#include "sd_manager.h"
#include "bsp/esp32_s3_touch_amoled_2_06.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "SD_MGR";

static SemaphoreHandle_t s_mutex;
static uint32_t          s_refcount;

void sd_manager_init(void)
{
    s_mutex    = xSemaphoreCreateMutex();
    s_refcount = 0;
}

esp_err_t sd_manager_acquire(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t ret = ESP_OK;
    if (s_refcount == 0) {
        ret = bsp_sdcard_mount();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "SD mount failed: %s", esp_err_to_name(ret));
        }
    }
    if (ret == ESP_OK) s_refcount++;
    xSemaphoreGive(s_mutex);
    return ret;
}

void sd_manager_release(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_refcount > 0) {
        s_refcount--;
        if (s_refcount == 0) bsp_sdcard_unmount();
    }
    xSemaphoreGive(s_mutex);
}

bool sd_manager_is_mounted(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool m = (s_refcount > 0);
    xSemaphoreGive(s_mutex);
    return m;
}
