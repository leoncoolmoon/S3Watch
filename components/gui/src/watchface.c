// Watchface dispatcher.
//
// Owns the watchface container, the 1 Hz LVGL refresh timer, and the
// registry of available faces. Each face is a standalone module in faces/
// that implements the watchface_iface_t contract.
//
// Adding a new face:
//   1. Drop a new file in components/gui/src/faces/face_N.c.
//   2. Implement the three callbacks and define `const watchface_iface_t
//      watchface_faceN = { ... };`.
//   3. Add `&watchface_faceN` to s_faces[] below.
//   4. Bump the option count / label in setting_watchface_screen.c.

#include "watchface.h"
#include "watchface_iface.h"
#include "rtc_lib.h"
#include "settings.h"
#include "esp_log.h"
#include "lvgl.h"

// Registry: each face's file exports a `const watchface_iface_t` symbol.
// To add a face, declare it here and append to s_faces[].
extern const watchface_iface_t watchface_face1;
extern const watchface_iface_t watchface_face2;

static const watchface_iface_t* const s_faces[] = {
    &watchface_face1,
    &watchface_face2,
};
static const int s_face_count = sizeof(s_faces) / sizeof(s_faces[0]);

static lv_obj_t*  s_container = NULL;
static lv_timer_t* s_timer    = NULL;
static const watchface_iface_t* s_active = NULL;

static const watchface_iface_t* pick_face(void) {
    int idx = settings_get_watchface_style();
    if (idx < 0 || idx >= s_face_count) idx = 0;
    return s_faces[idx];
}

void watchface_refresh_now(void) {
    // Pull a fresh time from the ESP32 internal RTC (no I2C). The PCF85063A
    // is re-synced periodically by rtc_minute_sync() and on every display-on.
    rtc_refresh_now();
    if (s_active && s_active->update_time) s_active->update_time();
}

static void tick_cb(lv_timer_t* timer) {
    (void)timer;
    watchface_refresh_now();
}

void watchface_create(lv_obj_t* parent) {
    s_container = lv_obj_create(parent);
    lv_obj_remove_style_all(s_container);
    lv_obj_set_size(s_container, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(s_container, LV_OBJ_FLAG_SCROLLABLE);

    s_active = pick_face();
    if (s_active && s_active->build) s_active->build(s_container);

    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    s_timer = lv_timer_create(tick_cb, 1000, NULL);
    lv_timer_ready(s_timer);
}

void watchface_rebuild(void) {
    if (!s_container) return;
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    lv_obj_clean(s_container);

    s_active = pick_face();
    if (s_active && s_active->build) s_active->build(s_container);

    s_timer = lv_timer_create(tick_cb, 1000, NULL);
    lv_timer_ready(s_timer);
}

void watchface_set_power_state(bool vbus_in, bool charging, int battery_percent) {
    if (s_active && s_active->update_power) {
        s_active->update_power(vbus_in, charging, battery_percent);
    }
}

int watchface_get_count(void) { return s_face_count; }

const char* watchface_get_name(int index) {
    if (index < 0 || index >= s_face_count) return "?";
    return s_faces[index]->name ? s_faces[index]->name : "?";
}
