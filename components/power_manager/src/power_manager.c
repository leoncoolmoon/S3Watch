#include "power_manager.h"
#include "bsp/display.h"
#include "bsp/esp32_s3_touch_amoled_2_06.h"
#include "task_coordinator.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#if CONFIG_PM_ENABLE
#include "esp_pm.h"
#endif

static const char *TAG = "POWER_MGR";

// ── awake/sleep state ──────────────────────────────────────────────────────
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
static const bsp_power_rail_t s_audio_rails[] = {
    BSP_POWER_RAIL_ALDO3,  // A3V3 — sole audio analog supply
};

// All switchable ALDOs — used only for bulk enable at init/wake.
static const bsp_power_rail_t s_all_aldo_rails[] = {
    BSP_POWER_RAIL_ALDO1, BSP_POWER_RAIL_ALDO2,
    BSP_POWER_RAIL_ALDO3, BSP_POWER_RAIL_ALDO4,
};
#define ALL_ALDO_COUNT (sizeof(s_all_aldo_rails) / sizeof(s_all_aldo_rails[0]))

static int s_rail_refcount[BSP_POWER_RAIL_COUNT] = {0};

static void client_rails(pm_rail_client_t client,
                         const bsp_power_rail_t **out_rails, size_t *out_count)
{
    switch (client) {
    case PM_RAIL_CLIENT_AUDIO:
        *out_rails = s_audio_rails;
        *out_count = sizeof(s_audio_rails) / sizeof(s_audio_rails[0]);
        break;
    default:
        *out_rails = NULL;
        *out_count = 0;
        break;
    }
}

void power_manager_rail_hold(pm_rail_client_t client)
{
    const bsp_power_rail_t *rails;
    size_t count;
    client_rails(client, &rails, &count);
    for (size_t i = 0; i < count; i++) {
        s_rail_refcount[rails[i]]++;
    }
}

void power_manager_rail_release(pm_rail_client_t client)
{
    const bsp_power_rail_t *rails;
    size_t count;
    client_rails(client, &rails, &count);
    for (size_t i = 0; i < count; i++) {
        if (s_rail_refcount[rails[i]] > 0) s_rail_refcount[rails[i]]--;
    }
}

// ── PM lock (no-light-sleep) ───────────────────────────────────────────────
#if CONFIG_PM_ENABLE
static esp_pm_lock_handle_t s_no_sleep_lock     = NULL;
static int                  s_no_sleep_refcount = 0;
#endif

void power_manager_no_sleep_hold(void)
{
#if CONFIG_PM_ENABLE
    s_no_sleep_refcount++;
    if (s_no_sleep_refcount == 1 && s_no_sleep_lock) {
        (void)esp_pm_lock_acquire(s_no_sleep_lock);
    }
#endif
}

void power_manager_no_sleep_release(void)
{
#if CONFIG_PM_ENABLE
    if (s_no_sleep_refcount > 0) {
        s_no_sleep_refcount--;
        if (s_no_sleep_refcount == 0 && s_no_sleep_lock) {
            (void)esp_pm_lock_release(s_no_sleep_lock);
        }
    }
#endif
}

// ── Sleep / wake sequences ─────────────────────────────────────────────────

void power_manager_request_sleep(void)
{
    if (!s_awake) return;
    s_awake = false;
    ESP_LOGI(TAG, "sleep");

    // 1. Notify all listeners: stop non-essential work.
    fire_event(PM_EVT_PREPARE_SLEEP);

    // 2. DCS panel sleep — ALDOs stay on throughout so the SPI command reaches
    //    the panel and it enters DCS sleep state.
    if (s_display_ops.on_sleep) s_display_ops.on_sleep();

    // 3. Gate ALDO3 (audio) if no audio client holds it.
    //    ALDO1/2/4 (display panel via J3) are intentionally left powered — cutting
    //    them power-cycles the AMOLED IC and Sleep-Out alone cannot reinit it on wake.
    if (s_rail_refcount[BSP_POWER_RAIL_ALDO3] == 0) {
        bsp_power_rail_enable(BSP_POWER_RAIL_ALDO3, false);
    }

    // 4. Release the "system awake" no-sleep lock.
    power_manager_no_sleep_release();
}

void power_manager_request_wake(pm_wake_src_t source)
{
    if (s_awake) return;
    s_awake = true;
    ESP_LOGI(TAG, "wake (src=%d)", (int)source);

    // 1. Re-acquire no-sleep lock before any I2C / display work.
    power_manager_no_sleep_hold();

    // 2. Restore ALDO3 (audio analog rail) if it was gated during sleep.
    //    ALDO1/2/4 were never cut, so re-enabling them is a safe no-op.
    //    Panel is in DCS sleep state; Sleep-Out is sufficient on wake.
    for (size_t i = 0; i < ALL_ALDO_COUNT; i++) {
        bsp_power_rail_enable(s_all_aldo_rails[i], true);
    }
    vTaskDelay(pdMS_TO_TICKS(5)); // let ALDO3 analog rail stabilize

    // 3. Wake the display (Sleep-Out, I2C recover, LVGL resume, brightness).
    if (s_display_ops.on_wake) s_display_ops.on_wake();

    // 4. Notify all listeners that the system is awake.
    fire_event(PM_EVT_WOKE_UP);
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

// ── init ──────────────────────────────────────────────────────────────────

void power_manager_init(void)
{
    // Tell BSP to never gate ALDOs inside bsp_display_sleep() — power_manager
    // drives all ALDO state directly via bsp_power_rail_enable().
    bsp_display_keep_aldo_alive(true);

    // Ensure all ALDO rails are up (on cold boot AXP2101 starts with them off;
    // FT5x06 touch and other peripherals need power before bsp_display_start).
    for (size_t i = 0; i < ALL_ALDO_COUNT; i++) {
        bsp_power_rail_enable(s_all_aldo_rails[i], true);
    }
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
}
