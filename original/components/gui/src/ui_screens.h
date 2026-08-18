// Internal header: top-level screen management for the gui component.
// Not exported to other components.

#pragma once
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif

void init_theme(void);
void load_screen(lv_obj_t* current_screen, lv_obj_t* next_screen,
                 lv_screen_load_anim_t anim);
lv_obj_t* active_screen_get(void);
lv_obj_t* get_main_screen(void);

// Called from ui_tileview after the tileview is created — registers it as
// the "main screen" returned by get_main_screen().
void ui_screens_set_main(lv_obj_t* main_screen_obj);

#ifdef __cplusplus
}
#endif
