#pragma once
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif

// Build the SignalK alerts tile (severity-coded list of active alarms +
// status line). Call once when the tile is created; the screen registers
// its own 1 Hz task_coordinator subscriber to repaint itself.
void signalk_alerts_create(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif
