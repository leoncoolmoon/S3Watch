// Top-level LVGL screen management. Owns the main_style, the current
// "active screen" pointer, and the get_main_screen() accessor that other
// gui modules use to refer to the tileview.

#include "ui_screens.h"
#include "ui.h"
#include "bsp/esp-bsp.h"

static lv_obj_t* main_screen   = NULL;
static lv_obj_t* active_screen = NULL;
static lv_style_t main_style;

void init_theme(void) {
  lv_style_init(&main_style);
  lv_style_set_text_color(&main_style, lv_color_white());
  lv_style_set_bg_color(&main_style, lv_color_black());
  lv_style_set_border_color(&main_style, lv_color_black());
}

lv_style_t* ui_get_main_style(void) { return &main_style; }

void load_screen(lv_obj_t* current_screen, lv_obj_t* next_screen,
                 lv_screen_load_anim_t anim)
{
  (void)current_screen;
  if (active_screen != next_screen) {
    bsp_display_lock(300);
    lv_screen_load_anim(next_screen, anim, 300, 0, false);
    bsp_display_unlock();
    active_screen = next_screen;
  }
}

lv_obj_t* active_screen_get(void) { return active_screen; }
lv_obj_t* get_main_screen(void)   { return main_screen; }

void ui_screens_set_main(lv_obj_t* main_screen_obj) {
  main_screen = main_screen_obj;
}
