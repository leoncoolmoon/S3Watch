#include "rtc_lib.h"
#include "pcf85063a.h"
#include "utc_tm_to_epoch.h"
#include <time.h>
#include <sys/time.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "power_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static struct tm current_time;
static portMUX_TYPE s_time_mux = portMUX_INITIALIZER_UNLOCKED;

static const char *weekdays[]      = {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
static const char *weekdaysshort[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
static const char *months[]        = {"January","February","March","April","May","June","July","August","September","October","November","December"};

#define NVS_NS  "rtc"
#define NVS_KEY "timestamp"
// Minimum plausible timestamp: 2025-01-01 00:00:00 UTC
#define MIN_VALID_TS ((int64_t)1735689600)

static void nvs_save_time(const struct tm *t)
{
    time_t ts = utc_tm_to_epoch(t);
    if (ts < (time_t)MIN_VALID_TS) return;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    esp_err_t err = nvs_set_i64(h, NVS_KEY, (int64_t)ts);
    if (err != ESP_OK) {
        ESP_LOGW("rtc_lib", "NVS set failed: %s", esp_err_to_name(err));
        nvs_close(h);
        return;
    }
    if (nvs_commit(h) != ESP_OK) {
        ESP_LOGW("rtc_lib", "NVS commit failed");
    }
    nvs_close(h);
}

static bool nvs_load_time(struct tm *out)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    int64_t ts = 0;
    esp_err_t err = nvs_get_i64(h, NVS_KEY, &ts);
    nvs_close(h);
    if (err != ESP_OK || ts < MIN_VALID_TS) return false;
    time_t t_val = (time_t)ts;
    if (!gmtime_r(&t_val, out)) return false;
    return true;
}

static bool time_is_valid(const struct tm *t)
{
    // tm_year is years since 1900; accept 2025 (125) onward
    return t->tm_year >= 125;
}

// Align the ESP32 internal monotonic time (settimeofday) to a UTC struct tm.
static void apply_to_system_time(const struct tm *utc_tm)
{
    time_t ts = utc_tm_to_epoch(utc_tm);
    if (ts < (time_t)MIN_VALID_TS) return;
    struct timeval tv = { .tv_sec = ts, .tv_usec = 0 };
    settimeofday(&tv, NULL);
}

// Read PCF85063A, fall back to NVS if invalid (post power-loss recovery),
// cache the result in current_time, and sync ESP32 internal time to it.
static void rtc_sync_from_hw(void)
{
    struct tm tmp;
    pcf85063a_get_time(&tmp);
    if (!time_is_valid(&tmp)) {
        struct tm saved;
        if (nvs_load_time(&saved)) {
            pcf85063a_set_time(&saved);
            tmp = saved;
        }
    }
    apply_to_system_time(&tmp);
    portENTER_CRITICAL(&s_time_mux);
    current_time = tmp;
    portEXIT_CRITICAL(&s_time_mux);
}

static void rtc_pm_cb(pm_event_t evt, void *ctx)
{
    (void)ctx;
    if (evt == PM_EVT_PREPARE_SLEEP) rtc_suspend();
    else if (evt == PM_EVT_WOKE_UP)  rtc_resume();
}

esp_err_t rtc_start(void)
{
    esp_err_t ret = pcf85063a_init();
    if (ret != ESP_OK) return ret;

    // Initial sync from hardware → ESP32 internal time.
    rtc_sync_from_hw();

    // Register power transitions: checkpoint time on sleep, resync on wake.
    power_manager_add_listener(rtc_pm_cb, NULL);
    return ESP_OK;
}

esp_err_t rtc_get_time(struct tm *utc_time)
{
    if (!utc_time) return ESP_ERR_INVALID_ARG;
    // Always return a fresh UTC snapshot from the system clock so callers get
    // a stable contract regardless of which path last touched current_time
    // (rtc_sync_from_hw stores UTC; rtc_refresh_now stores local).
    time_t now = time(NULL);
    if (now <= 0) return ESP_FAIL;
    if (!gmtime_r(&now, utc_time)) return ESP_FAIL;
    return ESP_OK;
}

// Write a UTC struct tm to the PCF85063A, mirror to ESP32 internal time, and
// checkpoint to NVS. Callers with a local-time struct tm should use
// rtc_set_time_from_local() instead — it does the local→UTC conversion.
esp_err_t rtc_set_time(const struct tm *utc_time)
{
    esp_err_t ret = pcf85063a_set_time(utc_time);
    if (ret == ESP_OK) {
        portENTER_CRITICAL(&s_time_mux);
        current_time = *utc_time;
        portEXIT_CRITICAL(&s_time_mux);
        apply_to_system_time(utc_time);
        nvs_save_time(utc_time);
    }
    return ret;
}

esp_err_t rtc_set_time_from_local(const struct tm *local_time)
{
    if (!local_time) return ESP_ERR_INVALID_ARG;
    struct tm tmp = *local_time;
    // Interpret as local under current TZ → UTC epoch.
    time_t epoch = mktime(&tmp);
    if (epoch < (time_t)MIN_VALID_TS) return ESP_ERR_INVALID_ARG;
    // Convert back to UTC struct tm and store via the UTC path.
    struct tm utc_tm;
    if (!gmtime_r(&epoch, &utc_tm)) return ESP_FAIL;
    return rtc_set_time(&utc_tm);
}

void rtc_suspend(void)
{
    // Flush current time to NVS before the I2C bus loses power (display sleep
    // cuts ALDOs that may carry the bus).
    if (time_is_valid(&current_time)) {
        nvs_save_time(&current_time);
    }
}

void rtc_resume(void)
{
    // ALDOs are back up by now, but give the PCF85063A a moment to stabilise.
    vTaskDelay(pdMS_TO_TICKS(30));
    // Re-sync ESP32 internal time from the hardware RTC. This corrects any
    // drift accumulated during the sleep window.
    rtc_sync_from_hw();
}

// Fast, no-I2C refresh of the cached struct tm from the ESP32 internal clock.
// Called by the watchface 1 s LVGL timer so the displayed time is always
// up-to-date without polling the PCF85063A.
void rtc_refresh_now(void)
{
    time_t now = time(NULL);
    if (now < (time_t)MIN_VALID_TS) return;
    struct tm lt;
    if (localtime_r(&now, &lt)) {
        portENTER_CRITICAL(&s_time_mux);
        current_time = lt;
        portEXIT_CRITICAL(&s_time_mux);
    }
}

// Coordinator subscriber (registered once per boot): once per minute while the
// display is on, re-sync from PCF85063A and write the current time to NVS.
// The NVS write bounds time-loss-on-power-fail to ~1 minute.
void rtc_minute_sync(void)
{
    rtc_sync_from_hw();
    if (time_is_valid(&current_time)) {
        nvs_save_time(&current_time);
    }
}

// Take a consistent snapshot so the LVGL task never reads a half-updated struct.
static inline struct tm rtc_snapshot(void)
{
    portENTER_CRITICAL(&s_time_mux);
    struct tm snap = current_time;
    portEXIT_CRITICAL(&s_time_mux);
    return snap;
}

int rtc_get_hour(void)   { return rtc_snapshot().tm_hour; }
int rtc_get_minute(void) { return rtc_snapshot().tm_min; }
int rtc_get_second(void) { return rtc_snapshot().tm_sec; }
int rtc_get_day(void)    { return rtc_snapshot().tm_mday; }
int rtc_get_month(void)  { return rtc_snapshot().tm_mon + 1; }
int rtc_get_year(void)   { return rtc_snapshot().tm_year + 1900; }

const char *rtc_get_weekday_string(void)
{
    return weekdays[rtc_snapshot().tm_wday];
}
const char *rtc_get_weekday_short_string(void)
{
    return weekdaysshort[rtc_snapshot().tm_wday];
}
const char *rtc_get_month_string(void)
{
    return months[rtc_snapshot().tm_mon];
}
