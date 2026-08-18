#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { ONDEV_TEST_PASS, ONDEV_TEST_FAIL } ondev_test_result_t;
typedef enum { ONDEV_TEST_SW,   ONDEV_TEST_HW   } ondev_test_category_t;

// Invoked once per test, in registry order.
//   index: 0-based, < total
//   detail: short status string (e.g. "Batt=85%, VBUS=on, Charging=yes")
typedef void (*ondev_test_progress_cb)(int index, int total,
                                       ondev_test_category_t cat,
                                       const char *name,
                                       ondev_test_result_t r,
                                       const char *detail,
                                       void *user);

// Run every registered test sequentially on the calling task. Blocking;
// expect a few seconds. Returns total failures.
int ondev_test_run_all(ondev_test_progress_cb cb, void *user);

// Count of registered tests (for UI sizing).
int ondev_test_count(void);

// Get metadata for the test at index (for the UI to pre-populate rows).
// Returns false if index is out of range. name and category are stable
// pointers (string literals); copying is unnecessary.
bool ondev_test_info(int index,
                     ondev_test_category_t *cat,
                     const char **name);

#ifdef __cplusplus
}
#endif
