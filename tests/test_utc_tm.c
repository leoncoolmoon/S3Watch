// Verify utc_tm_to_epoch against well-known reference values and against
// the host's timegm (the standard libc routine the firmware can't use).

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define _DARWIN_C_SOURCE
#include <stdlib.h>
#include <time.h>

/* timegm is a GNU/macOS extension not present in ESP-IDF newlib */
#if defined(ESP_PLATFORM)
static time_t timegm(struct tm *tm)
{
    static const int mdays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int year = tm->tm_year + 1900;
    int mon  = tm->tm_mon;
    long days = (long)(year - 1970) * 365;
    int y = year - 1;
    days += (y/4 - 492) - (y/100 - 19) + (y/400 - 4);
    for (int i = 0; i < mon; i++) {
        days += mdays[i];
        if (i == 1 && ((year%4==0 && year%100!=0) || year%400==0)) days++;
    }
    days += tm->tm_mday - 1;
    return (time_t)(days * 86400 + tm->tm_hour*3600 + tm->tm_min*60 + tm->tm_sec);
}
#endif
#include "test.h"
#include "utc_tm_to_epoch.h"

static struct tm make_tm(int y, int mon, int d, int h, int mi, int s) {
    struct tm t = (struct tm){0};
    t.tm_year = y - 1900;
    t.tm_mon  = mon - 1;
    t.tm_mday = d;
    t.tm_hour = h;
    t.tm_min  = mi;
    t.tm_sec  = s;
    return t;
}

void run_utc_tm_tests(int *fails) {
    // Unix epoch
    struct tm t = make_tm(1970, 1, 1, 0, 0, 0);
    EXPECT_EQ(utc_tm_to_epoch(&t), 0);

    // Y2K
    t = make_tm(2000, 1, 1, 0, 0, 0);
    EXPECT_EQ(utc_tm_to_epoch(&t), 946684800);

    // Known fixed reference: 2025-01-01 00:00:00 UTC = 1735689600
    // (this constant is also the firmware's MIN_VALID_TS).
    t = make_tm(2025, 1, 1, 0, 0, 0);
    EXPECT_EQ(utc_tm_to_epoch(&t), 1735689600);

    // Leap-day handling: 2024-02-29 12:00:00 UTC
    t = make_tm(2024, 2, 29, 12, 0, 0);
    EXPECT_EQ(utc_tm_to_epoch(&t), 1709208000);

    // Cross-check a spread of dates against host's timegm (POSIX, but
    // available on glibc/macOS) — catches any arithmetic drift over a
    // wide range of months and years.
    setenv("TZ", "UTC0", 1);
    tzset();
    int years[]  = {1970, 1999, 2000, 2001, 2024, 2025, 2038, 2099};
    int months[] = {1, 3, 6, 9, 12};
    int days[]   = {1, 15, 28};
    int n_y = sizeof(years)/sizeof(*years);
    int n_m = sizeof(months)/sizeof(*months);
    int n_d = sizeof(days)/sizeof(*days);
    for (int yi = 0; yi < n_y; yi++) {
        for (int mi = 0; mi < n_m; mi++) {
            for (int di = 0; di < n_d; di++) {
                struct tm a = make_tm(years[yi], months[mi], days[di], 6, 30, 15);
                struct tm b = a;
                time_t want = timegm(&b);
                time_t got  = utc_tm_to_epoch(&a);
                if (got != want) {
                    FAILF("year=%d mon=%d day=%d: got %lld, want %lld",
                          years[yi], months[mi], days[di],
                          (long long)got, (long long)want);
                }
            }
        }
    }
}
