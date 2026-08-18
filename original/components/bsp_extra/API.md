# bsp_extra API

## `bsp_board_extra.h`

### `bsp_extra_init`
```c
esp_err_t bsp_extra_init(void);
```
Initialize the AXP2101 PMU. Call once at the start of `app_main()`, before `power_manager_init()`.

---

### `bsp_extra_i2c_recover`
```c
void bsp_extra_i2c_recover(void);
```
Issue a software I2C bus reset to recover SCL/SDA from a stuck state. Called in `display_wake_impl()` after ALDO rails are restored. Safe to call from any task.

---

### `bsp_extra_touch_set_mode`
```c
esp_err_t bsp_extra_touch_set_mode(uint8_t mode);
```
Set the FT3168 touch controller power mode via I2C. Uses the shared I2C master driver (concurrent access is serialised).

| `mode` | Constant | Effect |
|--------|----------|--------|
| `0x00` | `FT3168_MODE_ACTIVE` | Normal operation, ~1.5 mA |
| `0x01` | `FT3168_MODE_MONITOR` | Idle scan, ~30 µA; auto-exits on touch |
| `0x03` | `FT3168_MODE_SLEEP` | Deep sleep, ~10 µA; requires RESETB to exit |

Returns `ESP_OK` on success or an I2C error code.

---

## `rtc_lib.h`

### `rtc_start`
```c
esp_err_t rtc_start(void);
```
Read the PCF85063A hardware RTC and call `settimeofday()` to seed the ESP32 system clock. Call once at boot after I2C is ready.

---

### `rtc_get_time` / `rtc_set_time`
```c
esp_err_t rtc_get_time(struct tm *utc_time);
esp_err_t rtc_set_time(const struct tm *utc_time);
```
Direct UTC read/write to the PCF85063A. Both operate in UTC; `localtime_r()` handles timezone conversion for display.

---

### `rtc_set_time_from_local`
```c
esp_err_t rtc_set_time_from_local(const struct tm *local_time);
```
Convenience for UI roller-picker values. Converts from the current `TZ` to UTC, then calls `rtc_set_time()`. Requires `settings_init()` to have set the TZ env var.

---

### `rtc_refresh_now`
```c
void rtc_refresh_now(void);
```
Refresh the cached `struct tm` from the ESP32 system clock (no I2C). Call before reading `rtc_get_hour()` / `rtc_get_minute()` / `rtc_get_second()` in the watchface tick to get second-accurate time without hitting the I2C bus every frame.

---

### `rtc_minute_sync`
```c
void rtc_minute_sync(void);
```
Read PCF85063A → `settimeofday()` → write NVS backup. Registered as a `task_coordinator` subscriber (once per minute, display-on only). Bounds worst-case time-loss on power failure to ~1 minute.

---

### Accessors
```c
int rtc_get_hour(void);
int rtc_get_minute(void);
int rtc_get_second(void);
int rtc_get_day(void);
int rtc_get_month(void);
int rtc_get_year(void);
const char *rtc_get_weekday_string(void);
const char *rtc_get_weekday_short_string(void);
const char *rtc_get_month_string(void);
```
Return fields from the cached `struct tm`. The cache is always **local civil
time** (every writer stores `localtime_r` of the system clock); the UTC truth
lives in the system clock itself, which is what the NVS checkpoint paths save.
Call `rtc_refresh_now()` first to ensure fresh values.
