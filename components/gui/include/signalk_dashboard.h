#pragma once
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif

// Build the SignalK dashboard tile (4-cell value grid + status line).
// Call once when the tile is created; the dashboard registers its own
// 1 Hz task_coordinator subscriber to repaint itself.
void signalk_dashboard_create(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif
