#pragma once
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif

void app_picker_create(lv_obj_t *parent);

// Re-read settings_get_watchface_bg() and update the background image.
// Call whenever the watchface background setting changes.
void app_picker_refresh_bg(void);

#ifdef __cplusplus
}
#endif
