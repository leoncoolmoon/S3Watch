// Exercise parse_delta via the signalk_test_inject_delta hook.
// Verifies that:
//   - a scalar delta lands in s_values (via signalk_client_get)
//   - a notification object lands in s_alerts (state + message + path)
//   - a notification null value clears the slot
//   - a notification with state "normal" clears the slot
//   - a notification with unknown state is treated as inactive

#include <stdbool.h>
#include <string.h>
#include "ondev_test_expect.h"
#include "signalk_client.h"

static void run(int *fails) {
    // Clean slate
    signalk_test_clear_values();
    signalk_alerts_clear();

    // 1. Scalar value → value cache
    const char d1[] =
        "{\"updates\":[{\"values\":[{\"path\":\"navigation.speedOverGround\",\"value\":5.14}]}]}";
    signalk_test_inject_delta(d1, (int)sizeof(d1) - 1);

    signalk_value_t v = {0};
    EXPECT_TRUE(signalk_client_get(SIGNALK_PATH_SOG, &v));
    EXPECT_TRUE(v.valid);
    EXPECT_TRUE(v.value > 5.13 && v.value < 5.15);

    // 2. Notification object → alert cache
    const char d2[] =
        "{\"updates\":[{\"values\":[{"
        "\"path\":\"notifications.environment.depth.belowKeel\","
        "\"value\":{\"state\":\"alarm\",\"message\":\"Shallow water!\"}"
        "}]}]}";
    signalk_test_inject_delta(d2, (int)sizeof(d2) - 1);

    EXPECT_EQ(signalk_alerts_count(), 1);
    signalk_alert_t a = {0};
    EXPECT_TRUE(signalk_alerts_get(0, &a));
    EXPECT_EQ(a.state, SIGNALK_ALERT_ALARM);
    EXPECT_STREQ(a.path,    "notifications.environment.depth.belowKeel");
    EXPECT_STREQ(a.message, "Shallow water!");

    // 3. Null value clears the slot
    const char d3[] =
        "{\"updates\":[{\"values\":[{"
        "\"path\":\"notifications.environment.depth.belowKeel\","
        "\"value\":null"
        "}]}]}";
    signalk_test_inject_delta(d3, (int)sizeof(d3) - 1);
    EXPECT_EQ(signalk_alerts_count(), 0);

    // 4. state "normal" clears the slot
    const char d4_set[] =
        "{\"updates\":[{\"values\":[{"
        "\"path\":\"notifications.test.foo\","
        "\"value\":{\"state\":\"warn\",\"message\":\"foo\"}"
        "}]}]}";
    signalk_test_inject_delta(d4_set, (int)sizeof(d4_set) - 1);
    EXPECT_EQ(signalk_alerts_count(), 1);

    const char d4_clear[] =
        "{\"updates\":[{\"values\":[{"
        "\"path\":\"notifications.test.foo\","
        "\"value\":{\"state\":\"normal\"}"
        "}]}]}";
    signalk_test_inject_delta(d4_clear, (int)sizeof(d4_clear) - 1);
    EXPECT_EQ(signalk_alerts_count(), 0);

    // 5. Unknown state → treated as NORMAL → no alert
    const char d5[] =
        "{\"updates\":[{\"values\":[{"
        "\"path\":\"notifications.test.bar\","
        "\"value\":{\"state\":\"banana\",\"message\":\"x\"}"
        "}]}]}";
    signalk_test_inject_delta(d5, (int)sizeof(d5) - 1);
    EXPECT_EQ(signalk_alerts_count(), 0);

    // Clean up
    signalk_test_clear_values();
    signalk_alerts_clear();
}

bool sw_suite_signalk_delta(char *detail, size_t n) {
    (void)detail; (void)n;
    int fails = 0;
    run(&fails);
    return fails == 0;
}
