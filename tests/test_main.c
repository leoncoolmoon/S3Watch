// Host unit-test runner. Each suite is a single function that takes a
// failure counter and runs all its EXPECT_*; failures accumulate but
// don't abort. Exit code is non-zero iff total failures > 0.

#include <stdio.h>

typedef struct {
    const char *name;
    void (*run)(int *fails);
} suite_t;

extern void run_utc_tm_tests(int *fails);
extern void run_ip_parse_tests(int *fails);
extern void run_signalk_alert_tests(int *fails);

static const suite_t suites[] = {
    { "utc_tm",        run_utc_tm_tests },
    { "ip_parse",      run_ip_parse_tests },
    { "signalk_alert", run_signalk_alert_tests },
};
static const int n_suites = (int)(sizeof(suites) / sizeof(suites[0]));

int main(void) {
    int total_fails = 0;
    for (int i = 0; i < n_suites; i++) {
        int before = total_fails;
        printf("[ RUN  ] %s\n", suites[i].name);
        suites[i].run(&total_fails);
        int added = total_fails - before;
        printf("[ %4s ] %s (%d new failure%s)\n",
               added ? "FAIL" : "OK",
               suites[i].name, added, added == 1 ? "" : "s");
    }
    printf("\n%s — %d total failure%s\n",
           total_fails ? "FAIL" : "PASS",
           total_fails, total_fails == 1 ? "" : "s");
    return total_fails ? 1 : 0;
}
