# display_manager

Manages LVGL display lifecycle: inactivity tracking, brightness dimming, and the sleep/wake transition via `power_manager`.

## Responsibilities

- Registers `pm_display_ops_t` with `power_manager` so sleep/wake is driven centrally.
- Tracks LVGL inactivity time to dim the display at 50% of the timeout, then turn it off at 100%.
- Implements two dim cycles: CYCLE_WAKE (fresh wake) and CYCLE_TOUCH (after user activity), each with different bright/dim/off durations.
- Puts the FT3168 touch controller into Monitor mode (~30 µA) after 10 s of idle, saving ~1.5 mA. Exits Monitor mode on touch or wake.
- Suspends/resumes the LVGL port task across sleep/wake to prevent spurious flushes.
- Runs a `pre_show_cb` just before the backlight comes on after wake, so widgets show fresh data on the first visible frame.

## Dim / off timing

| Phase | CYCLE_WAKE | CYCLE_TOUCH |
|-------|-----------|-------------|
| Dim at | timeout / 2 | 5 s |
| Off at | timeout | 10 s |

Timeout is read from `settings_get_display_timeout()` on every coordinator tick, so a change takes effect within 100 ms.

## Sleep / wake sequence

**Sleep (`display_sleep_impl`):**
1. Stop LVGL port + suspend LVGL task.
2. Disable LVGL input device polling.
3. Set FT3168 to Monitor mode.
4. Call `bsp_display_sleep()` (DCS Sleep-In; ALDO kept alive by `power_manager`).
5. Set backlight to 0.

**Wake (`display_wake_impl`):**
1. Call `bsp_display_wake()` (DCS Sleep-Out — panel was in DCS sleep, not power-cycled).
2. Recover I2C bus.
3. Resume LVGL port + task.
4. Populate widgets via `pre_show_cb` and synchronously render one frame.
5. Set backlight to `settings_get_brightness()`.
6. Set FT3168 to Active mode.
7. Re-enable LVGL input.

## Initialization

`display_manager_init()` is called from `ui_task()` after the LVGL UI is created. It subscribes coordinator callbacks and starts the task.

## Dependencies

`power_manager`, `settings`, `bsp_extra`, `task_coordinator`
