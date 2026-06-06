#ifndef __RTC_H__
#define __RTC_H__

#include <time.h>
#include "esp_err.h"

esp_err_t rtc_start(void);

// rtc_get_time / rtc_set_time both operate in UTC. The PCF85063A is the
// canonical UTC source; libc's localtime_r() (with the user's TZ setting
// applied) does the conversion for display.
esp_err_t rtc_get_time(struct tm *utc_time);
esp_err_t rtc_set_time(const struct tm *utc_time);

// Convenience for UI code that has a local struct tm (e.g. roller picker
// values). Interprets the input as local under the current TZ, converts
// to UTC, and writes through to rtc_set_time. Requires setenv("TZ", ...)
// + tzset() to have been called already — settings_init() handles that.
esp_err_t rtc_set_time_from_local(const struct tm *local_time);

void rtc_suspend(void);
void rtc_resume(void);

// Fast refresh of the cached struct tm from the ESP32 internal clock — no I2C.
// Call this from the watchface tick before reading rtc_get_hour() / _minute()
// / _second() so the displayed time is always to-the-second accurate.
void rtc_refresh_now(void);

// Once-per-minute checkpoint: reads PCF85063A → settimeofday → writes NVS.
// Wired up as a task_coordinator subscriber during boot. Bounds the worst-case
// time-loss on power failure to ~1 minute while the display is on.
void rtc_minute_sync(void);

int rtc_get_hour(void);
int rtc_get_minute(void);
int rtc_get_second(void);
int rtc_get_day(void);
int rtc_get_month(void);
int rtc_get_year(void);
const char *rtc_get_weekday_string(void);
const char *rtc_get_weekday_short_string(void);
const char *rtc_get_month_string(void);

#endif /* __RTC_H__ */
