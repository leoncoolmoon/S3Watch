#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

// imu_manager — owns the QMI8658C IMU: its power state and a software step counter.
//
// The chip is hardwired to the always-on VCC3V3 rail (it can't be ALDO-gated by
// power_manager); it powers up active (~270 µA) and drops to ~6 µA "Power-Down"
// when idle. This die is the QMI8658*C*, which has NO hardware pedometer, so step
// counting is software: the accel streams into the chip's FIFO and a peak detector
// runs on the drained samples (light-sleep friendly). See imu_manager.c.

// Put the IMU in Power-Down (~6 µA). Safe to call once at boot.
void imu_manager_idle(void);

// Enable/disable step counting.
//   enable=true  — accel on (31.25 Hz) → FIFO; the drain task counts steps.
//   enable=false — stop it and return the IMU to Power-Down.
// Idempotent; initialises the driver internally. Enabling zeroes the live count.
void imu_manager_set_step_counting(bool enable);

// Read the live step count (since enable). Returns ESP_OK with *out set; if step
// counting is disabled, sets *out = 0 and returns ESP_OK.
esp_err_t imu_manager_get_steps(uint32_t *out);

// Reset the live step count to 0.
void imu_manager_reset_steps(void);

// Register a callback invoked from the FIFO-drain task with the number of NEW steps
// detected in each batch (delta > 0 only). step_tracker uses this to accumulate
// lifetime/daily/weekly totals. Pass NULL to clear. Single subscriber.
void imu_manager_set_step_cb(void (*cb)(uint32_t new_steps));

// Test hook: wake the IMU, read one accel sample (g units), then restore the
// prior state (Power-Down, or accel/FIFO if step counting was running). ~50 ms.
esp_err_t imu_manager_test_read_accel_g(float *x, float *y, float *z);

#ifdef __cplusplus
}
#endif
