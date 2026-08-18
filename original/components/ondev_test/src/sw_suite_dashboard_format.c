// Exercise the dashboard formatters. Pure: SI in, display string out.

#include <stdbool.h>
#include "ondev_test_expect.h"
#include "signalk_client.h"
#include "signalk_dashboard_fmt.h"

static void run(int *fails) {
    char buf[32];
    signalk_value_t v = {0};

    // Heading: 45° → "045°"
    v.valid = true;
    v.value = 0.7853981634;       // π/4
    fmt_heading_to(buf, sizeof(buf), &v, true);
    EXPECT_STREQ(buf, "045°");

    // Heading wraparound: 6.30 rad ≈ 361° → 1° → "001°"
    v.value = 6.30;
    fmt_heading_to(buf, sizeof(buf), &v, true);
    // Pin a less-strict check: rounded display in [0, 5]°
    EXPECT_TRUE(buf[0] == '0' && (buf[1] == '0' || buf[1] == '1') &&
                buf[2] >= '0' && buf[2] <= '9');

    // Heading stale → "—"
    fmt_heading_to(buf, sizeof(buf), &v, false);
    EXPECT_STREQ(buf, "—");

    // Depth: 12.3 m → "40.4 ft"
    v.value = 12.3;
    fmt_depth_ft_to(buf, sizeof(buf), &v, true);
    EXPECT_STREQ(buf, "40.4 ft");

    // SOG: 2.778 m/s → "5.4 kt"
    v.value = 2.778;
    fmt_sog_kt_to(buf, sizeof(buf), &v, true);
    EXPECT_STREQ(buf, "5.4 kt");

    // Wind: ang=+45° (starboard), spd=6.17 m/s ≈ 12 kt
    signalk_value_t ang = { .valid = true, .value = 0.7853981634 };
    signalk_value_t spd = { .valid = true, .value = 6.17 };
    fmt_wind_to(buf, sizeof(buf), &ang, true, &spd, true);
    EXPECT_STREQ(buf, "45° S / 12 kt");

    // Wind: ang=-45° (port)
    ang.value = -0.7853981634;
    fmt_wind_to(buf, sizeof(buf), &ang, true, &spd, true);
    EXPECT_STREQ(buf, "45° P / 12 kt");

    // Wind: dead-ahead → "0°"
    ang.value = 0.0;
    fmt_wind_to(buf, sizeof(buf), &ang, true, &spd, true);
    EXPECT_STREQ(buf, "0° / 12 kt");

    // Wind: astern → "180°"
    ang.value = 3.14159265;
    fmt_wind_to(buf, sizeof(buf), &ang, true, &spd, true);
    EXPECT_STREQ(buf, "180° / 12 kt");

    // Wind: both stale → "—"
    fmt_wind_to(buf, sizeof(buf), &ang, false, &spd, false);
    EXPECT_STREQ(buf, "—");
}

bool sw_suite_dashboard_format(char *detail, size_t n) {
    (void)detail; (void)n;
    int fails = 0;
    run(&fails);
    return fails == 0;
}
