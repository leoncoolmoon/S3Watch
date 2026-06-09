# ondev_test API

## Types

```c
typedef enum { ONDEV_TEST_PASS, ONDEV_TEST_FAIL } ondev_test_result_t;
typedef enum { ONDEV_TEST_SW,   ONDEV_TEST_HW   } ondev_test_category_t;

typedef void (*ondev_test_progress_cb)(
    int index,                  // 0-based, < total
    int total,
    ondev_test_category_t cat,
    const char *name,           // stable pointer (string literal)
    ondev_test_result_t result,
    const char *detail,         // short status string, e.g. "Batt=85%, VBUS=on"
    void *user
);
```

## Functions

### `ondev_test_run_all`
```c
int ondev_test_run_all(ondev_test_progress_cb cb, void *user);
```
Run every registered test sequentially on the calling task. Blocking — expect several seconds for HW tests. Fires `cb` once per test. Returns total failure count.

---

### `ondev_test_count`
```c
int ondev_test_count(void);
```
Total number of registered tests. Use to size a UI list before running.

---

### `ondev_test_info`
```c
bool ondev_test_info(int index, ondev_test_category_t *cat, const char **name);
```
Get metadata for the test at `index` without running it. Useful for pre-populating UI rows. `name` is a stable string literal pointer — no copying needed. Returns false if `index` is out of range.
