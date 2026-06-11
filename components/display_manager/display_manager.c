#include "display_manager.h"
#include "power_manager.h"
#include "bsp/display.h"
#include "bsp/esp32_s3_touch_amoled_2_06.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "settings.h"
#include "bsp_board_extra.h"
#include "rtc_lib.h"
#include "task_coordinator.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "sdkconfig.h"
#if CONFIG_PM_PROFILING
#include <stdio.h>
#include "esp_heap_caps.h"
#include "esp_pm.h"
#endif

static const char *TAG = "DISPLAY_MGR";

static void (*s_display_on_cb)(void)  = NULL;
static display_pre_show_cb_t s_pre_show_cb = NULL;

void display_manager_set_on_callback(void (*cb)(void)) { s_display_on_cb = cb; }
void display_manager_set_pre_show_cb(display_pre_show_cb_t cb) { s_pre_show_cb = cb; }

static bool display_on = true;
static uint32_t timeout_ms;
static TaskHandle_t s_lvgl_task = NULL;

// Two cycles drive bright → dim → off:
//
//   CYCLE_WAKE  (fresh wake from off): bright for T/2, dim for T/2, off at T
//                where T = settings_get_display_timeout()
//   CYCLE_TOUCH (after any user activity during display-on):
//                bright for TOUCH_BRIGHT_MS, dim for TOUCH_DIM_MS, then off
//
// All thresholds are measured against lv_disp_get_inactive_time(NULL), which
// LVGL keeps updated on every input event regardless of which screen is
// active — so no need to wire touch events per-screen.
typedef enum { CYCLE_WAKE, CYCLE_TOUCH } dim_cycle_t;
#define DIM_BRIGHTNESS_PCT 30
#define TOUCH_BRIGHT_MS    5000
#define TOUCH_DIM_MS       5000
#define TOUCH_CYCLE_OFF_MS (TOUCH_BRIGHT_MS + TOUCH_DIM_MS)

static dim_cycle_t s_cycle = CYCLE_WAKE;
static uint32_t    s_last_inactive_ms = 0;
static bool        s_dimmed = false;
static uint8_t     s_pre_dim_brightness = 0;

// FT3168 Monitor mode: after this much idle time, drop the touch IC from
// Active (~1.5 mA) to Monitor (~30 µA). The chip auto-transitions back to
// Active when it sees a touch. We track our own intent so we don't re-issue
// the I2C write on every coordinator tick.
#define TOUCH_MONITOR_AFTER_MS 10000
#define FT3168_MODE_ACTIVE     0x00
#define FT3168_MODE_MONITOR    0x01
#define FT3168_MODE_SLEEP      0x03
static bool s_touch_in_monitor = false;

// ── display ops: called by power_manager during sleep/wake sequences ───────

static void display_sleep_impl(void)
{
    if (!display_on) return;
    ESP_LOGI(TAG, "Turning display off");

    // Stop LVGL timers to pause flushing while panel sleeps. Take LVGL lock to
    // avoid in-flight flush.
    if (lvgl_port_lock(200)) {
        lvgl_port_stop();
        if (s_lvgl_task) vTaskSuspend(s_lvgl_task);
        lvgl_port_unlock();
    } else {
        ESP_LOGW(TAG, "LVGL lock timeout on sleep — stopping anyway");
        lvgl_port_stop();
        if (s_lvgl_task) vTaskSuspend(s_lvgl_task);
    }

    // Disable touch input polling before putting the IC to sleep.
    lv_indev_t *indev = bsp_display_get_input_dev();
    if (indev) lv_indev_enable(indev, false);

    // Drop FT3168 to Monitor mode (~30 µA). Sleep mode (~10 µA) would save a bit
    // more but requires a hardware RESETB pulse to exit — which also resets the LCD
    // panel and forces a full panel reinit on wake. Monitor mode exits cleanly via
    // I2C and auto-exits on touch, keeping the wake path simple.
    (void)bsp_extra_touch_set_mode(FT3168_MODE_MONITOR);

    // Send DCS panel sleep-in (panel only — power_manager owns the rails), then
    // release the DISPLAY lock. That cuts ALDO1/2/4, fully powering down the panel
    // for low standby; wake does a full reinit. Order matters: sleep-in first, then
    // pull power.
    bsp_display_sleep();
    bsp_display_brightness_set(0);
    power_manager_rail_release(PM_RAIL_CLIENT_DISPLAY);

    // Reset cycle/dim state so the next display-on session starts fresh.
    s_dimmed           = false;
    s_cycle            = CYCLE_WAKE;
    s_last_inactive_ms = 0;
    s_touch_in_monitor = false;

    display_on = false;
}

static void display_wake_impl(void)
{
    if (display_on) return;
    ESP_LOGI(TAG, "Turning display on");

    // Re-hold the DISPLAY lock to power ALDO1/2/4 back on, let them settle, then
    // fully reinit the panel — it was power-cycled during sleep, so DCS Sleep-Out
    // is not enough.
    power_manager_rail_hold(PM_RAIL_CLIENT_DISPLAY);
    vTaskDelay(pdMS_TO_TICKS(5));
    bsp_display_wake_from_gated();

    // Reset I2C bus after ALDO rails restore to clear any stuck state.
    bsp_extra_i2c_recover();

    lvgl_port_resume();
    if (s_lvgl_task) vTaskResume(s_lvgl_task);

    if (lvgl_port_lock(200)) {
        // Reset the inactivity timer while we hold the lock — critical after
        // wake because LVGL's tick kept running during sleep so inactive_time
        // would immediately exceed the timeout without this reset.
        lv_disp_trig_activity(NULL);
        s_cycle            = CYCLE_WAKE;
        s_last_inactive_ms = 0;
        s_dimmed           = false;
#if LVGL_VERSION_MAJOR >= 9
        lv_display_t *disp = lv_display_get_default();
        lv_obj_t     *scr  = disp ? lv_scr_act() : NULL;
#else
        lv_disp_t *disp = lv_disp_get_default();
        lv_obj_t  *scr  = disp ? lv_disp_get_scr_act(disp) : NULL;
#endif
        if (scr) lv_obj_invalidate(scr);
        // Populate widgets with fresh state (time / battery) so the first
        // visible frame is already correct, then force a synchronous render
        // before the backlight comes up.
        if (s_pre_show_cb) s_pre_show_cb();
        lv_refr_now(NULL);
        lvgl_port_unlock();
    }

    bsp_display_brightness_set(settings_get_brightness());

    // Confirm Active mode. If the IC is on an ALDO it was power-cycled
    // (Active by default on boot). If it is on VCC3V3 the RESETB pulse above
    // exited Sleep mode; the panel reinit covered the 70ms Trsi settle time.
    (void)bsp_extra_touch_set_mode(FT3168_MODE_ACTIVE);
    s_touch_in_monitor = false;

    lv_indev_t *indev = bsp_display_get_input_dev();
    if (indev) lv_indev_enable(indev, true);

    display_on = true;
    if (s_display_on_cb) s_display_on_cb();
}

// ── public turn on/off ─────────────────────────────────────────────────────

void display_manager_turn_off(void) { power_manager_request_sleep(); }
void display_manager_turn_on(void)  { power_manager_request_wake(PM_WAKE_BUTTON); }

bool display_manager_is_on(void) { return display_on; }

// Treat a call to this function as "user activity just happened". Callers
// outside the LVGL touch path (PMU short-press / GPIO 0 back-button ISR) use
// this so the inactivity_check_cb sees a real activity transition. LVGL touch
// events already bump the inactivity counter, so they don't need to call this.
void display_manager_reset_timer(void)
{
    lv_disp_trig_activity(NULL);
    s_touch_in_monitor = false;
}

// ── task_coordinator subscribers ──────────────────────────────────────────

#if CONFIG_PM_PROFILING
static void pm_profile_dump_cb(void *user)
{
    (void)user;
    ESP_LOGI(TAG, "================ PM profile (display_on=%d) ================", display_on);
    esp_pm_dump_locks(stdout);

    size_t free_int   = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t min_int    = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    size_t large_int  = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t min_psram  = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "Heap  internal: free=%u min=%u largest=%u  PSRAM: free=%u min=%u",
             (unsigned)free_int, (unsigned)min_int, (unsigned)large_int,
             (unsigned)free_psram, (unsigned)min_psram);

    ESP_LOGI(TAG, "Battery: %d%% Vbat=%dmV Vbus=%dmV Vsys=%dmV charging=%d vbus_in=%d",
             bsp_power_get_battery_percent(),
             bsp_power_get_batt_voltage_mv(),
             bsp_power_get_vbus_voltage_mv(),
             bsp_power_get_system_voltage_mv(),
             bsp_power_is_charging() ? 1 : 0,
             bsp_power_is_vbus_in() ? 1 : 0);

    task_coord_dump_stats();
}
#endif

static void inactivity_check_cb(void *user)
{
    (void)user;
    if (!display_on) return;

    // Re-read timeout each tick so a settings change applies immediately.
    timeout_ms = settings_get_display_timeout();

    uint32_t inactive = lv_disp_get_inactive_time(NULL);

    // Detect that LVGL just saw user input: inactive_time dropped vs. last tick.
    bool activity_just_now = (inactive < s_last_inactive_ms);
    s_last_inactive_ms = inactive;

    if (activity_just_now) {
        s_cycle = CYCLE_TOUCH;
        if (s_dimmed) {
            bsp_display_brightness_set(s_pre_dim_brightness);
            s_dimmed = false;
        }
    }

    uint32_t dim_at, off_at;
    if (s_cycle == CYCLE_WAKE) {
        if (timeout_ms < 2) timeout_ms = 2;
        off_at = timeout_ms;
        dim_at = timeout_ms / 2;
    } else {
        off_at = TOUCH_CYCLE_OFF_MS;
        dim_at = TOUCH_BRIGHT_MS;
    }

    if (inactive >= off_at) {
        power_manager_request_sleep();
        return;
    }
    if (!s_dimmed && inactive >= dim_at) {
        s_pre_dim_brightness = settings_get_brightness();
        uint8_t dim_level = (s_pre_dim_brightness * DIM_BRIGHTNESS_PCT) / 100;
        if (dim_level < 1) dim_level = 1;
        bsp_display_brightness_set(dim_level);
        s_dimmed = true;
    }
}

static void touch_idle_check_cb(void *user)
{
    (void)user;
    if (!display_on) return;
    if (s_touch_in_monitor) return;
    uint32_t inactive = lv_disp_get_inactive_time(NULL);
    if (inactive < TOUCH_MONITOR_AFTER_MS) return;
    esp_err_t err = bsp_extra_touch_set_mode(FT3168_MODE_MONITOR);
    if (err == ESP_OK) {
        s_touch_in_monitor = true;
        ESP_LOGI(TAG, "FT3168 -> Monitor mode (idle %ums)", (unsigned)inactive);
    } else {
        ESP_LOGD(TAG, "FT3168 mode-write failed (will retry): %s", esp_err_to_name(err));
    }
}

// ── init ──────────────────────────────────────────────────────────────────

void display_manager_init(void)
{
    timeout_ms  = settings_get_display_timeout();
    s_lvgl_task = xTaskGetHandle("taskLVGL");
    if (!s_lvgl_task) {
        ESP_LOGW(TAG, "taskLVGL handle not found — LVGL suspend optimization disabled");
    }

    // Register display hardware ops so power_manager drives the sleep/wake sequence.
    static const pm_display_ops_t dm_ops = {
        .on_sleep = display_sleep_impl,
        .on_wake  = display_wake_impl,
    };
    power_manager_register_display_ops(&dm_ops);

    // task_coordinator was initialised by power_manager_init; subscribe
    // display-specific callbacks here, then start the task.
    task_coord_subscribe("inactivity_check", inactivity_check_cb, NULL,
                         /*on*/  100,   // 100 ms granularity vs 30 s display timeout
                         /*off*/ 0);    // skip when display already off

    task_coord_subscribe("rtc_minute_sync",  (task_coord_cb_t)rtc_minute_sync, NULL,
                         /*on*/  60000, // 1/min: PCF85063A → settimeofday → NVS write
                         /*off*/ 0);    // skip — display-off NVS save is done in PM listener

    task_coord_subscribe("touch_idle",       touch_idle_check_cb, NULL,
                         /*on*/  1000,  // check once per second when display on
                         /*off*/ 0);    // IC is in Sleep mode when display off; skip

#if CONFIG_PM_PROFILING
    task_coord_subscribe("pm_profile_dump", pm_profile_dump_cb, NULL,
                         /*on*/  30000,
                         /*off*/ 30000);
#endif

    // Start the task AFTER all subscribes so the coordinator never runs a
    // partial subscriber list on the other core.
    task_coord_start();
}
