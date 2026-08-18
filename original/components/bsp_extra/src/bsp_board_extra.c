#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_event.h"

#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"
#include "pcf85063a.h"

static const char *TAG = "bsp_extra_board";

static i2c_master_bus_handle_t bus_handle;

static i2c_master_dev_handle_t rtc_dev_handle = NULL;
static i2c_master_dev_handle_t touch_dev_handle = NULL;

#define FT3168_I2C_ADDR     0x38
#define FT3168_REG_PMODE    0xA5

esp_err_t bsp_rtc_init(void)
{
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x51,
        .scl_speed_hz = CONFIG_I2C_MASTER_FREQUENCY,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = 0
        }
    };

    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus_handle, &dev_config, &rtc_dev_handle),
                        TAG, "RTC I2C device add");

    return ESP_OK;
}

esp_err_t rtc_register_read(uint8_t regAddr, uint8_t *data, uint8_t len) {
    esp_err_t ret = i2c_master_transmit_receive(rtc_dev_handle, &regAddr, 1, data, len, I2C_MASTER_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RTC READ FAILED: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t rtc_register_write(uint8_t regAddr, uint8_t *data, uint8_t len) {
    // PCF85063A's largest burst write is the 7-byte time-registers block
    // (pcf85063a_set_time) — reject anything bigger rather than trusting an
    // unbounded uint8_t into a stack buffer, and skip the malloc/free pair
    // entirely; nothing this part needs ever exceeds a handful of bytes.
    if (len > 7) return ESP_ERR_INVALID_ARG;

    uint8_t buffer[8];
    buffer[0] = regAddr;
    memcpy(&buffer[1], data, len);

    esp_err_t ret = i2c_master_transmit(rtc_dev_handle, buffer, len + 1, I2C_MASTER_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RTC WRITE FAILED: %s", esp_err_to_name(ret));
    }
    return ret;
}

// Lazy check-then-create on a static handle — only safe because every caller
// of bsp_extra_touch_set_mode (display_manager's turn_on/turn_off and
// touch_idle_check_cb) runs as a task_coordinator subscriber on the same
// task. A caller from a different task would race on touch_dev_handle and
// would need a lock.
static esp_err_t bsp_touch_dev_init(void)
{
    if (touch_dev_handle) return ESP_OK;
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = FT3168_I2C_ADDR,
        .scl_speed_hz    = CONFIG_I2C_MASTER_FREQUENCY,
        .scl_wait_us     = 0,
        .flags           = { .disable_ack_check = 0 },
    };
    return i2c_master_bus_add_device(bus_handle, &dev_config, &touch_dev_handle);
}

esp_err_t bsp_extra_touch_set_mode(uint8_t mode)
{
    if (!touch_dev_handle) {
        esp_err_t err = bsp_touch_dev_init();
        if (err != ESP_OK) return err;
    }
    uint8_t buf[2] = { FT3168_REG_PMODE, mode };
    // Short timeout — if the touch IC's I2C state machine is in the Monitor
    // "won't respond" window (datasheet sec 2.3), this will return error and
    // the next attempt (after the IC clears its state) will succeed. Don't
    // treat failure as fatal.
    return i2c_master_transmit(touch_dev_handle, buf, sizeof(buf), 20);
}

void bsp_extra_i2c_recover(void)
{
    if (bus_handle) {
        (void)i2c_master_bus_reset(bus_handle);
    }
}

esp_err_t bsp_extra_init(void)
{
    esp_err_t ret;

    // Ensure default event loop exists for cross-component events
    (void)esp_event_loop_create_default();

    bus_handle = bsp_i2c_get_handle();
    
    ret = bsp_rtc_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RTC init failed");
        return ret;
    }
    // pcf85063a_init() is called by rtc_start() via settings_init(); don't
    // call it here or the RTC is configured twice on every boot.

    ret = bsp_power_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Power init failed");
    }

    return ESP_OK;
}
