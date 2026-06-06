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

#ifdef __cplusplus
}
#endif
