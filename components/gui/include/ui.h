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
