// Host tests for signalk_dashboard_fmt.c formatters.
// Exercises all four formatting functions: heading, depth, SOG, wind.

#include "test.h"
#include "signalk_dashboard_fmt.h"
#include <string.h>

#define PI 3.14159265358979323846

static signalk_value_t mk(double v)
{
    signalk_value_t sv;
    sv.valid      = true;
    sv.updated_ms = 0;
    sv.value      = v;
    return sv;
}

// ── fmt_heading_to ────────────────────────────────────────────────────────

static void test_heading_stale(int *fails) {
    char buf[16];
    signalk_value_t sv = mk(0.0);
    fmt_heading_to(buf, sizeof(buf), &sv, false);
    EXPECT_STREQ(buf, "—");
}

static void test_heading_null(int *fails) {
    char buf[16];
    fmt_heading_to(buf, sizeof(buf), NULL, true);
    EXPECT_STREQ(buf, "—");
}

static void test_heading_zero(int *fails) {
    char buf[16];
    signalk_value_t sv = mk(0.0);
    fmt_heading_to(buf, sizeof(buf), &sv, true);
    EXPECT_STREQ(buf, "000°");
}

static void test_heading_180(int *fails) {
    char buf[16];
    signalk_value_t sv = mk(PI);
    fmt_heading_to(buf, sizeof(buf), &sv, true);
    EXPECT_STREQ(buf, "180°");
}

static void test_heading_wraps_over_360(int *fails) {
    char buf[16];
    // 4.5 * PI rad = 810° → wraps to 90°, well away from rounding boundaries
    signalk_value_t sv = mk(4.5 * PI);
    fmt_heading_to(buf, sizeof(buf), &sv, true);
    EXPECT_STREQ(buf, "090°");
}

static void test_heading_negative(int *fails) {
    char buf[16];
    signalk_value_t sv = mk(-PI / 2.0);  // -90° → 270°
    fmt_heading_to(buf, sizeof(buf), &sv, true);
    EXPECT_STREQ(buf, "270°");
}

// ── fmt_depth_ft_to ───────────────────────────────────────────────────────

static void test_depth_stale(int *fails) {
    char buf[16];
    signalk_value_t sv = mk(10.0);
    fmt_depth_ft_to(buf, sizeof(buf), &sv, false);
    EXPECT_STREQ(buf, "—");
}

static void test_depth_null(int *fails) {
    char buf[16];
    fmt_depth_ft_to(buf, sizeof(buf), NULL, true);
    EXPECT_STREQ(buf, "—");
}

static void test_depth_zero(int *fails) {
    char buf[16];
    signalk_value_t sv = mk(0.0);
    fmt_depth_ft_to(buf, sizeof(buf), &sv, true);
    EXPECT_STREQ(buf, "0.0 ft");
}

static void test_depth_1m(int *fails) {
    char buf[16];
    // 1.0 m * 3.280840 = 3.28084 → "3.3 ft"
    signalk_value_t sv = mk(1.0);
    fmt_depth_ft_to(buf, sizeof(buf), &sv, true);
    EXPECT_STREQ(buf, "3.3 ft");
}

// ── fmt_sog_kt_to ─────────────────────────────────────────────────────────

static void test_sog_stale(int *fails) {
    char buf[16];
    signalk_value_t sv = mk(5.0);
    fmt_sog_kt_to(buf, sizeof(buf), &sv, false);
    EXPECT_STREQ(buf, "—");
}

static void test_sog_null(int *fails) {
    char buf[16];
    fmt_sog_kt_to(buf, sizeof(buf), NULL, true);
    EXPECT_STREQ(buf, "—");
}

static void test_sog_zero(int *fails) {
    char buf[16];
    signalk_value_t sv = mk(0.0);
    fmt_sog_kt_to(buf, sizeof(buf), &sv, true);
    EXPECT_STREQ(buf, "0.0 kt");
}

static void test_sog_1ms(int *fails) {
    char buf[16];
    // 1.0 m/s * 1.943844 = 1.943844 → "1.9 kt"
    signalk_value_t sv = mk(1.0);
    fmt_sog_kt_to(buf, sizeof(buf), &sv, true);
    EXPECT_STREQ(buf, "1.9 kt");
}

// ── fmt_wind_to ───────────────────────────────────────────────────────────

static void test_wind_both_stale(int *fails) {
    char buf[32];
    signalk_value_t ang = mk(0.0), spd = mk(0.0);
    fmt_wind_to(buf, sizeof(buf), &ang, false, &spd, false);
    EXPECT_STREQ(buf, "—");
}

static void test_wind_port(int *fails) {
    char buf[32];
    signalk_value_t ang = mk(-PI / 4.0);  // -45° → port
    signalk_value_t spd = mk(0.0);
    fmt_wind_to(buf, sizeof(buf), &ang, true, &spd, true);
    EXPECT_STREQ(buf, "45° P / 0 kt");
}

static void test_wind_starboard(int *fails) {
    char buf[32];
    signalk_value_t ang = mk(PI / 4.0);   // 45° → starboard
    signalk_value_t spd = mk(0.0);
    fmt_wind_to(buf, sizeof(buf), &ang, true, &spd, true);
    EXPECT_STREQ(buf, "45° S / 0 kt");
}

static void test_wind_zero_angle(int *fails) {
    char buf[32];
    signalk_value_t ang = mk(0.0);
    signalk_value_t spd = mk(0.0);
    fmt_wind_to(buf, sizeof(buf), &ang, true, &spd, true);
    EXPECT_STREQ(buf, "0° / 0 kt");
}

static void test_wind_180(int *fails) {
    char buf[32];
    signalk_value_t ang = mk(PI);  // 180°
    signalk_value_t spd = mk(0.0);
    fmt_wind_to(buf, sizeof(buf), &ang, true, &spd, true);
    EXPECT_STREQ(buf, "180° / 0 kt");
}

static void test_wind_only_speed_fresh(int *fails) {
    char buf[32];
    signalk_value_t ang = mk(PI / 4.0), spd = mk(0.0);
    fmt_wind_to(buf, sizeof(buf), &ang, false, &spd, true);
    EXPECT_STREQ(buf, "— / 0 kt");
}

static void test_wind_only_angle_fresh(int *fails) {
    char buf[32];
    signalk_value_t ang = mk(PI / 4.0), spd = mk(0.0);
    fmt_wind_to(buf, sizeof(buf), &ang, true, &spd, false);
    EXPECT_STREQ(buf, "45° S / —");
}

// ── Suite entry point ─────────────────────────────────────────────────────

void run_signalk_fmt_tests(int *fails)
{
    test_heading_stale(fails);
    test_heading_null(fails);
    test_heading_zero(fails);
    test_heading_180(fails);
    test_heading_wraps_over_360(fails);
    test_heading_negative(fails);

    test_depth_stale(fails);
    test_depth_null(fails);
    test_depth_zero(fails);
    test_depth_1m(fails);

    test_sog_stale(fails);
    test_sog_null(fails);
    test_sog_zero(fails);
    test_sog_1ms(fails);

    test_wind_both_stale(fails);
    test_wind_port(fails);
    test_wind_starboard(fails);
    test_wind_zero_angle(fails);
    test_wind_180(fails);
    test_wind_only_speed_fresh(fails);
    test_wind_only_angle_fresh(fails);
}
