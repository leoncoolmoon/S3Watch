#pragma once
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

// Put the QMI8658 IMU in Power-Down mode (~6 µA). The chip is hardwired to
// the always-on VCC3V3 rail and powers up active (~270 µA); this drops it to
// the lowest state the chip supports while keeping its digital interface
// reachable. Safe to call once at boot. Step-counting / motion classification
// machinery was removed from this component.
void sensors_low_power_idle(void);

// Test hook: wake the IMU, read one accel sample (g units), then return it
// to Power-Down. Used by the on-device test runner. ~50 ms blocking call.
esp_err_t sensors_test_read_accel_g(float *x, float *y, float *z);

#ifdef __cplusplus
}
#endif
