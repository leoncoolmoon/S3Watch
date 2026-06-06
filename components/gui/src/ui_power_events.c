// BSP power events → watchface battery indicator.
//
// Two paths feed the indicator:
//   1. Event-driven: bsp posts an event on VBUS/charging transitions; we
//      apply it immediately while holding the LVGL lock.
//   2. Periodic: every 30 s (only while display is on) we snapshot the
//      battery state and dispatch the update onto the LVGL thread. This
//      also triggers the NTP daily re-sync check.

#include "ui_power_events.h"
#include "watchface.h"
#include "ui_tileview.h"
#include "display_manager.h"
#include "bsp/esp32_s3_touch_amoled_2_06.h"
#include "bsp/esp-bsp.h"
#include "esp_event.h"
#include "ntp_sync.h"
#include "task_coordinator.h"
#include "lvgl.h"
#include <stdbool.h>

static void power_ui_evt(void* handler_arg, esp_event_base_t base, int32_t id,
                          void* event_data) {
  (void)handler_arg;
  (void)base;
  (void)id;
  bsp_power_event_payload_t* pl = (bsp_power_event_payload_t*)event_data;
  if (pl) {
    int pct = bsp_power_get_battery_percent();
    bsp_display_lock(0);
    watchface_set_power_state(pl->vbus_in, pl->charging, pct);
    bsp_display_unlock();
  }
}

void ui_power_refresh_sync(void) {
  bool vbus     = bsp_power_is_vbus_in();
  bool charging = bsp_power_is_charging();
  int  pct      = bsp_power_get_battery_percent();
  watchface_set_power_state(vbus, charging, pct);
}

// Snapshot of latest power state read by the coordinator subscriber. Passed
// through lv_async_call into the LVGL thread where the watchface widgets are
// safe to touch.
typedef struct { bool vbus; bool charging; int percent; } ui_power_snapshot_t;
static ui_power_snapshot_t s_pwr_snap;

static void ui_power_apply_async(void* arg) {
  ui_power_snapshot_t* s = (ui_power_snapshot_t*)arg;
  watchface_set_power_state(s->vbus, s->charging, s->percent);
}

// Coordinator subscriber: every 30 s while display is on, snapshot the BSP
// power state and dispatch a watchface update onto the LVGL thread. Also
// kicks the NTP daily re-sync check (a no-op until 24 h have elapsed).
// Skipped while display is off — the watchface isn't visible and LVGL is
// suspended.
static void ui_battery_refresh_cb(void* user) {
  (void)user;
  s_pwr_snap.vbus     = bsp_power_is_vbus_in();
  s_pwr_snap.charging = bsp_power_is_charging();
  s_pwr_snap.percent  = bsp_power_get_battery_percent();
  lv_async_call(ui_power_apply_async, &s_pwr_snap);
  ntp_sync_check();
}

// Display-manager pre-show hook: runs on every off→on transition inside the
// LVGL lock, before the synchronous render + backlight enable. Pulls fresh
// time + battery state into the active watchface so the first visible frame
// is already correct.
static void pre_show_cb(void) {
  // Every wake comes up on the watchface, regardless of which tile the user
  // was viewing before the screen slept. Must happen before the refresh
  // calls below so the labels we update belong to the now-active tile.
  ui_tileview_reset_to_watchface();
  watchface_refresh_now();
  ui_power_refresh_sync();
}

void ui_power_events_start(void) {
  esp_event_handler_register(BSP_POWER_EVENT_BASE, ESP_EVENT_ANY_ID,
                              power_ui_evt, NULL);
  task_coord_subscribe("ui_battery_refresh", ui_battery_refresh_cb, NULL,
                       /*on*/ 30000, /*off*/ 0);
  display_manager_set_pre_show_cb(pre_show_cb);
  // Trigger once immediately so the watchface doesn't show 0% on boot.
  ui_battery_refresh_cb(NULL);
}
