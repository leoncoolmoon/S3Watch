# alarm_manager API

```c
#include "alarm_manager.h"
```

## Lifecycle

### `alarm_manager_init`
```c
void alarm_manager_init(void);
```
Register the 1 Hz `alarm_check` task_coordinator subscriber. Call once at boot,
after `settings_init()` and **before** `task_coord_start()` (subscribing after
start is not thread-safe). Alarm state is read live from `settings`, so there is
nothing to load.

## Firing state

### `alarm_manager_is_firing`
```c
bool alarm_manager_is_firing(void);
```
True while the alarm is currently ringing. Polled by the display-wake hook to
bring the watch up on the alarm app instead of the watchface.

### `alarm_manager_dismiss`
```c
void alarm_manager_dismiss(void);
```
Stop a ringing alarm and disarm it (one-shot: persists `alarm_enabled = false`).
No-op if not currently ringing.

## Alarm time + armed state

```c
void alarm_manager_set_hour(int hour);   int  alarm_manager_get_hour(void);
void alarm_manager_set_min(int min);     int  alarm_manager_get_min(void);
void alarm_manager_set_enabled(bool e);  bool alarm_manager_get_enabled(void);
```
Thin pass-throughs to the persisted `settings` values (hour clamped `[0,23]`,
min `[0,59]`). `alarm_manager_set_enabled(false)` additionally silences a
currently-ringing alarm.

## Fire callback

```c
typedef void (*alarm_fire_cb_t)(void);
void alarm_manager_set_fire_cb(alarm_fire_cb_t cb);
```
Register a callback invoked (on the task_coordinator task) when the alarm fires
**while the display is already on**, so the UI can navigate to the alarm app.
The callback must be cheap and non-blocking — gui uses an `lv_async_call`. The
display-off case is handled separately by the display-wake hook, so the callback
is not invoked then.
