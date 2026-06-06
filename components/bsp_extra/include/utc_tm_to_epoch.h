#pragma once
// Pure-C helper, no ESP-IDF deps — included by both the firmware and the
// host unit tests (see tests/).
//
// Convert a UTC struct tm to a time_t epoch without going through mktime
// (which interprets the input as local time). newlib's <time.h> in the
// ESP-IDF build doesn't expose timegm, so we use the well-known
// days_from_civil arithmetic (Howard Hinnant).

#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline time_t utc_tm_to_epoch(const struct tm *t) {
    int y = t->tm_year + 1900;
    unsigned m = t->tm_mon + 1;
    unsigned d = t->tm_mday;
    y -= (m <= 2);
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = (int64_t)era * 146097 + (int64_t)doe - 719468;
    return (time_t)(days * 86400 + t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec);
}

#ifdef __cplusplus
}
#endif
