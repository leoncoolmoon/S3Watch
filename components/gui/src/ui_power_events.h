// Internal header: BSP power-event subscription + periodic battery refresh.

#pragma once
#ifdef __cplusplus
extern "C" {
#endif

// Register the BSP power-event handler and the coordinator subscriber that
// refreshes the watchface battery indicator. Call once from ui_task().
void ui_power_events_start(void);

// Synchronously read AXP2101 power state and push it into the active
// watchface. Caller is responsible for holding the LVGL lock. Used on
// display-on so the first visible frame already shows the correct battery /
// VBUS / charging state instead of waiting for the next 30 s tick.
void ui_power_refresh_sync(void);

#ifdef __cplusplus
}
#endif
