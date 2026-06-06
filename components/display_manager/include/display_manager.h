#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

void display_manager_init(void);
void display_manager_set_on_callback(void (*cb)(void));

// Register a callback that runs on every display-off → on transition,
// inside the LVGL port lock, after the active screen has been invalidated
// and before the synchronous render + backlight enable. Use it to populate
// widgets with fresh state (time, battery) so the first visible frame is
// already correct.
typedef void (*display_pre_show_cb_t)(void);
void display_manager_set_pre_show_cb(display_pre_show_cb_t cb);

// Register a callback that runs at the very start of every on → off
// transition, before the panel and rails are shut down. Use it to tear
// down resources whose lifetime should match the display being on
// (e.g. close a SignalK WebSocket + stop WiFi).
typedef void (*display_pre_off_cb_t)(void);
void display_manager_set_pre_off_cb(display_pre_off_cb_t cb);

void display_manager_turn_on(void);
void display_manager_turn_off(void);
bool display_manager_is_on(void);
void display_manager_reset_timer(void);

// Early PM setup: create and acquire a NO_LIGHT_SLEEP lock so the
// system won’t enter light-sleep during boot/UI init. Safe to call multiple times.
void display_manager_pm_early_init(void);

#ifdef __cplusplus
}
#endif
