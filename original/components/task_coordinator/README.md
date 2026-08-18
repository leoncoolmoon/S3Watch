# task_coordinator

A single shared FreeRTOS task that runs a list of periodic callbacks at configurable cadences, switching between "display on" and "display off" rates automatically.

## Problem it solves

Many subsystems need recurring work on a slow timer: PMU button polling, RTC sync, inactivity checking, touch IC idle, PM profiling. Creating a dedicated task per subscriber wastes stack RAM and scheduler overhead. task_coordinator runs all of them on one task with a shared tick.

## How it works

The coordinator wakes at the configured base period (`on_period_ms` or `off_period_ms`, chosen by `active_state_fn`). For each subscriber, it tracks elapsed time since the last invocation and calls the callback when that subscriber's period has elapsed.

- **on period**: cadence when `active_state_fn()` returns true (typically display is on).
- **off period**: cadence when false (typically display is off, system sleeping).
- Either period may be 0 to skip all calls while in that state.

Callbacks run sequentially on the coordinator task. They must be fast and non-blocking. The coordinator acquires the LVGL mutex before invoking each callback, so **subscribers can safely call LVGL APIs directly** — no `bsp_display_lock` needed inside a callback. Do not call `bsp_display_lock` from within a subscriber; it is already held and will deadlock.

## Initialization order

```c
// In power_manager_init():
task_coord_init(100, 1000, power_manager_is_awake);

// Subscribers registered during boot (power_manager, display_manager, etc.):
task_coord_subscribe("name", cb, ctx, on_ms, off_ms);

// After all subscribers are registered:
task_coord_start();  // called from display_manager_init()
```

The task is not created until `task_coord_start()` to avoid running a partial subscriber list on the other core.

## Stats

`task_coord_dump_stats()` logs per-subscriber invocation counts, total CPU time, and worst-case single-call time. Useful for finding slow callbacks.

## Limits

Subscriber slots: **16** (compile-time constant). Currently ~11 are used. Subscriptions are permanent — there is no unsubscribe. Exceeding the limit logs an error and drops the subscriber silently, so check headroom before adding new subscribers.

## Dependencies

None (ESP-IDF base only).
