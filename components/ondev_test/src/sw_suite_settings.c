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

    // Mutate, save, then mutate again, then load, expect the saved value.
    settings_set_brightness(73);
    settings_set_signalk_port(8901);
    settings_set_signalk_host("10.42.42.7");
    EXPECT_TRUE(settings_save());

    // Stomp the in-memory copies through more setters
    settings_set_brightness(11);
    settings_set_signalk_port(1234);
    settings_set_signalk_host("noped");

    EXPECT_TRUE(settings_load());
    EXPECT_EQ(settings_get_brightness(),    73);
    EXPECT_EQ(settings_get_signalk_port(),  8901);
    EXPECT_STREQ(settings_get_signalk_host(), "10.42.42.7");

    // Restore prior values and persist
    settings_set_brightness(prev_brightness);
    settings_set_signalk_port(prev_port);
    settings_set_signalk_host(prev_host);
    settings_save();
}

bool sw_suite_settings(char *detail, size_t n) {
    (void)detail; (void)n;
    int fails = 0;
    run(&fails);
    return fails == 0;
}
