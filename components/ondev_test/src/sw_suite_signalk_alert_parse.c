#include <stdbool.h>
#include <stddef.h>

extern void run_signalk_alert_tests(int *fails);

bool sw_suite_signalk_alert_parse(char *detail, size_t n) {
    (void)detail; (void)n;
    int fails = 0;
    run_signalk_alert_tests(&fails);
    return fails == 0;
}
