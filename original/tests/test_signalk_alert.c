#include "test.h"
#include "signalk_alert_parse.h"

void run_signalk_alert_tests(int *fails) {
    // Each known state string maps to its enum value.
    EXPECT_EQ(signalk_alert_parse_state("emergency"), SIGNALK_ALERT_EMERGENCY);
    EXPECT_EQ(signalk_alert_parse_state("alarm"),     SIGNALK_ALERT_ALARM);
    EXPECT_EQ(signalk_alert_parse_state("warn"),      SIGNALK_ALERT_WARN);
    EXPECT_EQ(signalk_alert_parse_state("alert"),     SIGNALK_ALERT_ALERT);

    // "normal" and "nominal" both mean inactive — collapse to NORMAL so
    // the cache cleanup branch fires.
    EXPECT_EQ(signalk_alert_parse_state("normal"),  SIGNALK_ALERT_NORMAL);
    EXPECT_EQ(signalk_alert_parse_state("nominal"), SIGNALK_ALERT_NORMAL);

    // Unknown / garbage strings are treated as "not active" so they don't
    // ever materialize as displayed rows.
    EXPECT_EQ(signalk_alert_parse_state(""),           SIGNALK_ALERT_NORMAL);
    EXPECT_EQ(signalk_alert_parse_state("unknown"),    SIGNALK_ALERT_NORMAL);
    EXPECT_EQ(signalk_alert_parse_state("Emergency"),  SIGNALK_ALERT_NORMAL); // case-sensitive
    EXPECT_EQ(signalk_alert_parse_state("ALARM"),      SIGNALK_ALERT_NORMAL);

    // NULL guard
    EXPECT_EQ(signalk_alert_parse_state(NULL), SIGNALK_ALERT_NORMAL);

    // Severity ordering — implicit but worth pinning down, the alerts gui
    // sorts by this enum.
    EXPECT_TRUE(SIGNALK_ALERT_ALERT     > SIGNALK_ALERT_NORMAL);
    EXPECT_TRUE(SIGNALK_ALERT_WARN      > SIGNALK_ALERT_ALERT);
    EXPECT_TRUE(SIGNALK_ALERT_ALARM     > SIGNALK_ALERT_WARN);
    EXPECT_TRUE(SIGNALK_ALERT_EMERGENCY > SIGNALK_ALERT_ALARM);
}
