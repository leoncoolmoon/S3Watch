// Exercise the alert cache's severity-desc sort and eviction policy.

#include <stdbool.h>
#include <stdio.h>
#include "ondev_test_expect.h"
#include "signalk_client.h"

static void inject(const char *path, const char *state) {
    char buf[256];
    int len = snprintf(buf, sizeof(buf),
        "{\"updates\":[{\"values\":[{"
        "\"path\":\"%s\","
        "\"value\":{\"state\":\"%s\",\"message\":\"x\"}"
        "}]}]}", path, state);
    signalk_test_inject_delta(buf, len);
}

static void run(int *fails) {
    signalk_alerts_clear();

    // Sort: inject 4 mixed-severity alerts, verify get() returns
    // emergency → alarm → warn → alert.
    inject("notifications.t.alert",     "alert");
    inject("notifications.t.alarm",     "alarm");
    inject("notifications.t.emergency", "emergency");
    inject("notifications.t.warn",      "warn");

    EXPECT_EQ(signalk_alerts_count(), 4);
    signalk_alert_t a;
    EXPECT_TRUE(signalk_alerts_get(0, &a));
    EXPECT_EQ(a.state, SIGNALK_ALERT_EMERGENCY);
    EXPECT_TRUE(signalk_alerts_get(1, &a));
    EXPECT_EQ(a.state, SIGNALK_ALERT_ALARM);
    EXPECT_TRUE(signalk_alerts_get(2, &a));
    EXPECT_EQ(a.state, SIGNALK_ALERT_WARN);
    EXPECT_TRUE(signalk_alerts_get(3, &a));
    EXPECT_EQ(a.state, SIGNALK_ALERT_ALERT);

    // Eviction: fill 16 alarm-level slots, inject one emergency → emergency
    // takes the place of one of the alarms (lowest-severity slot).
    signalk_alerts_clear();
    for (int i = 0; i < SIGNALK_ALERT_MAX_ACTIVE; i++) {
        char path[64];
        snprintf(path, sizeof(path), "notifications.cap.%d", i);
        inject(path, "alarm");
    }
    EXPECT_EQ(signalk_alerts_count(), SIGNALK_ALERT_MAX_ACTIVE);

    inject("notifications.cap.evict", "emergency");
    EXPECT_EQ(signalk_alerts_count(), SIGNALK_ALERT_MAX_ACTIVE);
    // After eviction the highest-severity entry should be the emergency.
    EXPECT_TRUE(signalk_alerts_get(0, &a));
    EXPECT_EQ(a.state, SIGNALK_ALERT_EMERGENCY);

    // Adding an alert-level entry when full and the lowest is alarm should
    // be rejected (alert < alarm severity).
    inject("notifications.cap.reject", "alert");
    EXPECT_EQ(signalk_alerts_count(), SIGNALK_ALERT_MAX_ACTIVE);
    // Verify the rejected entry isn't somewhere in the list.
    int found_reject = 0;
    for (int i = 0; i < SIGNALK_ALERT_MAX_ACTIVE; i++) {
        if (signalk_alerts_get(i, &a) && a.state == SIGNALK_ALERT_ALERT) {
            found_reject = 1;
            break;
        }
    }
    EXPECT_EQ(found_reject, 0);

    signalk_alerts_clear();
}

bool sw_suite_signalk_alert_cache(char *detail, size_t n) {
    (void)detail; (void)n;
    int fails = 0;
    run(&fails);
    return fails == 0;
}
