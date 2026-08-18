#pragma once
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif

void watchface_create(lv_obj_t* parent);
void watchface_rebuild(void);

// Synchronously refresh the active face's time labels from the ESP32 internal
// clock. Caller is responsible for holding the LVGL lock. Used on display-on
// so the first visible frame already shows the correct time instead of waiting
// for the next 1 Hz tick.
void watchface_refresh_now(void);

// Update battery / VBUS / charging indicators on the active face.
void watchface_set_power_state(bool vbus_in, bool charging, int battery_percent);

// Registry introspection — used by the watchface picker UI so it stays in
// sync with whatever faces are compiled in.
int          watchface_get_count(void);
const char*  watchface_get_name(int index);


#ifdef __cplusplus
}
#endif
