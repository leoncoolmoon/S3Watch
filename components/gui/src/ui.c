// UI orchestration: bring up screen-manager, build the tileview, start the
// back-button + power-events plumbing, then hand control to the LVGL task.
// Per-concern code lives in ui_screens.c, ui_tileview.c, ui_back_button.c,
// ui_power_events.c.

#include "ui.h"
#include "ui_screens.h"
#include "ui_tileview.h"
#include "ui_back_button.h"
#include "ui_power_events.h"

#include "bsp/esp-bsp.h"
#include "bsp/esp32_s3_touch_amoled_2_06.h"
#include "display_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include "lvgl_spiffs_fs.h"
#include "watchface.h"

static const char* TAG = "UI";

static void create_main_screen(void) {
  swatch_tileview();
  vTaskDelay(pdMS_TO_TICKS(100));
  // Deliberately NOT loaded here: the boot splash stays the active screen
  // while the tileview is built behind it. boot_manager performs the
  // splash→watchface handoff (boot_splash_handoff) once the boot tone and
  // this build have both finished.
}

void ui_init(void) {
  bsp_display_lock(0);

  init_theme();

  // Register LVGL FS driver for SPIFFS before any file-based widgets
  lvgl_spiffs_fs_register();

  create_main_screen();

  // Seed the watchface battery indicator before any event arrives.
  {
    bool vbus = bsp_power_is_vbus_in();
    bool chg  = bsp_power_is_charging();
    int  pct  = bsp_power_get_battery_percent();
    watchface_set_power_state(vbus, chg, pct);
  }

  bsp_display_unlock();
}

void ui_task(void* pvParameters) {
  // Optional ready semaphore from boot_manager — given once the tileview is
  // built and all UI plumbing is live, so the splash handoff can proceed.
  SemaphoreHandle_t ready = (SemaphoreHandle_t)pvParameters;
  ESP_LOGI(TAG, "UI task started");

  ui_init();
  display_manager_init();

  ui_power_events_start();
  ui_back_button_start();

  if (ready) xSemaphoreGive(ready);
  vTaskDelete(NULL);
}
