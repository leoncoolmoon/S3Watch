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
//   The setting_watchface_screen picker is driven by watchface_get_count() /
//   watchface_get_name() and updates automatically — no change needed there.

#include "watchface.h"
#include "watchface_iface.h"
#include "rtc_lib.h"
#include "settings.h"
#include "ui_fonts.h"
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

// ── Shared face helpers ──────────────────────────────────────────────────
//
// Generic building blocks declared in watchface_iface.h so every face can
// reuse them instead of duplicating background/battery/hour-format code.

void watchface_add_background(lv_obj_t *parent) {
    lv_obj_t* image = lv_image_create(parent);
    int bg = settings_get_watchface_bg();
    if (bg == 1) {
        LV_IMAGE_DECLARE(background_wf);
        lv_image_set_src(image, &background_wf);
    } else if (bg == 2) {
        LV_IMAGE_DECLARE(background_wf_3);
        lv_image_set_src(image, &background_wf_3);
    } else {
        LV_IMAGE_DECLARE(background_wf_2);
        lv_image_set_src(image, &background_wf_2);
    }
    lv_obj_set_align(image, LV_ALIGN_CENTER);
}

void watchface_build_battery_widget(lv_obj_t *parent, lv_obj_t **out_icon,
                                    lv_obj_t **out_pct_label,
                                    lv_obj_t **out_charge_label) {
    extern const lv_image_dsc_t image_battery_icon;
    lv_obj_t* icon = lv_image_create(parent);
    lv_image_set_src(icon, &image_battery_icon);
    lv_obj_set_align(icon, LV_ALIGN_TOP_MID);
    lv_obj_set_x(icon, -100);
    lv_obj_set_style_img_recolor_opa(icon, LV_OPA_COVER, 0);
    lv_obj_set_style_img_recolor(icon, lv_color_hex(0x909090), 0);

    lv_obj_t* pct_label = lv_label_create(parent);
    lv_obj_align_to(pct_label, icon, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    lv_obj_set_style_text_color(pct_label, lv_color_hex(0xc0c0c0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(pct_label, "--%");
    lv_obj_set_style_text_font(pct_label, &font_normal_26, 0);

    lv_obj_t* charge_label = lv_label_create(icon);
#ifdef LV_SYMBOL_CHARGE
    lv_label_set_text(charge_label, LV_SYMBOL_CHARGE);
#else
    lv_label_set_text(charge_label, "⚡");
#endif
    lv_obj_center(charge_label);
    lv_obj_set_style_text_font(charge_label, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(charge_label, lv_color_white(), 0);
    lv_obj_add_flag(charge_label, LV_OBJ_FLAG_HIDDEN);

    *out_icon         = icon;
    *out_pct_label    = pct_label;
    *out_charge_label = charge_label;
}

void watchface_update_battery_widget(lv_obj_t *icon, lv_obj_t *pct_label,
                                     lv_obj_t *charge_label, bool vbus_in,
                                     bool charging, int battery_percent) {
    if (!icon) return;
    lv_color_t col = lv_color_hex(0x909090);
    if (vbus_in)  col = lv_color_hex(0x00BFFF);
    if (charging) col = lv_color_hex(0x00FF00);
    lv_obj_set_style_img_recolor(icon, col, 0);
    if (pct_label) {
        if (battery_percent >= 0 && battery_percent <= 100) {
            char buf[8];
            lv_snprintf(buf, sizeof(buf), "%d%%", battery_percent);
            lv_label_set_text(pct_label, buf);
        } else {
            lv_label_set_text(pct_label, "--%");
        }
    }
    if (charge_label) {
        if (vbus_in || charging) lv_obj_clear_flag(charge_label, LV_OBJ_FLAG_HIDDEN);
        else                     lv_obj_add_flag(charge_label, LV_OBJ_FLAG_HIDDEN);
    }
}

void watchface_update_hour_label(lv_obj_t *label_hour, lv_obj_t *label_ampm) {
    int hour = rtc_get_hour();
    if (settings_get_time_24h()) {
        if (label_hour) lv_label_set_text_fmt(label_hour, "%02d", hour);
        if (label_ampm) lv_obj_add_flag(label_ampm, LV_OBJ_FLAG_HIDDEN);
    } else {
        int h12 = hour % 12;
        if (h12 == 0) h12 = 12;
        if (label_hour) lv_label_set_text_fmt(label_hour, "%02d", h12);
        if (label_ampm) {
            lv_label_set_text(label_ampm, hour >= 12 ? "PM" : "AM");
            lv_obj_clear_flag(label_ampm, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

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
