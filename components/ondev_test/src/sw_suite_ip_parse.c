#include <stdbool.h>
#include <stddef.h>

extern void run_ip_parse_tests(int *fails);

bool sw_suite_ip_parse(char *detail, size_t n) {
    (void)detail; (void)n;
    int fails = 0;
    run_ip_parse_tests(&fails);
    return fails == 0;
}
