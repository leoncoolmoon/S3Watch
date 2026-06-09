#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"
#include "display_manager.h"
#include "power_manager.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "lvgl.h"
#include "sensors.h"
#include "settings.h"
#include "ui.h"
#include "esp_sleep.h"
#include "audio_alert.h"
#include "audio_manager.h"
#include "nvs_flash.h"
#include "wifi_manager.h"
#include "ntp_sync.h"
#include "signalk_client.h"
#include "sd_logger.h"

static const char *TAG = "MAIN";

extern "C" void app_main(void) {

  // esp_log_level_set("lcd_panel.io.spi", ESP_LOG_DEBUG);

  // NVS must be initialised before any component (RTC, BLE) uses it
  esp_err_t nvs_err = nvs_flash_init();
  if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS partition damaged, erasing...");
    ESP_ERROR_CHECK(nvs_flash_erase());
    nvs_err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(nvs_err);

  // Create default event loop for component event handlers
  esp_event_loop_create_default();

  // Initialize I2C bus first so AXP2101 can be configured before the BSP
  // tries to talk to the FT5x06 touch controller on the same bus.
  // bsp_i2c_init() is idempotent — subsequent calls from bsp_display_start()
  // will no-op via its i2c_initialized guard.
  bsp_i2c_init();
  bsp_extra_init();   // initializes AXP2101, sets bsp_power s_ready = true

  // power_manager_init() does three things needed before display/UI starts:
  //   1. Creates and acquires the NO_LIGHT_SLEEP lock (blocks light-sleep during boot)
  //   2. Calls esp_pm_configure() (activates DFS + tickless idle; lock prevents early sleep)
  //   3. Enables all ALDO rails + 50 ms settle (FT5x06 needs power before bsp_display_start)
  //   4. Initialises task_coordinator and subscribes PMU/wake-button callbacks
  power_manager_init();
  audio_manager_init();

  bsp_display_start();

  settings_init();

  // Reads settings_get_sd_logging_enabled() — must come after settings_init().
  // Mirrors all ESP_LOG output to /sdcard/logs/ for this boot session when enabled.
  sd_logger_init();

  wifi_manager_init();
  ntp_sync_init();
  signalk_client_init();
  // Respect the user's WiFi permission — only attempt a boot-time connect
  // (for NTP sync etc.) when they've allowed it; otherwise release the
  // radio that wifi_manager_init() just started so we boot in the "off"
  // steady state described in the README.
  if (settings_get_wifi_enabled()) {
      // Try saved networks — if one connects, ntp_sync fires automatically,
      // syncs the RTC, then calls wifi_manager_release() to stop the radio.
      wifi_manager_auto_connect();
  } else {
      wifi_manager_release();
  }

  // UI task calls display_manager_init() after creating the screen.

  // QMI8658 is wired to always-on VCC3V3 and powers up active (~270 µA).
  // Drop it into Power-Down (~6 µA) — see sensors_low_power_idle().
  sensors_low_power_idle();

  // Run the UI at a slightly higher priority so LVGL remains responsive
  xTaskCreate(ui_task, "ui", 8000, NULL, 4, NULL);

  // Play a subtle startup tone once the system is up
  audio_alert_play_startup();
}
