#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Step counter app — displays the QMI8658 hardware pedometer's step count.
// If step counting is disabled in Settings, shows a hint to enable it.
void step_app_create(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif
