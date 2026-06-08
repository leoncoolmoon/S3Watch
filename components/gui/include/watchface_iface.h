// Watchface module interface.
//
// Each face lives in its own file in components/gui/src/faces/ and exports
// one of these structs. To add face 3, drop a new file in faces/, write the
// three callbacks against this interface, and add one line to the registry
// in watchface.c.

#pragma once
#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // Display name for picker UIs ("Face 1", "Face 2", ...).
    const char *name;

    // Build the face into `parent`. Should create whatever widgets the face
    // needs and stash pointers in the face's own file-scope statics. Called
    // once per face activation.
    void (*build)(lv_obj_t *parent);

    // Refresh the time display. Called once per second by the watchface
    // dispatcher's 1 Hz LVGL timer. Faces should pull current time from
    // rtc_lib.h and any user prefs (e.g. 24h mode) from settings.h. The
    // dispatcher does no formatting on the face's behalf.
    void (*update_time)(void);

    // Apply a new battery / charging / VBUS state to the face's power
    // indicators (if it has any). Faces with no power UI implement this as
    // a no-op.
    void (*update_power)(bool vbus_in, bool charging, int battery_percent);
} watchface_iface_t;

// ── Shared face helpers ──────────────────────────────────────────────────
//
// Generic building blocks every face needs (background image, battery
// widget, 12h/24h hour formatting), implemented once in watchface.c so
// individual face files stay focused on their own layout — see the "Adding
// a new face" guidance in faces/README.md.

// Create the user-selected background image as a child of `parent`,
// centered. Faces call this first thing in build().
void watchface_add_background(lv_obj_t *parent);

// Create the standard battery indicator (icon + percent label + charge
// glyph) as children of `parent`, in the standard top-mid position. Faces
// stash the returned widget pointers and pass them straight through to
// watchface_update_battery_widget() from their update_power callback.
void watchface_build_battery_widget(lv_obj_t *parent, lv_obj_t **out_icon,
                                    lv_obj_t **out_pct_label,
                                    lv_obj_t **out_charge_label);

// Apply vbus/charging/battery-percent state to a widget built by
// watchface_build_battery_widget(). Each pointer is checked individually,
// so it's safe to call even if build() bailed out early and left some NULL.
void watchface_update_battery_widget(lv_obj_t *icon, lv_obj_t *pct_label,
                                     lv_obj_t *charge_label, bool vbus_in,
                                     bool charging, int battery_percent);

// Format the current hour into `label_hour` per the user's 12h/24h
// preference, showing/hiding `label_ampm` to match. Either pointer may be
// NULL.
void watchface_update_hour_label(lv_obj_t *label_hour, lv_obj_t *label_ampm);

#ifdef __cplusplus
}
#endif
