#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

// step_tracker — persisted step statistics on top of imu_manager's software step
// counter. Owns lifetime / today / rolling-7-day-week totals, best-day and
// best-week records, local-midnight rollover, and flash-wear-conscious persistence
// (an NVS working store committed sparingly + a once-per-day settings.json mirror
// that the existing SD backup carries). It pulls per-batch step deltas from
// imu_manager via imu_manager_set_step_cb(), so imu_manager stays a pure driver.
//
// Call once at boot, AFTER settings_init() (it seeds from settings.json) and after
// imu_manager is set up (it registers the step callback).
void step_tracker_init(void);

// Current totals. Each lazily applies a pending midnight rollover first, so an idle
// watch reports the correct "today" the moment the app reads it. Safe from any task.
uint32_t step_tracker_today(void);      // steps since local midnight
uint32_t step_tracker_week(void);       // today + previous 6 local days
uint32_t step_tracker_lifetime(void);   // steps ever
uint32_t step_tracker_best_day(void);   // record single-day total
uint32_t step_tracker_best_week(void);  // record rolling-week total

// Wipe all statistics (lifetime / today / history / records) and persist.
// Deliberate user action (Settings → Step Counter → Reset Stats).
void step_tracker_reset_all(void);

#ifdef __cplusplus
}
#endif
