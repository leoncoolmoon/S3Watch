#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include "rtc_lib.h"

bool hw_test_rtc(char *detail, size_t n) {
    struct tm t = {0};
    esp_err_t err = rtc_get_time(&t);
    if (err != ESP_OK) {
        snprintf(detail, n, "rtc_get_time err=0x%x", err);
        return false;
    }
    snprintf(detail, n, "UTC %04d-%02d-%02d %02d:%02d:%02d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);
    return t.tm_year >= 125;   // ≥ 2025
}
