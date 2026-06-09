# alarm_manager

The alarm engine. Owns the scheduling and firing logic for the watch's single
one-shot alarm; the UI is the `alarm_clock` screen in `gui`, which talks to the
alarm only through this component's API. UI-agnostic — never includes gui/lvgl.

## Responsibilities

- Registers a 1 Hz `alarm_check` `task_coordinator` subscriber at boot (runs
  display-on **and** display-off), so a set alarm fires even if the app is never
  opened and survives reboot.
- On an exact minute match: wakes the display (`power_manager_request_wake`),
  resets the display inactivity timer, and starts the looping alarm sound via
  `audio_alert_alarm_start()` (the `AM_CLIENT_ALARM` codec tier — keeps ringing
  through display sleep).
- Enforces the auto-silence timeout (`settings_get_alarm_timeout_min`).
- One-shot: firing then dismiss / auto-silence disarms the alarm.

## State & persistence

The alarm time, armed flag, sound index, and auto-silence timeout are persisted
by `settings` (`alarm_hour`, `alarm_min`, `alarm_enabled`, `alarm_sound`,
`alarm_timeout_min`). This component keeps no persistent state of its own — only
the transient `firing` flag and the ring start time. Getters/setters here are
thin pass-throughs to `settings`, except `set_enabled(false)`, which also
silences a current ring.

## UI hand-off (no gui dependency)

When the alarm fires it must surface the alarm screen, but this component cannot
include gui. Two hooks, both registered by gui:

- **Display already on** → the registered `alarm_fire_cb_t` (set via
  `alarm_manager_set_fire_cb`) is invoked on the task_coordinator task; gui's
  callback does an `lv_async_call` to open the alarm app.
- **Display was off** → `power_manager_request_wake` runs the display-wake
  pre-show hook, which polls `alarm_manager_is_firing()` and opens the alarm app
  itself. (Splitting the two avoids a double-open.)

## Initialization

Call `alarm_manager_init()` in `main()` after `settings_init()` and before
`task_coord_start()` (which runs later inside `ui_task → display_manager_init`).

```c
settings_init();
alarm_manager_init();   // <-- here
```

## Dependencies

`settings`, `audio_alert`, `display_manager`, `power_manager`, `task_coordinator`.
`gui → alarm_manager`; nothing depends back, so the graph stays acyclic.
