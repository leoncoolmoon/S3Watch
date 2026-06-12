// boot_manager — staged power-on sequence (moved out of main.cpp so boot has
// one owner and a defined UX): power on → splash + boot tone → watchface.
//
//   Stage 0  core HW       (NVS, event loop, I2C, PMU, codec)
//   Stage 1  display       bsp_display_start + boot splash (visible ~immediately)
//   Stage 2  settings+tone settings_init, then the boot tone starts DURING the
//                          splash (radio is quiet, watchface not yet built —
//                          the tone competes with nothing that stalls flash)
//   Stage 3  services      alarm/sd/wifi/ntp/signalk/imu/steps init while the
//                          splash shows; radio released (first NTP sync comes
//                          later via ntp_sync_check after its boot-quiet gate)
//   Stage 4  UI build      ui_task builds the tileview BEHIND the splash and
//                          signals readiness
//   Stage 5  handoff       wait for UI ready + tone done + a minimum splash
//                          time, then slide the watchface up over the splash
//
// Ordering contracts honored here (see AGENTS.md): every task_coordinator
// subscriber registers before task_coord_start() (which runs inside stage 4's
// display_manager_init); settings_init precedes everything that reads settings;
// step_tracker_init follows settings_init + the imu_manager setup.

#include "boot_manager.h"
#include "boot_splash.h"
#include "ui.h"

#include "bsp/esp-bsp.h"
#include "bsp/esp32_s3_touch_amoled_2_06.h"
#include "bsp_board_extra.h"
#include "power_manager.h"
#include "audio_manager.h"
#include "audio_alert.h"
#include "alarm_manager.h"
#include "settings.h"
#include "sd_manager.h"
#include "sd_logger.h"
#include "wifi_manager.h"
#include "ntp_sync.h"
#include "signalk_client.h"
#include "imu_manager.h"
#include "step_tracker.h"

#include "cJSON.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "BOOT_MGR";

// Minimum time the splash stays up even when the tone is disabled/short, so
// boot doesn't flash a sub-second splash frame.
#define BOOT_SPLASH_MIN_US   (1200LL * 1000)
// Safety cap on waiting for the tone/UI — a wedged tone task must not brick
// boot at the splash. Generous: tone is ~1.5 s, UI build ~1 s.
#define BOOT_HANDOFF_CAP_MS  10000

// Route ALL cJSON allocations to PSRAM. cJSON builds its parse tree from many
// tiny mallocs (one per node + key/value strdup); in internal RAM that scales
// badly — the music index parse tree alone can exceed the internal heap on a
// large library, and settings/SD-backup/SignalK parsing all share the same
// allocator. Installed before the first parse (settings_init → settings_load).
static void *cjson_psram_malloc(size_t sz) { return heap_caps_malloc(sz, MALLOC_CAP_SPIRAM); }
static void  cjson_psram_free(void *ptr)   { heap_caps_free(ptr); }

void boot_manager_run(void)
{
    // ── Stage 0: core HW ────────────────────────────────────────────────────
    static cJSON_Hooks cjson_hooks = { cjson_psram_malloc, cjson_psram_free };
    cJSON_InitHooks(&cjson_hooks);

    // NVS must be initialised before any component (RTC, step_tracker) uses it.
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition damaged, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);

    esp_event_loop_create_default();

    // I2C first so the AXP2101 can be configured before the BSP talks to the
    // touch controller on the same bus. bsp_i2c_init() is idempotent.
    bsp_i2c_init();
    bsp_extra_init();      // AXP2101 up, bsp_power ready

    // power_manager_init: NO_LIGHT_SLEEP lock held, esp_pm configured, display
    // rails on (+50 ms settle), task_coordinator initialised (not started).
    power_manager_init();
    audio_manager_init();  // codec bring-up (brackets ALDO3 itself)

    // ── Stage 1: display + splash ───────────────────────────────────────────
    bsp_display_start();
    boot_splash_show();
    int64_t splash_t0 = esp_timer_get_time();

    // ── Stage 2: settings + boot tone ───────────────────────────────────────
    settings_init();       // SPIFFS load, TZ, rtc_start, brightness apply

    SemaphoreHandle_t tone_done = xSemaphoreCreateBinary();
    // Tone plays over the splash, against a quiet radio and an unbuilt UI —
    // the calm window. `tone_done` is always given (sound off / spawn failure
    // included), and NULL just degrades the wait to the minimum splash time.
    audio_alert_play_startup(tone_done);

    // ── Stage 3: services (splash showing, tone playing) ────────────────────
    alarm_manager_init();  // pre-task_coord_start, per its contract
    sd_manager_init();
    sd_logger_init();      // reads settings_get_sd_logging_enabled()

    wifi_manager_init();
    ntp_sync_init();
    signalk_client_init();
    // Radio off for boot regardless of the WiFi permission: the first NTP sync
    // runs via ntp_sync_check() (~30 s in, past ntp_sync's boot-quiet window).
    // Keeps WiFi bring-up + the sync's NVS commit away from tone playback.
    wifi_manager_release();

    // QMI8658C is on always-on VCC3V3 and boots active (~270 µA): apply the
    // saved step-counter setting (off → ~6 µA Power-Down), then start the
    // stats layer (seeds from settings.json, registers the imu step callback).
    imu_manager_set_step_counting(settings_get_step_counter_enabled());
    step_tracker_init();

    // ── Stage 4: UI build behind the splash ─────────────────────────────────
    SemaphoreHandle_t ui_ready = xSemaphoreCreateBinary();
    xTaskCreate(ui_task, "ui", 8000, (void *)ui_ready, 4, NULL);

    // ── Stage 5: handoff ────────────────────────────────────────────────────
    if (ui_ready) {
        if (xSemaphoreTake(ui_ready, pdMS_TO_TICKS(BOOT_HANDOFF_CAP_MS)) != pdTRUE) {
            ESP_LOGE(TAG, "UI build did not signal ready within %d ms", BOOT_HANDOFF_CAP_MS);
        }
        vSemaphoreDelete(ui_ready);
    }
    if (tone_done) {
        if (xSemaphoreTake(tone_done, pdMS_TO_TICKS(BOOT_HANDOFF_CAP_MS)) != pdTRUE) {
            ESP_LOGW(TAG, "boot tone did not finish within %d ms — handing off anyway",
                     BOOT_HANDOFF_CAP_MS);
        }
        vSemaphoreDelete(tone_done);
    }
    int64_t elapsed_us = esp_timer_get_time() - splash_t0;
    if (elapsed_us < BOOT_SPLASH_MIN_US) {
        vTaskDelay(pdMS_TO_TICKS((BOOT_SPLASH_MIN_US - elapsed_us) / 1000));
    }

    boot_splash_handoff();
    ESP_LOGI(TAG, "boot complete — handed off to normal operation");
}
