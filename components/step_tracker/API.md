# step_tracker API

State is held in module statics (one mutex-guarded `step_stats_t`); no handle is
exposed. Getters are safe to call from any task (e.g. the LVGL/UI task).

### `step_tracker_init`
```c
void step_tracker_init(void);
```
Load persisted stats (NVS → else seed from `settings.json` → else zero), apply any
midnight rollover(s) that elapsed while powered off, and register the `imu_manager`
step callback. Call once at boot **after** `settings_init()` and after the
`imu_manager_set_step_counting(...)` setup. Idempotent.

### Getters
```c
uint32_t step_tracker_today(void);      // steps since local midnight
uint32_t step_tracker_week(void);       // today + previous 6 local days
uint32_t step_tracker_lifetime(void);   // steps ever
uint32_t step_tracker_best_day(void);   // record single-day total
uint32_t step_tracker_best_week(void);  // record rolling-week total
```
Each lazily applies a pending local-midnight rollover before returning, so "today" is
correct on read even on an otherwise idle watch. Return 0 before `step_tracker_init()`.

### `step_tracker_reset_all`
```c
void step_tracker_reset_all(void);
```
Wipe all statistics (lifetime/today/history/records) and persist immediately (NVS +
`settings.json`). Wired to **Settings → Step Counter → Reset Stats**.

---

## Implementation notes

- **Delta source:** `imu_manager` calls back with the number of *new* steps per FIFO
  drain (delta > 0 only). `step_tracker` adds them to `today`/`lifetime`, refreshes
  records, and (throttled) commits to NVS.
- **Local day** = `(time(NULL) + tm.tm_gmtoff) / 86400` via `localtime_r` (TZ applied
  by `settings`). Rollover compares this to the stored `today_day`.
- **Persisted struct** `step_stats_t` is defined in `settings.h` (avoids a
  settings⇄step_tracker include cycle); `magic` (`"STK1"`) + exact-size checks gate
  loads, so a layout change cleanly invalidates old blobs.
- **Thread-safety / real-time:** a single FreeRTOS mutex guards the struct, but it is
  held **only for arithmetic** — every flash write (NVS commit, `settings.json`
  mirror) is done on a snapshot *after* the lock is released. This is deliberate: the
  wake path runs `lv_refr_now()` on the task_coordinator task and needs the LVGL lock;
  if a getter on the UI task (which holds the LVGL lock) ever blocked on `s_lock`
  across a flash write, it would stall waking/leaving. Keeping flash out of the
  critical section bounds every lock hold to microseconds.
