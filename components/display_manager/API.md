# display_manager API

### `display_manager_init`
```c
void display_manager_init(void);
```
Subscribe coordinator callbacks and register display ops with `power_manager`. Call from `ui_task()` after LVGL UI is created. Also calls `task_coord_start()` — this must be the last init call before the coordinator begins running.

---

### `display_manager_turn_on` / `display_manager_turn_off`
```c
void display_manager_turn_on(void);
void display_manager_turn_off(void);
```
Request a wake or sleep via `power_manager`. These are thin wrappers; the actual sequence runs through `power_manager_request_wake/sleep`.

---

### `display_manager_is_on`
```c
bool display_manager_is_on(void);
```
Returns true if the display is currently active (not sleeping). Used by `task_coordinator` as the active-state predicate before `power_manager_is_awake` was substituted.

---

### `display_manager_reset_timer`
```c
void display_manager_reset_timer(void);
```
Signal "user activity just happened" from outside the LVGL touch path. Call from PMU short-press handlers or GPIO back-button ISR to reset the inactivity timer. LVGL touch events reset it automatically; this is only needed for non-touch input.

---

### `display_manager_set_on_callback`
```c
void display_manager_set_on_callback(void (*cb)(void));
```
Register a callback fired at the end of `display_wake_impl()`, after the backlight is on. One slot only.

---

### `display_manager_set_pre_show_cb`
```c
typedef void (*display_pre_show_cb_t)(void);
void display_manager_set_pre_show_cb(display_pre_show_cb_t cb);
```
Register a callback that runs inside the LVGL lock on every wake, after the screen is invalidated and before the synchronous render. Use it to push fresh data (time, battery) to widgets so the first visible frame is already correct.
