# ondev_test

On-device hardware and software test runner. Provides a registry of tests with progress callbacks, suitable for driving a UI progress list.

## Test categories

- **SW** — software-only tests (delta parser, JSON parsing, settings serialisation, etc.)
- **HW** — hardware-in-the-loop tests (battery voltage, IMU read, RTC, codec ping, etc.)

## Usage

Tests are registered internally (not via public API). The runner is invoked from the On-Device Test screen in the settings menu:

```c
int total = ondev_test_count();

// Optional: pre-populate UI rows
for (int i = 0; i < total; i++) {
    ondev_test_category_t cat;
    const char *name;
    ondev_test_info(i, &cat, &name);
}

// Run — blocks for several seconds; fires cb for each test
int failures = ondev_test_run_all(my_progress_cb, user_data);
```

The progress callback is fired once per test, in registration order, with the result and a short detail string.

## Dependencies

`sensors`, `signalk_client` (test hooks), `settings`, `bsp_extra`
