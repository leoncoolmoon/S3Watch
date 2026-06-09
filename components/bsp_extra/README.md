# bsp_extra

Board-level extras that sit above the BSP but below the application components: AXP2101 PMU init, I2C bus recovery, FT3168 touch controller power modes, and PCF85063A RTC access.

## Components

### AXP2101 / PMU (`bsp_extra_init`)
Initialises the AXP2101 power management IC. Must be called before `power_manager_init()` since power_manager relies on `bsp_power_*` functions being ready.

### I2C recovery (`bsp_extra_i2c_recover`)
Issues a bus-reset sequence on the shared I2C bus after display ALDO rails are restored on wake. The panel / touch controller can leave SCL/SDA stuck after a power cycle; recovery unblocks the bus before the next I2C transaction.

### FT3168 touch controller (`bsp_extra_touch_set_mode`)
Controls the operating mode of the FT3168 capacitive touch IC:

| Mode | Code | Current | Notes |
|------|------|---------|-------|
| Active | 0x00 | ~1.5 mA | Normal 60 Hz scan |
| Monitor | 0x01 | ~30 µA | Lower-rate; auto-exits to Active on touch |
| Sleep | 0x03 | ~10 µA | Requires RESETB pulse to exit — not used |

`display_manager` sets Monitor mode during idle and restores Active on wake. Sleep mode is intentionally avoided because RESETB is shared with the LCD reset line.

### PCF85063A RTC (`rtc_lib.h`)
UTC-based RTC access. `rtc_start()` loads time from the PCF85063A into `settimeofday()` at boot. `rtc_minute_sync()` checkpoints the internal clock back to the RTC once per minute (wired as a task_coordinator subscriber) to bound time-loss on power failure.

## Dependencies

`esp32_s3_touch_amoled_2_06` (BSP)
