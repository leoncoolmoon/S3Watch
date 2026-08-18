// Boot splash — wordmark + spinner + firmware version, shown by boot_manager
// while the startup tone plays and the tileview is built behind it. See
// boot_splash.h for the lifecycle contract.

#include "boot_splash.h"
#include "ui_fonts.h"
#include "ui_screens.h"
#include "bsp/esp-bsp.h"
#include "esp_app_desc.h"
#include "lvgl.h"

static lv_obj_t *s_splash = NULL;

void boot_splash_show(void)
{
    bsp_display_lock(0);

    s_splash = lv_obj_create(NULL);   // a screen (no parent)
    lv_obj_remove_style_all(s_splash);
    lv_obj_set_style_bg_color(s_splash, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_splash, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_splash, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *wordmark = lv_label_create(s_splash);
    lv_obj_set_style_text_font(wordmark, &font_bold_42, 0);
    lv_obj_set_style_text_color(wordmark, lv_color_white(), 0);
    lv_label_set_text(wordmark, "S3Watch");
    lv_obj_align(wordmark, LV_ALIGN_CENTER, 0, -60);

    lv_obj_t *spinner = lv_spinner_create(s_splash);
    lv_spinner_set_anim_params(spinner, 1000, 200);
    lv_obj_set_size(spinner, 56, 56);
    lv_obj_set_style_arc_width(spinner, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x4090FF), LV_PART_INDICATOR);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 40);

    lv_obj_t *version = lv_label_create(s_splash);
    lv_obj_set_style_text_font(version, &font_normal_26, 0);
    lv_obj_set_style_text_color(version, lv_color_hex(0x606060), 0);
    lv_label_set_text_fmt(version, "v%s", esp_app_get_description()->version);
    lv_obj_align(version, LV_ALIGN_BOTTOM_MID, 0, -40);

    lv_screen_load(s_splash);
    // Synchronous first frame: the splash must be visible the moment the
    // backlight is up, not one LVGL-timer tick later.
    lv_refr_now(NULL);

    bsp_display_unlock();
}

// One-shot: the source screen of a slide animation must outlive it (300 ms) —
// same reasoning as the tileview's async deletes.
static void splash_delete_cb(lv_timer_t *t)
{
    (void)t;
    if (s_splash) {
        lv_obj_del(s_splash);
        s_splash = NULL;
    }
}

void boot_splash_handoff(void)
{
    if (!s_splash) return;
    bsp_display_lock(0);
    // Slide the watchface up over the splash — same motion language as
    // back-navigation (load_screen also tracks active_screen for us).
    load_screen(NULL, get_main_screen(), LV_SCR_LOAD_ANIM_OVER_TOP);
    lv_timer_t *t = lv_timer_create(splash_delete_cb, 400, NULL);
    lv_timer_set_repeat_count(t, 1);
    bsp_display_unlock();
}
