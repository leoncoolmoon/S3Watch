// Wrapper: the actual test body lives in tests/test_utc_tm.c and is
// compiled into this component (see CMakeLists.txt).

#include <stdbool.h>
#include <stddef.h>

extern void run_utc_tm_tests(int *fails);

bool sw_suite_utc_tm(char *detail, size_t n) {
    (void)detail; (void)n;
    int fails = 0;
    run_utc_tm_tests(&fails);
    return fails == 0;
}
