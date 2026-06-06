#pragma once

#include <sys/cdefs.h>
#include <stdbool.h>
#include "esp_codec_dev.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"


#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bsp_extra_init(void);
void bsp_extra_i2c_recover(void);

// FT3168 touch controller power-mode control. Modes:
//   0x00 = Active  (~1.5 mA, normal 60 Hz touch scan, default after power-up)
//   0x01 = Monitor (~30 µA, lower-rate scan that detects touch presence;
//                  chip auto-transitions back to Active when a touch is seen)
//   0x03 = Sleep   (~10 µA, requires RESETB pulse to exit — fragile here)
// Use Monitor mode during display-on idle to save ~1.5 mA on the touch rail.
// Safe to call from any task; uses the shared I2C bus through the existing
// master driver, which serializes concurrent access.
esp_err_t bsp_extra_touch_set_mode(uint8_t mode);

#ifdef __cplusplus
}
#endif
