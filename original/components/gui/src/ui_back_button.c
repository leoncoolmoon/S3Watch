// Hardware back-button:
//   - GPIO 0 (physical "back" key on the watch): ISR + task, edge-triggered.
//   - PMU power-key short-press: polled via the task_coordinator at 100 ms.
// Both dispatch ui_handle_back_async() onto the LVGL thread, where it walks
// the dynamic-tile stack and pops one level (settings sub-screen → settings
// menu → watchface).

#include "ui_back_button.h"
#include "ui_tileview.h"
#include "display_manager.h"
#include "task_coordinator.h"
#include "bsp/esp32_s3_touch_amoled_2_06.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char* TAG = "UI_BACK";

#define UI_BACK_BTN GPIO_NUM_0

static TaskHandle_t s_back_btn_task = NULL;

static void ui_handle_back_async(void* user) {
  (void)user;
  ui_tileview_back();
}

// GPIO 0 ISR: fires on each falling edge (button press). Notifies the
// back-button task so it can debounce and dispatch on the LVGL thread.
static void IRAM_ATTR ui_back_btn_isr(void* arg) {
  (void)arg;
  if (!s_back_btn_task) return;
  BaseType_t hp = pdFALSE;
  vTaskNotifyGiveFromISR(s_back_btn_task, &hp);
  portYIELD_FROM_ISR(hp);
}

static void ui_back_btn_task(void* arg) {
  (void)arg;
  // Configure GPIO 0 as input with pull-up and a falling-edge interrupt.
  gpio_config_t io = {
      .pin_bit_mask = 1ULL << UI_BACK_BTN,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_NEGEDGE,
  };
  (void)gpio_config(&io);
  // gpio_install_isr_service may already be installed by another component
  // (e.g. esp_lcd_touch). Ignore ESP_ERR_INVALID_STATE.
  (void)gpio_install_isr_service(0);
  (void)gpio_isr_handler_add(UI_BACK_BTN, ui_back_btn_isr, NULL);

  const TickType_t debounce = pdMS_TO_TICKS(120);
  TickType_t last_press = 0;
  for (;;) {
    // Block forever until ISR fires. No polling, no periodic wakeups.
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    // Ignore presses while the display is off — wake-from-sleep is handled
    // by the task_coordinator wake_button subscriber via the PMU button.
    if (!display_manager_is_on()) continue;
    TickType_t now = xTaskGetTickCount();
    if (now - last_press <= debounce) continue;
    last_press = now;
    // Dispatch back action on LVGL thread.
    lv_async_call(ui_handle_back_async, NULL);
    // Count this as user activity so the dim/off cycle restarts (lv_async_call
    // by itself doesn't bump the LVGL inactivity counter).
    display_manager_reset_timer();
  }
}

// Coordinator subscriber: while display is on, poll the PMU short-press latch
// and dispatch it as a Back action — same effect as a GPIO 0 press, just
// triggered by the other physical button on the watch.
static void ui_pmu_back_cb(void* user) {
  (void)user;
  if (!display_manager_is_on()) return;
  if (bsp_power_poll_pwr_button_short()) {
    lv_async_call(ui_handle_back_async, NULL);
    // Count this as user activity so the dim/off cycle restarts.
    display_manager_reset_timer();
  }
}

void ui_back_button_start(void) {
  ESP_LOGI(TAG, "starting back-button (GPIO 0 ISR + PMU subscriber)");
  // GPIO 0 back-button: ISR-driven, task blocks on a notification.
  // SPIRAM stack: this task does no flash (NVS/SPIFFS) writes — it only posts a
  // Back action onto the LVGL thread — so it's safe off internal RAM. (It can't
  // run while the flash cache is disabled, but it has nothing to do then.)
  if (xTaskCreateWithCaps(ui_back_btn_task, "ui_back_btn", 4096, NULL, 5,
                          &s_back_btn_task,
                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    ESP_LOGE(TAG, "ui_back_btn task create failed");
  }
  // PMU short-press while screen is on dispatches the same Back action.
  task_coord_subscribe("pmu_back_btn", ui_pmu_back_cb, NULL,
                       /*on*/  100, /*off*/ 0);
}
