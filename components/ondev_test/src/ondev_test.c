// On-device test runner: walks a static registry of {category, name, fn}
// entries, calls each, dispatches the result via the progress callback,
// logs to serial. Used from Settings → Run Tests.

#include "ondev_test.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ONDEV_TEST";

// All test functions return true on PASS, fill `detail` with a status
// string (≤ DETAIL_LEN-1 chars). Suites that use EXPECT_* macros return
// true iff their internal failure counter stayed at 0.
typedef bool (*test_fn_t)(char *detail, size_t n);

typedef struct {
    ondev_test_category_t cat;
    const char *name;
    test_fn_t   fn;
} test_t;

// ── Forward decls ────────────────────────────────────────────────────────

// Software
extern bool sw_suite_utc_tm              (char *detail, size_t n);
extern bool sw_suite_ip_parse            (char *detail, size_t n);
extern bool sw_suite_signalk_alert_parse (char *detail, size_t n);
extern bool sw_suite_signalk_delta       (char *detail, size_t n);
extern bool sw_suite_signalk_alert_cache (char *detail, size_t n);
extern bool sw_suite_dashboard_format    (char *detail, size_t n);
extern bool sw_suite_settings            (char *detail, size_t n);

// Hardware
extern bool hw_test_heap        (char *detail, size_t n);
extern bool hw_test_i2c_scan    (char *detail, size_t n);
extern bool hw_test_axp         (char *detail, size_t n);
extern bool hw_test_rtc         (char *detail, size_t n);
extern bool hw_test_nvs         (char *detail, size_t n);
extern bool hw_test_spiffs      (char *detail, size_t n);
extern bool hw_test_imu         (char *detail, size_t n);
extern bool hw_test_audio       (char *detail, size_t n);
extern bool hw_test_brightness  (char *detail, size_t n);
extern bool hw_test_wifi_scan   (char *detail, size_t n);
extern bool hw_test_coord_stats (char *detail, size_t n);

// ── Registry ─────────────────────────────────────────────────────────────

static const test_t s_tests[] = {
    // Software section
    { ONDEV_TEST_SW, "utc_tm",              sw_suite_utc_tm },
    { ONDEV_TEST_SW, "ip_parse",            sw_suite_ip_parse },
    { ONDEV_TEST_SW, "signalk_alert_parse", sw_suite_signalk_alert_parse },
    { ONDEV_TEST_SW, "signalk_delta",       sw_suite_signalk_delta },
    { ONDEV_TEST_SW, "signalk_alert_cache", sw_suite_signalk_alert_cache },
    { ONDEV_TEST_SW, "dashboard_format",    sw_suite_dashboard_format },
    { ONDEV_TEST_SW, "settings_roundtrip",  sw_suite_settings },

    // Hardware section
    { ONDEV_TEST_HW, "Heap snapshot",       hw_test_heap },
    { ONDEV_TEST_HW, "I2C scan",            hw_test_i2c_scan },
    { ONDEV_TEST_HW, "AXP2101",             hw_test_axp },
    { ONDEV_TEST_HW, "RTC",                 hw_test_rtc },
    { ONDEV_TEST_HW, "NVS round-trip",      hw_test_nvs },
    { ONDEV_TEST_HW, "SPIFFS round-trip",   hw_test_spiffs },
    { ONDEV_TEST_HW, "QMI8658 accel",       hw_test_imu },
    { ONDEV_TEST_HW, "Audio output",        hw_test_audio },
    { ONDEV_TEST_HW, "Display brightness",  hw_test_brightness },
    { ONDEV_TEST_HW, "WiFi scan",           hw_test_wifi_scan },
    { ONDEV_TEST_HW, "task_coord stats",    hw_test_coord_stats },
};
static const int s_count = (int)(sizeof(s_tests) / sizeof(s_tests[0]));

// ── Public API ───────────────────────────────────────────────────────────

int ondev_test_count(void) { return s_count; }

bool ondev_test_info(int index, ondev_test_category_t *cat, const char **name) {
    if (index < 0 || index >= s_count) return false;
    if (cat)  *cat  = s_tests[index].cat;
    if (name) *name = s_tests[index].name;
    return true;
}

int ondev_test_run_all(ondev_test_progress_cb cb, void *user) {
    ESP_LOGI(TAG, "===== Starting test suite (%d tests) =====", s_count);
    int64_t t_start = esp_timer_get_time();
    int failures = 0;

    for (int i = 0; i < s_count; i++) {
        char detail[96] = "";
        int64_t t0 = esp_timer_get_time();
        bool pass = s_tests[i].fn(detail, sizeof(detail));
        int64_t dt_ms = (esp_timer_get_time() - t0) / 1000;
        if (!pass) failures++;
        const char *cat_str = (s_tests[i].cat == ONDEV_TEST_SW) ? "sw" : "hw";
        ESP_LOGI(TAG, "[%2d/%d] %s %s: %s — %s (%lld ms)",
                 i + 1, s_count, cat_str, s_tests[i].name,
                 pass ? "PASS" : "FAIL",
                 detail[0] ? detail : "(no detail)",
                 (long long)dt_ms);
        if (cb) {
            cb(i, s_count, s_tests[i].cat, s_tests[i].name,
               pass ? ONDEV_TEST_PASS : ONDEV_TEST_FAIL, detail, user);
        }
    }

    int64_t total_ms = (esp_timer_get_time() - t_start) / 1000;
    ESP_LOGI(TAG, "===== Done: %d/%d PASS (%lld ms, %d failure%s) =====",
             s_count - failures, s_count, (long long)total_ms,
             failures, failures == 1 ? "" : "s");
    return failures;
}
