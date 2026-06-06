#include "display_manager.h"
#include "bsp/display.h"
#include "bsp/esp32_s3_touch_amoled_2_06.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "settings.h"
#include "audio_alert.h"
#include "rtc_lib.h"
#include "bsp_board_extra.h"
#include "task_coordinator.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "sdkconfig.h"
#if CONFIG_PM_ENABLE
#include "esp_pm.h"
#endif
#if CONFIG_PM_PROFILING
#include <stdio.h>
#include "esp_heap_caps.h"
#endif

// If the board provides simple GPIO buttons, use one as wake key.
// On this hardware BSP_CAPS_BUTTONS is 0, so we will use the PMU PWR key
// instead.
#define DISPLAY_BUTTON GPIO_NUM_0

static const char *TAG = "DISPLAY_MGR";

static void (*s_display_on_cb)(void) = NULL;
static display_pre_show_cb_t s_pre_show_cb = NULL;
static display_pre_off_cb_t  s_pre_off_cb  = NULL;

void display_manager_set_on_callback(void (*cb)(void)) { s_display_on_cb = cb; }
void display_manager_set_pre_show_cb(display_pre_show_cb_t cb) { s_pre_show_cb = cb; }
void display_manager_set_pre_off_cb (display_pre_off_cb_t  cb) { s_pre_off_cb  = cb; }

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
#if CONFIG_PM_ENABLE
static esp_pm_lock_handle_t s_no_ls_lock = NULL;
#endif

static void display_turn_off_internal(void) {
  if (!display_on) {
    return;
  }
  ESP_LOGI(TAG, "Turning display off");
  // Give consumers a chance to tear down resources whose lifetime tracks
  // "display on" (SignalK WS + WiFi, etc.) BEFORE we suspend LVGL and
  // power-down the rails. Must be cheap (<50 ms) to keep sleep snappy.
  if (s_pre_off_cb) s_pre_off_cb();
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
#if CONFIG_PM_ENABLE
  if (s_no_ls_lock) {
    (void)esp_pm_lock_release(s_no_ls_lock);
  }
#endif
  // Disable touch input polling before putting the IC to sleep.
  lv_indev_t *indev = bsp_display_get_input_dev();
  if (indev) {
    lv_indev_enable(indev, false);
  }
  // Drive FT3168 into Sleep mode (~10 µA) before ALDO rails are cut.
  // If the IC is on an always-on supply this saves ~20 µA vs Monitor mode.
  // If the ALDO cuts its power the rail-cut achieves 0 µA regardless; the
  // write is a belt-and-suspenders no-op in that case.
  (void)bsp_extra_touch_set_mode(FT3168_MODE_SLEEP);
  // Stop audio and RTC polling before ALDO rails are cut
  audio_alert_suspend();
  rtc_suspend();
  // Put panel into low-power sleep and ensure backlight is off
  bsp_display_sleep();
  bsp_display_brightness_set(0);
  // Reset cycle/dim state so the next display-on session starts fresh.
  s_dimmed = false;
  s_cycle = CYCLE_WAKE;
  s_last_inactive_ms = 0;
  // Clear intent flag — wake path resets the IC via RESETB and writes Active.
  s_touch_in_monitor = false;
  // PM lock released — CPU may enter light sleep while display is off.
  // PMU interrupt (GPIO 35) wakes the CPU via ISR + semaphore.
  display_on = false;
}

void display_manager_turn_off(void) { display_turn_off_internal(); }

void display_manager_turn_on(void) {
  if (!display_on) {
    ESP_LOGI(TAG, "Turning display on");
#if defined(BSP_LCD_TOUCH_RST)
    // Pulse RESETB to wake the FT3168 from Sleep mode before bringing up the
    // display. GPIO_NUM_9 is shared with the LCD reset line; pulsing it here
    // is safe because the LCD is still asleep and bsp_display_wake() below
    // re-initialises it from scratch (including its own panel reset).
    // The panel reinit (~400ms) gives FT3168 well over the Trsi = 70ms settle
    // time it needs after RST release, so no extra delay is required here.
    gpio_set_direction(BSP_LCD_TOUCH_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(BSP_LCD_TOUCH_RST, 0);  // assert RESETB low
    vTaskDelay(pdMS_TO_TICKS(5));           // Trst min = 5ms
    gpio_set_level(BSP_LCD_TOUCH_RST, 1);  // release RESETB
#endif
    // Wake the panel first — this re-enables ALDOs and waits for them to settle
    bsp_display_wake();
    // Reset I2C bus after ALDO rails restore to clear any stuck state
    bsp_extra_i2c_recover();
    // Restart RTC polling now that I2C bus is powered again
    rtc_resume();
    // NOTE: no pre-clear here. The full-screen lv_obj_invalidate +
    // lv_refr_now below paints the entire panel; a separate clear pushed
    // via the same DMA path would interleave with the LVGL flush and
    // produce a mangled first frame once brightness rises.
    lvgl_port_resume();
    if (s_lvgl_task) vTaskResume(s_lvgl_task);

    if (lvgl_port_lock(200)) {
      // Reset the inactivity timer while we hold the lock — critical after
      // wake because LVGL's tick kept running during sleep so inactive_time
      // would immediately exceed the timeout without this reset.
      lv_disp_trig_activity(NULL);
      // Fresh wake → use the long initial cycle (T/2 bright, T/2 dim).
      s_cycle = CYCLE_WAKE;
      s_last_inactive_ms = 0;
      s_dimmed = false;
#if LVGL_VERSION_MAJOR >= 9
      lv_display_t *disp = lv_display_get_default();
      lv_obj_t *scr = disp ? lv_scr_act() : NULL;
#else
      lv_disp_t *disp = lv_disp_get_default();
      lv_obj_t *scr = disp ? lv_disp_get_scr_act(disp) : NULL;
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
    if (indev) {
      lv_indev_enable(indev, true);
    }
    display_on = true;
    if (s_display_on_cb) s_display_on_cb();
  }
  // Prevent light sleep while actively displaying UI for responsiveness
#if CONFIG_PM_ENABLE
  if (s_no_ls_lock) {
    (void)esp_pm_lock_acquire(s_no_ls_lock);
  }
#endif
  display_manager_reset_timer();
}

bool display_manager_is_on(void) { return display_on; }

// Treat a call to this function as "user activity just happened". Callers
// outside the LVGL touch path (PMU short-press / GPIO 0 back-button ISR) use
// this so the inactivity_check_cb sees a real activity transition. LVGL touch
// events already bump the inactivity counter, so they don't need to call this.
void display_manager_reset_timer(void) {
  lv_disp_trig_activity(NULL);
  // The brightness restore + cycle switch now happens in inactivity_check_cb
  // on the next tick (it detects the inactivity-counter drop). We just clear
  // the touch-IC tracking flag here — the FT3168 self-recovers to Active on
  // a real touch, so an explicit "back" press tells us it's effectively
  // active again on the host side.
  s_touch_in_monitor = false;
}

static bool wake_button_pressed(void) {
#if BSP_CAPS_BUTTONS
  return gpio_get_level(DISPLAY_BUTTON) == 0;
#else
  // Poll AXP2101 power key short-press event
  return bsp_power_poll_pwr_button_short();
#endif
}

#if CONFIG_PM_PROFILING
// Coordinator subscriber: every 30 s, dump a rich power/health snapshot.
// Cheap to read; serial output is the only meaningful cost. Disable
// CONFIG_PM_PROFILING in sdkconfig to compile this out entirely.
static void pm_profile_dump_cb(void *user) {
  (void)user;
  ESP_LOGI(TAG, "================ PM profile (display_on=%d) ================", display_on);
  // PM locks + per-mode time + light_sleep_counts (from esp_pm).
  esp_pm_dump_locks(stdout);

  // Heap + PSRAM: helps catch slow leaks alongside the power view.
  size_t free_int   = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  size_t min_int    = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
  size_t large_int  = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  size_t min_psram  = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
  ESP_LOGI(TAG, "Heap  internal: free=%u min=%u largest=%u  PSRAM: free=%u min=%u",
           (unsigned)free_int, (unsigned)min_int, (unsigned)large_int,
           (unsigned)free_psram, (unsigned)min_psram);

  // Battery / charging / VBUS — useful for correlating current draw with state.
  ESP_LOGI(TAG, "Battery: %d%% Vbat=%dmV Vbus=%dmV Vsys=%dmV charging=%d vbus_in=%d",
           bsp_power_get_battery_percent(),
           bsp_power_get_batt_voltage_mv(),
           bsp_power_get_vbus_voltage_mv(),
           bsp_power_get_system_voltage_mv(),
           bsp_power_is_charging() ? 1 : 0,
           bsp_power_is_vbus_in() ? 1 : 0);

  // Coordinator subscriber stats (calls, avg/max execution time).
  task_coord_dump_stats();
}
#endif

// Coordinator subscriber: drives the bright → dim → off cycle from
// lv_disp_get_inactive_time(). Using the LVGL inactivity counter as the
// single source of truth means touches on ANY screen (tileview, settings,
// flashlight, ...) correctly reset the cycle, not just the boot-time
// lv_scr_act() like the previous touch_event_cb did.
static void inactivity_check_cb(void *user) {
  (void)user;
  if (!display_on) return;

  // Re-read timeout each tick so a settings change applies immediately.
  timeout_ms = settings_get_display_timeout();

  uint32_t inactive = lv_disp_get_inactive_time(NULL);

  // Detect that LVGL just saw user input: inactive_time dropped vs. last tick.
  // (It only resets on real input events; lv_async_call etc. don't bump it.)
  bool activity_just_now = (inactive < s_last_inactive_ms);
  s_last_inactive_ms = inactive;

  if (activity_just_now) {
    // User activity → switch to the short follow-up cycle (5 s bright + 5 s
    // dim) and immediately restore brightness if we were dimmed.
    s_cycle = CYCLE_TOUCH;
    if (s_dimmed) {
      bsp_display_brightness_set(s_pre_dim_brightness);
      s_dimmed = false;
    }
  }

  // Pick the dim / off thresholds for the active cycle.
  uint32_t dim_at, off_at;
  if (s_cycle == CYCLE_WAKE) {
    if (timeout_ms < 2) timeout_ms = 2;  // defensive
    off_at = timeout_ms;
    dim_at = timeout_ms / 2;
  } else {  // CYCLE_TOUCH
    off_at = TOUCH_CYCLE_OFF_MS;
    dim_at = TOUCH_BRIGHT_MS;
  }

  if (inactive >= off_at) {
    display_turn_off_internal();
    return;
  }
  if (!s_dimmed && inactive >= dim_at) {
    s_pre_dim_brightness = settings_get_brightness();
    uint8_t dim_level = (s_pre_dim_brightness * DIM_BRIGHTNESS_PCT) / 100;
    if (dim_level < 1) dim_level = 1;  // never go to 0 while nominally "on"
    bsp_display_brightness_set(dim_level);
    s_dimmed = true;
  }
}

// Coordinator subscriber: after extended idle (no touch / no widget activity),
// drop the FT3168 touch controller into its low-power Monitor mode. The chip
// auto-transitions back to Active when it sees a real touch; the
// display_manager_reset_timer() side clears our intent flag, so we'll re-arm
// for the next idle window without further intervention.
static void touch_idle_check_cb(void *user) {
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
    // Datasheet sec 2.3: the touch IC's I2C state machine can be temporarily
    // out of sync if other bus traffic intervened. The IC self-clears state
    // periodically; we'll retry on the next coordinator tick.
    ESP_LOGD(TAG, "FT3168 mode-write failed (will retry): %s", esp_err_to_name(err));
  }
}

// Coordinator subscriber: poll the PMU PWR key for *wake* only (display OFF).
// The "PMU-press as Back when display is ON" role lives in the UI layer
// (see ui.c — registered as a separate subscriber that runs only when on).
// We deliberately don't read the latch when display is on so we don't race
// with that subscriber over the AXP2101 IRQ-status register clear.
static void wake_button_cb(void *user) {
  (void)user;
  if (display_on) return;
  if (wake_button_pressed()) {
    display_manager_turn_on();
  }
}

void display_manager_init(void) {
  timeout_ms = settings_get_display_timeout();
  s_lvgl_task = xTaskGetHandle("taskLVGL");
  if (!s_lvgl_task) {
    ESP_LOGW(TAG, "taskLVGL handle not found — LVGL suspend optimization disabled");
  }
#if BSP_CAPS_BUTTONS
  gpio_config_t io_conf = {
      .pin_bit_mask = 1ULL << DISPLAY_BUTTON,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&io_conf);
#else
  ESP_LOGI(TAG, "Using PMU PWR key to wake display");
#endif

  // No per-screen LVGL touch handler: inactivity_check_cb reads
  // lv_disp_get_inactive_time(NULL) which LVGL updates for ALL input events
  // regardless of which screen is active, so navigation between tiles /
  // settings / flashlight all reset the dim cycle correctly.

  // PM lock may be created in early init; if not, create and acquire now.
  // IMPORTANT: only acquire when newly creating — otherwise we'd double-acquire
  // (early-init already holds it) and the count would never reach zero on release.
#if CONFIG_PM_ENABLE
  if (!s_no_ls_lock) {
    (void)esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "display",
                             &s_no_ls_lock);
    if (s_no_ls_lock) {
      (void)esp_pm_lock_acquire(s_no_ls_lock);
    }
  }
#endif
  // All periodic non-LVGL work now runs as task_coordinator subscribers.
  // Display ON base period = 100 ms (the floor across registered subscribers;
  // no point waking faster than the most frequent subscriber needs).
  // Display OFF base period = 1000 ms (one wake/sec, single I2C burst).
  task_coord_init(100, 1000, display_manager_is_on);

  task_coord_subscribe("inactivity_check", inactivity_check_cb, NULL,
                       /*on*/  100,  // 100 ms granularity is fine vs 30 s display timeout
                       /*off*/ 0);   // skip when display already off

  task_coord_subscribe("wake_button",      wake_button_cb,      NULL,
                       /*on*/  0,    // skip when display already on (ui.c owns the press)
                       /*off*/ 1000);// 1 Hz wake poll while display off (floor for snappy wake)

  task_coord_subscribe("axp_state",        (task_coord_cb_t)bsp_power_refresh_state, NULL,
                       /*on*/  3000, // 3 s — charge/VBUS state changes are slow
                       /*off*/ 5000);// 5 s while off

  task_coord_subscribe("rtc_minute_sync",  (task_coord_cb_t)rtc_minute_sync, NULL,
                       /*on*/  60000,// 1/min: PCF85063A → settimeofday → NVS write
                       /*off*/ 0);   // skip — display-off NVS save is done by rtc_suspend()

  task_coord_subscribe("touch_idle",       touch_idle_check_cb, NULL,
                       /*on*/  1000, // check once per second when display on
                       /*off*/ 0);   // IC is in Sleep mode when display off; skip

#if CONFIG_PM_PROFILING
  // Compile-out-able diagnostic. 30 s cadence in both states.
  task_coord_subscribe("pm_profile_dump", pm_profile_dump_cb, NULL,
                       /*on*/  30000,
                       /*off*/ 30000);
#endif
  // Start the task AFTER all subscribes so the coordinator never runs a
  // partial subscriber list on the other core (ST2 fix).
  task_coord_start();
}

void display_manager_pm_early_init(void) {
#if CONFIG_PM_ENABLE
  // Same one-shot-acquire pattern as display_manager_init: only acquire when
  // newly creating the lock so multiple init paths don't double-count.
  if (!s_no_ls_lock) {
    (void)esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "display",
                             &s_no_ls_lock);
    if (s_no_ls_lock) {
      (void)esp_pm_lock_acquire(s_no_ls_lock);
    }
  }
#else
  // No power management; nothing to do
  (void)0;
#endif
}
