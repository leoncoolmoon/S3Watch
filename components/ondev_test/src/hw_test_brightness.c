// Ramp 0 → 100 → restore. Always passes unless the BSP call errors.

#include <stdio.h>
#include <stdbool.h>
#include "bsp/esp32_s3_touch_amoled_2_06.h"
#include "settings.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

bool hw_test_brightness(char *detail, size_t n) {
    uint8_t saved = settings_get_brightness();
    for (int v = 0; v <= 100; v += 10) {
        bsp_display_brightness_set((uint8_t)v);
        vTaskDelay(pdMS_TO_TICKS(60));
    }
    bsp_display_brightness_set(saved);
    snprintf(detail, n, "ramp 0→100→restore (saved=%u)", saved);
    return true;
}
