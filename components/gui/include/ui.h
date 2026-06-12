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

    // Open the Alarm app into the row-2 app tile and navigate to it. Used by
    // the display-wake hook so a ringing alarm comes up on the alarm screen
    // (with Dismiss) rather than the watchface. Must run on the LVGL thread.
    void ui_open_alarm_app(void);

    // Open the Music app straight to Now Playing. Used by the display-wake hook
    // so an actively-playing track comes up ready to pause/stop. LVGL thread.
    void ui_open_music_app(void);

    void ui_init(void);
    // pvParameters: optional SemaphoreHandle_t, given once the tileview is
    // built and the UI plumbing is live (boot_manager blocks the boot-splash
    // handoff on it). The built main screen is NOT auto-loaded — the boot
    // splash stays active until boot_splash_handoff(). Task self-deletes.
    void ui_task(void* pvParameters);

    // Accessor for the main style
    lv_style_t* ui_get_main_style(void);

#ifdef __cplusplus
}
#endif
