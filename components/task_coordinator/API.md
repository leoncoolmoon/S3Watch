# task_coordinator API

## Types

```c
typedef void (*task_coord_cb_t)(void *user);
typedef bool (*task_coord_active_fn)(void);  // returns true = "on" cadence
```

## Functions

### `task_coord_init`
```c
void task_coord_init(uint32_t on_period_ms, uint32_t off_period_ms,
                     task_coord_active_fn active_state_fn);
```
Set the base tick periods and the active-state predicate. Does not create the task. Call once during boot before any `task_coord_subscribe()` calls.

- `on_period_ms`: base wake interval when `active_state_fn()` returns true.
- `off_period_ms`: base wake interval when false.
- `active_state_fn`: called each tick to select the current period (e.g. `power_manager_is_awake`).

---

### `task_coord_subscribe`
```c
void task_coord_subscribe(const char *name,
                          task_coord_cb_t cb, void *user,
                          uint32_t period_when_on_ms,
                          uint32_t period_when_off_ms);
```
Register a periodic callback.

- `period_when_on_ms = 0`: skip this subscriber when the system is "on".
- `period_when_off_ms = 0`: skip this subscriber when the system is "off".
- Periods are rounded up to the nearest base tick.
- Call only before `task_coord_start()`.

---

### `task_coord_start`
```c
void task_coord_start(void);
```
Create and start the coordinator task. Call after all subscribers are registered. Subsequent `task_coord_subscribe()` calls are not thread-safe.

---

### `task_coord_dump_stats`
```c
void task_coord_dump_stats(void);
```
Log per-subscriber stats (invocations, total CPU time, worst-case single call) via `ESP_LOGI`. Safe to call at any time from any task.
