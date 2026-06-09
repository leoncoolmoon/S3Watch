#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

// Alarm engine. Owns the firing/scheduling logic; the alarm time, armed state,
// sound, and auto-silence timeout are persisted by `settings`. The UI lives in
// gui (`alarm_clock` screen) and talks to the alarm only through this API.

// Load nothing of its own (state lives in settings) and register the 1 Hz
// `alarm_check` task_coordinator subscriber. Call once at boot, BEFORE
// task_coord_start() (subscribing after start is not thread-safe).
void alarm_manager_init(void);

// True while the alarm is currently ringing — used by the display-wake hook to
// bring the watch up on the alarm app instead of the watchface.
bool alarm_manager_is_firing(void);

// Stop a ringing alarm and disarm it (one-shot). No-op if not ringing.
void alarm_manager_dismiss(void);

// Alarm time + armed state. Thin pass-throughs to settings (persisted). Hour
// clamped [0,23], min [0,59] by settings. set_enabled(false) also silences a
// currently-ringing alarm.
void alarm_manager_set_hour(int hour);
int  alarm_manager_get_hour(void);
void alarm_manager_set_min(int min);
int  alarm_manager_get_min(void);
void alarm_manager_set_enabled(bool enabled);
bool alarm_manager_get_enabled(void);

// Invoked (on the task_coordinator task) when the alarm fires while the display
// is ALREADY ON, so the UI can navigate to the alarm app. The display-off case
// is handled separately by the display-wake hook (which polls is_firing). The
// callback must be cheap and non-blocking (e.g. an lv_async_call).
typedef void (*alarm_fire_cb_t)(void);
void alarm_manager_set_fire_cb(alarm_fire_cb_t cb);

#ifdef __cplusplus
}
#endif
