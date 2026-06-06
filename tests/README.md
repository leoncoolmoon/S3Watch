# Host (native) unit tests

These tests run on the build host (not the ESP32). They exercise pure-logic
helpers that have been extracted from the firmware into header-only files
with **no ESP-IDF dependencies**:

| Header (also used by firmware) | Tests |
|---|---|
| [components/bsp_extra/include/utc_tm_to_epoch.h](../components/bsp_extra/include/utc_tm_to_epoch.h) | [test_utc_tm.c](test_utc_tm.c) — Hinnant days_from_civil vs known epochs and host `timegm` |
| [components/gui/include/ip_parse.h](../components/gui/include/ip_parse.h) | [test_ip_parse.c](test_ip_parse.c) — IPv4 round-trip + reject malformed |
| [components/signalk_client/include/signalk_alert_parse.h](../components/signalk_client/include/signalk_alert_parse.h) | [test_signalk_alert.c](test_signalk_alert.c) — SignalK state string → enum + severity ordering |

The firmware `#include`s the same headers, so a passing test here proves the
exact code path the firmware uses.

## Build + run

```bash
cmake -S tests -B tests/build
cmake --build tests/build
tests/build/s3watch_tests
```

Or via CTest:
```bash
ctest --test-dir tests/build --output-on-failure
```

Expected output ends with `PASS — 0 total failures`. Exit code is non-zero
on any failure.

## Adding a new suite

1. Extract the logic you want to test into a pure-C header under the
   relevant component's `include/` directory (no `esp_*` / `lvgl` / `freertos`
   includes — those break the host build). Update the firmware source file
   to `#include` and use it.
2. Add `test_<name>.c` here with a `void run_<name>_tests(int *fails)`
   function using the `EXPECT_*` macros from [test.h](test.h).
3. Register it in [CMakeLists.txt](CMakeLists.txt) (add the source file)
   and in [test_main.c](test_main.c) (extern + add to the `suites[]` array).

## Why not on-device tests?

Most of the firmware (LVGL widgets, BSP I/O, FreeRTOS tasks) can only be
exercised on the actual hardware. ESP-IDF ships a `unity` framework for
that, but it requires flashing and serial-monitor coordination, so the
feedback loop is slow. The host tests here cover the pure logic that can
be tested in ~50 ms with no hardware.
