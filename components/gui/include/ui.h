#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "bsp/esp32_s3_touch_amoled_2_06.h"

    // Dynamic tile management (right of controls tile)
    lv_obj_t* ui_dynamic_tile_acquire(void);
    void ui_dynamic_tile_show(void);
    void ui_dynamic_tile_close(void);

    // App tile management (right of app picker tile, row 2)
    lv_obj_t* ui_app_tile_acquire(void);
    void ui_app_tile_show(void);
    void ui_app_tile_close(void);

    // Marks the app just launched into the (1,2) tile as the SignalK
    // dashboard, so the tileview only brings WiFi/WS up for that app — not
    // for every app that shares the slot. Call once, right after creating
    // the dashboard. Cleared automatically on the next ui_app_tile_acquire().
    void ui_app_tile_mark_signalk(void);

    // Second-level dynamic tile (to the right of the first dynamic tile)
    lv_obj_t* ui_dynamic_subtile_acquire(void);
    void ui_dynamic_subtile_show(void);
    void ui_dynamic_subtile_close(void);

    void ui_init(void);
    void ui_task(void* pvParameters);

    // Accessor for the main style
    lv_style_t* ui_get_main_style(void);

#ifdef __cplusplus
}
#endif
