// Round-trip a few settings through SPIFFS. Saves the prior values before
// mutating, restores them at the end, regardless of pass/fail.

#include <stdbool.h>
#include <string.h>
#include "ondev_test_expect.h"
#include "settings.h"

static void run(int *fails) {
    // Snapshot prior values
    uint8_t  prev_brightness = settings_get_brightness();
    uint16_t prev_port       = settings_get_signalk_port();
    char     prev_host[64];
    strncpy(prev_host, settings_get_signalk_host(), sizeof(prev_host) - 1);
    prev_host[sizeof(prev_host) - 1] = '\0';
    // Alarm fields — persistence here is what makes a set alarm survive reboot.
    int     prev_a_hour    = settings_get_alarm_hour();
    int     prev_a_min     = settings_get_alarm_min();
    bool    prev_a_enabled = settings_get_alarm_enabled();
    int     prev_a_timeout = settings_get_alarm_timeout_min();
    uint8_t prev_a_sound   = settings_get_alarm_sound();

    // Mutate, save, then mutate again, then load, expect the saved value.
    settings_set_brightness(73);
    settings_set_signalk_port(8901);
    settings_set_signalk_host("10.42.42.7");
    settings_set_alarm_hour(6);
    settings_set_alarm_min(42);
    settings_set_alarm_enabled(true);
    settings_set_alarm_timeout_min(17);
    settings_set_alarm_sound(2);
    EXPECT_TRUE(settings_save());

    // Stomp the in-memory copies through more setters
    settings_set_brightness(11);
    settings_set_signalk_port(1234);
    settings_set_signalk_host("noped");
    settings_set_alarm_hour(1);
    settings_set_alarm_min(1);
    settings_set_alarm_enabled(false);
    settings_set_alarm_timeout_min(5);
    settings_set_alarm_sound(0);

    EXPECT_TRUE(settings_load());
    EXPECT_EQ(settings_get_brightness(),    73);
    EXPECT_EQ(settings_get_signalk_port(),  8901);
    EXPECT_STREQ(settings_get_signalk_host(), "10.42.42.7");
    EXPECT_EQ(settings_get_alarm_hour(),        6);
    EXPECT_EQ(settings_get_alarm_min(),         42);
    EXPECT_TRUE(settings_get_alarm_enabled());
    EXPECT_EQ(settings_get_alarm_timeout_min(), 17);
    EXPECT_EQ(settings_get_alarm_sound(),       2);

    // Clamp-on-set: out-of-range inputs are pinned to the valid range.
    settings_set_alarm_hour(99);          EXPECT_EQ(settings_get_alarm_hour(), 23);
    settings_set_alarm_min(-3);           EXPECT_EQ(settings_get_alarm_min(),  0);
    settings_set_alarm_timeout_min(999);  EXPECT_EQ(settings_get_alarm_timeout_min(), 30);
    settings_set_alarm_timeout_min(0);    EXPECT_EQ(settings_get_alarm_timeout_min(), 1);

    // Restore prior values and persist
    settings_set_brightness(prev_brightness);
    settings_set_signalk_port(prev_port);
    settings_set_signalk_host(prev_host);
    settings_set_alarm_hour(prev_a_hour);
    settings_set_alarm_min(prev_a_min);
    settings_set_alarm_enabled(prev_a_enabled);
    settings_set_alarm_timeout_min(prev_a_timeout);
    settings_set_alarm_sound(prev_a_sound);
    settings_save();
}

bool sw_suite_settings(char *detail, size_t n) {
    (void)detail; (void)n;
    int fails = 0;
    run(&fails);
    return fails == 0;
}
