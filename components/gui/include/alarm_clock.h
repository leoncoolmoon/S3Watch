#pragma once
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif

// Build the alarm app screen on the given tile/parent (app picker entry). All
// alarm logic lives in the `alarm_manager` component; this is the UI only.
void alarm_clock_create(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif
