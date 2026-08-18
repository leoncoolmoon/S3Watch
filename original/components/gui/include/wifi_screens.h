#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Opens the WiFi scan screen as a child of parent.
// When done (connected or cancelled) it deletes itself.
void wifi_scan_screen_open(lv_obj_t *parent);

// Opens the NTP server settings screen.
void ntp_settings_screen_open(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif
