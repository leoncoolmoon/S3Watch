#include "power_manager.h"
#include "bsp/display.h"
#include "bsp/esp32_s3_touch_amoled_2_06.h"
#include "task_coordinator.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#if CONFIG_PM_ENABLE
#include "esp_pm.h"
#endif

static const char *TAG = "POWER_MGR";

// ── locking ────────────────────────────────────────────────────────────────
// Two mutexes, strict order: s_transition_mutex → s_ref_mutex → (BSP/AXP).
//
// s_transition_mutex serializes the whole request_sleep()/request_wake()
// sequences. Today every transition initiator happens to run on the
// task_coordinator task, but the API is public (display_manager_turn_on/off
// wrap it) and one new caller on another task would interleave the display
// op sequences — so the check-then-act on s_awake is serialized here. Locks
// taken INSIDE a transition (LVGL port lock, signalk lifecycle mux) are all
// bounded-timeout, so there is no unbounded inversion against this mutex.
//
// s_ref_mutex guards both refcount families (s_rail_refcount[],
// s_no_sleep_refcount) TOGETHER WITH their edge actions (rail toggle /
// esp_pm acquire-release). Refcounts are mutated from genuinely concurrent
// tasks today (audio tasks via audio_manager open/close vs the coordinator's
// display sleep/wake). Keeping the edge action inside the lock is what
// guarantees the hardware state always matches the settled refcount —
// deciding the edge under the lock but toggling outside would let an
// enable/disable pair land in reversed order. Hold time is one short I2C
// transaction worst-case. Never call back into power_manager from under it.
static SemaphoreHandle_t s_transition_mutex = NULL;
static SemaphoreHandle_t s_ref_mutex        = NULL;

// ── awake/sleep state ──────────────────────────────────────────────────────
// volatile: read lock-free by power_manager_is_awake() (task_coordinator's
// active_fn and others); written only under s_transition_mutex.
static volatile bool s_awake = true;  // system starts awake (boot is awake)

bool power_manager_is_awake(void) { return s_awake; }

// ── display ops vtable ─────────────────────────────────────────────────────
static pm_display_ops_t s_display_ops = {0};

void power_manager_register_display_ops(const pm_display_ops_t *ops)
{
    if (ops) s_display_ops = *ops;
}

// ── event listeners ────────────────────────────────────────────────────────
#define PM_MAX_LISTENERS 8

typedef struct { pm_event_cb_t cb; void *ctx; } pm_listener_t;
static pm_listener_t s_listeners[PM_MAX_LISTENERS];
static int           s_listener_count = 0;

void power_manager_add_listener(pm_event_cb_t cb, void *ctx)
{
    if (!cb || s_listener_count >= PM_MAX_LISTENERS) {
        ESP_LOGW(TAG, "listener registration failed (cb=%p count=%d)", cb, s_listener_count);
        return;
    }
    s_listeners[s_listener_count].cb  = cb;
    s_listeners[s_listener_count].ctx = ctx;
    s_listener_count++;
}

static void fire_event(pm_event_t evt)
{
    for (int i = 0; i < s_listener_count; i++) {
        if (s_listeners[i].cb) s_listeners[i].cb(evt, s_listeners[i].ctx);
    }
}

// ── ALDO rail resource management ─────────────────────────────────────────
//
// Schematic-confirmed rail map (doc/power-rails.md):
//   ALDO3 → A3V3   ES8311 (AVDD/DACVREF/ADCVREF) + ES7210 (VDDA/VDDM) — audio analog
//   ALDO1 → VL1_3.3V  no board-level consumers; feeds AMOLED J3 (display-side)
//   ALDO2 → VL2_3.3V  no board-level consumers; feeds AMOLED J3 (display-side)
//   ALDO4 → VL3_1.8V  no board-level consumers; feeds AMOLED J3 (display-side)
// AUDIO lock → ALDO3 (audio analog). On only while a codec is open.
static const bsp_power_rail_t s_audio_rails[] = {
    BSP_POWER_RAIL_ALDO3,  // A3V3 — sole audio analog supply
};

// DISPLAY lock → the three AMOLED panel rails (via J3). They come on and die
// together — on only while the display is on.
static const bsp_power_rail_t s_display_rails[] = {
    BSP_POWER_RAIL_ALDO1, BSP_POWER_RAIL_ALDO2, BSP_POWER_RAIL_ALDO4,
};

static int s_rail_refcount[BSP_POWER_RAIL_COUNT] = {0};

static void client_rails(pm_rail_client_t client,
                         const bsp_power_rail_t **out_rails, size_t *out_count)
{
    switch (client) {
    case PM_RAIL_CLIENT_AUDIO:
        *out_rails = s_audio_rails;
        *out_count = sizeof(s_audio_rails) / sizeof(s_audio_rails[0]);
        break;
    case PM_RAIL_CLIENT_DISPLAY:
        *out_rails = s_display_rails;
        *out_count = sizeof(s_display_rails) / sizeof(s_display_rails[0]);
        break;
    default:
        *out_rails = NULL;
        *out_count = 0;
        break;
    }
}

static const char *rail_client_name(pm_rail_client_t client)
{
    switch (client) {
    case PM_RAIL_CLIENT_AUDIO:   return "AUDIO";
    case PM_RAIL_CLIENT_DISPLAY: return "DISPLAY";
    default:                     return "?";
    }
}

// rail_hold/release ARE the lock: power_manager owns the physical rails and is
// the only code that toggles them. A rail is powered iff some consumer holds it
// — enable on the 0→1 edge, disable on the 1→0 edge. Each physical toggle is
// logged (with the rail # and the client) so rail activity is visible.
void power_manager_rail_hold(pm_rail_client_t client)
{
    const bsp_power_rail_t *rails;
    size_t count;
    client_rails(client, &rails, &count);
    if (s_ref_mutex) xSemaphoreTake(s_ref_mutex, portMAX_DELAY);
    for (size_t i = 0; i < count; i++) {
        if (s_rail_refcount[rails[i]]++ == 0) {
            bsp_power_rail_enable(rails[i], true);
            ESP_LOGI(TAG, "ALDO%d ON  (%s hold)",
                     (int)(rails[i] - BSP_POWER_RAIL_ALDO1 + 1), rail_client_name(client));
        }
    }
    if (s_ref_mutex) xSemaphoreGive(s_ref_mutex);
}

void power_manager_rail_release(pm_rail_client_t client)
{
    const bsp_power_rail_t *rails;
    size_t count;
    client_rails(client, &rails, &count);
    if (s_ref_mutex) xSemaphoreTake(s_ref_mutex, portMAX_DELAY);
    for (size_t i = 0; i < count; i++) {
        if (s_rail_refcount[rails[i]] > 0 && --s_rail_refcount[rails[i]] == 0) {
            bsp_power_rail_enable(rails[i], false);
            ESP_LOGI(TAG, "ALDO%d OFF (%s release)",
                     (int)(rails[i] - BSP_POWER_RAIL_ALDO1 + 1), rail_client_name(client));
        }
    }
    if (s_ref_mutex) xSemaphoreGive(s_ref_mutex);
}

// Read the ACTUAL AXP2101 LDO enable bits back (not our refcounts) so the gating
// can be confirmed on-device. ALDO1/2/4 should read 0 in display-off; ALDO3
// should read 1 only while audio is playing.
static void log_aldo_states(const char *when)
{
    ESP_LOGI(TAG, "ALDO @%s: 1=%d 2=%d 3=%d 4=%d (1=on,0=off)", when,
             bsp_power_rail_is_enabled(BSP_POWER_RAIL_ALDO1),
             bsp_power_rail_is_enabled(BSP_POWER_RAIL_ALDO2),
             bsp_power_rail_is_enabled(BSP_POWER_RAIL_ALDO3),
             bsp_power_rail_is_enabled(BSP_POWER_RAIL_ALDO4));
}

// ── PM lock (no-light-sleep) ───────────────────────────────────────────────
#if CONFIG_PM_ENABLE
static esp_pm_lock_handle_t s_no_sleep_lock     = NULL;
static int                  s_no_sleep_refcount = 0;
#endif

void power_manager_no_sleep_hold(void)
{
#if CONFIG_PM_ENABLE
    if (s_ref_mutex) xSemaphoreTake(s_ref_mutex, portMAX_DELAY);
    s_no_sleep_refcount++;
    if (s_no_sleep_refcount == 1 && s_no_sleep_lock) {
        (void)esp_pm_lock_acquire(s_no_sleep_lock);
    }
    if (s_ref_mutex) xSemaphoreGive(s_ref_mutex);
#endif
}

void power_manager_no_sleep_release(void)
{
#if CONFIG_PM_ENABLE
    if (s_ref_mutex) xSemaphoreTake(s_ref_mutex, portMAX_DELAY);
    if (s_no_sleep_refcount > 0) {
        s_no_sleep_refcount--;
        if (s_no_sleep_refcount == 0 && s_no_sleep_lock) {
            (void)esp_pm_lock_release(s_no_sleep_lock);
        }
    }
    if (s_ref_mutex) xSemaphoreGive(s_ref_mutex);
#endif
}

// ── Sleep / wake sequences ─────────────────────────────────────────────────

void power_manager_request_sleep(void)
{
    // Serialize against a concurrent wake/sleep from another task: the
    // check-then-act on s_awake and the multi-step sequence below must not
    // interleave with request_wake()'s (display ops would corrupt).
    if (s_transition_mutex) xSemaphoreTake(s_transition_mutex, portMAX_DELAY);
    if (!s_awake) {
        if (s_transition_mutex) xSemaphoreGive(s_transition_mutex);
        return;
    }
    s_awake = false;
    ESP_LOGI(TAG, "sleep");

    // 1. Notify all listeners: stop non-essential work. NOTIFY audio releases its
    //    AUDIO lock here (audio_manager_suspend), so ALDO3 drops unless music/alarm
    //    still hold it.
    fire_event(PM_EVT_PREPARE_SLEEP);

    // 2. Display sleep. on_sleep (display_manager) sends the DCS sleep-in and then
    //    releases the DISPLAY lock — that's what cuts ALDO1/2/4 (panel power-cycled,
    //    so wake does a full reinit). power_manager touches no rails directly; rail
    //    state flows entirely from the consumer locks.
    if (s_display_ops.on_sleep) s_display_ops.on_sleep();

    // 3. Confirm the actual PMU rail bits (read back from the AXP2101).
    log_aldo_states("sleep");

    // 4. Release the "system awake" no-sleep lock.
    power_manager_no_sleep_release();

    if (s_transition_mutex) xSemaphoreGive(s_transition_mutex);
}

void power_manager_request_wake(pm_wake_src_t source)
{
    if (s_transition_mutex) xSemaphoreTake(s_transition_mutex, portMAX_DELAY);
    if (s_awake) {
        if (s_transition_mutex) xSemaphoreGive(s_transition_mutex);
        return;
    }
    s_awake = true;
    ESP_LOGI(TAG, "wake (src=%d)", (int)source);

    // 1. Re-acquire no-sleep lock before any I2C / display work.
    power_manager_no_sleep_hold();

    // 2. Wake the display. on_wake (display_manager) re-holds the DISPLAY lock —
    //    that powers ALDO1/2/4 back on — then reinitialises the power-cycled panel.
    //    power_manager touches no rails directly; ALDO3 stays whatever audio dictates.
    if (s_display_ops.on_wake) s_display_ops.on_wake();

    // 3. Confirm the actual PMU rail bits (read back from the AXP2101).
    log_aldo_states("wake");

    // 4. Notify all listeners that the system is awake.
    fire_event(PM_EVT_WOKE_UP);

    if (s_transition_mutex) xSemaphoreGive(s_transition_mutex);
}

// ── task_coordinator subscribers ──────────────────────────────────────────

static void wake_poll_cb(void *user)
{
    (void)user;
    if (bsp_power_poll_pwr_button_short()) {
        power_manager_request_wake(PM_WAKE_BUTTON);
    }
}

static void pmu_state_cb(void *user)
{
    (void)user;
    bsp_power_refresh_state();
}

// Battery power telemetry. The AXP2101 has NO battery-current ADC (datasheet
// Table 6-7 — ADC is VBAT/VBUS/VSYS/TS/die-temp only), so we can't read mA;
// instead log SOC% + VBAT over time and compute the drain rate offline. Only
// meaningful on battery (on USB the PMU is charging), so skip when VBUS is in.
// The line is mirrored to /sdcard/logs/ by sd_logger when SD logging is enabled.
// aldo=<1><2><3><4> doubles as per-sample proof the display rails are cut in
// display-off (1/2/4 read 0) and that ALDO3 is on only during audio.
static void batt_telemetry_cb(void *user)
{
    (void)user;
    if (bsp_power_is_vbus_in()) return;
    ESP_LOGI(TAG, "PWRLOG up=%lld awake=%d soc=%d vbat=%d aldo=%d%d%d%d",
             (long long)(esp_timer_get_time() / 1000000),
             s_awake ? 1 : 0,
             bsp_power_get_battery_percent(),
             bsp_power_get_batt_voltage_mv(),
             bsp_power_rail_is_enabled(BSP_POWER_RAIL_ALDO1),
             bsp_power_rail_is_enabled(BSP_POWER_RAIL_ALDO2),
             bsp_power_rail_is_enabled(BSP_POWER_RAIL_ALDO3),
             bsp_power_rail_is_enabled(BSP_POWER_RAIL_ALDO4));
}

// ── init ──────────────────────────────────────────────────────────────────

void power_manager_init(void)
{
    // Locks first — power_manager_rail_hold below is already a user, and
    // audio_manager_init (next in app_main) takes the AUDIO rail.
    if (!s_transition_mutex) s_transition_mutex = xSemaphoreCreateMutex();
    if (!s_ref_mutex)        s_ref_mutex        = xSemaphoreCreateMutex();

    // Tell BSP to never gate ALDOs inside bsp_display_sleep() — power_manager is
    // the sole owner of every ALDO rail and drives them via the consumer locks.
    bsp_display_keep_aldo_alive(true);

    // Seed the boot state: the display comes up on (display_on=true, no wake call),
    // so hold the DISPLAY lock once here — that powers ALDO1/2/4 for bsp_display_start
    // and the FT5x06 touch IC. Thereafter display_manager releases on sleep and
    // re-holds on wake, keeping the refcount balanced. ALDO3 stays off until audio
    // (audio_manager_init brackets codec bring-up with the AUDIO lock).
    power_manager_rail_hold(PM_RAIL_CLIENT_DISPLAY);
    vTaskDelay(pdMS_TO_TICKS(50)); // let rails stabilize

#if CONFIG_PM_ENABLE
    if (!s_no_sleep_lock) {
        (void)esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "power_mgr", &s_no_sleep_lock);
    }
    // Acquire the "system awake" hold; refcount starts at 0, hold takes it to 1.
    power_manager_no_sleep_hold();

    // Enable DFS + automatic light sleep.  The lock above prevents light sleep
    // during the remainder of boot init.
    esp_pm_config_t pm_cfg = {
        .max_freq_mhz       = 240,
        .min_freq_mhz       = 40,
        .light_sleep_enable = true,
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_cfg));
#endif

    // task_coordinator: 100 ms base tick when awake, 1000 ms when sleeping.
    // Uses power_manager_is_awake as the active_fn (replaces display_manager_is_on).
    task_coord_init(100, 1000, power_manager_is_awake);

    // PMU power button: poll only while sleeping (display_manager's ui.c handles
    // it when awake to avoid racing over the AXP2101 IRQ status register).
    task_coord_subscribe("pm_wake_btn", wake_poll_cb, NULL,
                         /*on*/  0,      // skip when awake
                         /*off*/ 1000);  // 1 Hz while sleeping — snappy wake

    // PMU state (charge/VBUS transitions): fires bsp_power_event_t callbacks.
    task_coord_subscribe("pm_pmu_state", pmu_state_cb, NULL,
                         /*on*/  3000,  // 3 s — charge/VBUS changes are slow
                         /*off*/ 5000); // 5 s while sleeping

    // Battery power telemetry (PWRLOG): 60 s in both display-on and -off so
    // drain can be compared. Self-gates to on-battery; captured to SD when SD
    // logging is enabled. Negligible cost (task_coord already wakes ≥1 Hz off).
    task_coord_subscribe("batt_telemetry", batt_telemetry_cb, NULL,
                         /*on*/  60000,
                         /*off*/ 60000);
}
