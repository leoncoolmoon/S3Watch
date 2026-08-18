#include "setting_watchface_screen.h"
#include "ui.h"
#include "ui_fonts.h"
#include "settings.h"
#include "watchface.h"
#include "app_picker.h"
#include "esp_log.h"

static lv_obj_t* s_screen;
static lv_obj_t* s_roller_face;
static lv_obj_t* s_roller_bg;
static const char* TAG = "WatchfaceSettings";

static void on_delete(lv_event_t* e)
{
    (void)e;
    s_screen = NULL;
    s_roller_face = NULL;
    s_roller_bg = NULL;
}

static void screen_events(lv_event_t* e)
{
    if (lv_event_get_code(e) == LV_EVENT_GESTURE) {
        if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_RIGHT) {
            lv_indev_wait_release(lv_indev_active());
            ui_dynamic_subtile_close();
            s_screen = NULL;
        }
    }
}

static void on_face_change(lv_event_t* e)
{
    (void)e;
    if (!s_roller_face) return;
    int sel = (int)lv_roller_get_selected(s_roller_face);
    ESP_LOGI(TAG, "Watch face selected: %d", sel);
    settings_set_watchface_style(sel);
    watchface_rebuild();
}

static void on_bg_change(lv_event_t* e)
{
    (void)e;
    if (!s_roller_bg) return;
    int sel = (int)lv_roller_get_selected(s_roller_bg);
    ESP_LOGI(TAG, "Background selected: %d", sel);
    settings_set_watchface_bg(sel);
    watchface_rebuild();
    app_picker_refresh_bg();
}

void setting_watchface_screen_create(lv_obj_t* parent)
{
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_text_color(&style, lv_color_white());
    lv_style_set_bg_color(&style, lv_color_black());
    lv_style_set_bg_opa(&style, LV_OPA_COVER);

    s_screen = lv_obj_create(parent);
    lv_obj_remove_style_all(s_screen);
    lv_obj_add_style(s_screen, &style, 0);
    lv_obj_set_size(s_screen, lv_pct(100), lv_pct(100));
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_screen, screen_events, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(s_screen, on_delete, LV_EVENT_DELETE, NULL);

    lv_obj_t* title = lv_label_create(s_screen);
    lv_obj_set_style_text_font(title, &font_bold_32, 0);
    lv_label_set_text(title, "Watch Face");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    lv_obj_t* content = lv_obj_create(s_screen);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, lv_pct(100), lv_pct(80));
    lv_obj_add_flag(content, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(content, LV_ALIGN_BOTTOM_MID, 0, 0);

    // Face picker
    lv_obj_t* lbl_face = lv_label_create(content);
    lv_obj_set_style_text_font(lbl_face, &font_normal_28, 0);
    lv_obj_set_style_text_color(lbl_face, lv_color_hex(0xc0c0c0), 0);
    lv_label_set_text(lbl_face, "Face");

    s_roller_face = lv_roller_create(content);
    // Build the roller options from the watchface registry so this UI stays
    // in sync with whatever faces are compiled in (Phase 3 modularization).
    {
        int n = watchface_get_count();
        if (n < 1) n = 1;
        // Each entry: up to ~24 chars name + '\n' separator. 32B per entry is safe.
        char opts[32 * 8] = {0};
        size_t pos = 0;
        for (int i = 0; i < n; i++) {
            const char *name = watchface_get_name(i);
            int written = snprintf(opts + pos, sizeof(opts) - pos,
                                   (i == 0) ? "%s" : "\n%s", name);
            if (written < 0 || (size_t)written >= sizeof(opts) - pos) break;
            pos += (size_t)written;
        }
        lv_roller_set_options(s_roller_face, opts, LV_ROLLER_MODE_NORMAL);
        lv_roller_set_visible_row_count(s_roller_face, (n < 3) ? n : 3);
    }
    lv_roller_set_selected(s_roller_face, (uint16_t)settings_get_watchface_style(), LV_ANIM_OFF);
    lv_obj_set_style_text_font(s_roller_face, &font_normal_32, 0);
    lv_obj_set_style_text_color(s_roller_face, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_roller_face, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_roller_face, lv_color_white(), LV_PART_SELECTED);
    lv_obj_set_style_bg_color(s_roller_face, lv_color_hex(0x333333), LV_PART_SELECTED);
    lv_obj_add_event_cb(s_roller_face, on_face_change, LV_EVENT_VALUE_CHANGED, NULL);

    // Background picker
    lv_obj_t* lbl_bg = lv_label_create(content);
    lv_obj_set_style_text_font(lbl_bg, &font_normal_28, 0);
    lv_obj_set_style_text_color(lbl_bg, lv_color_hex(0xc0c0c0), 0);
    lv_label_set_text(lbl_bg, "Background");

    s_roller_bg = lv_roller_create(content);
    lv_roller_set_options(s_roller_bg, "BG 1\nBG 2\nBG 3", LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(s_roller_bg, 3);
    lv_roller_set_selected(s_roller_bg, (uint16_t)settings_get_watchface_bg(), LV_ANIM_OFF);
    lv_obj_set_style_text_font(s_roller_bg, &font_normal_32, 0);
    lv_obj_set_style_text_color(s_roller_bg, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_roller_bg, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_roller_bg, lv_color_white(), LV_PART_SELECTED);
    lv_obj_set_style_bg_color(s_roller_bg, lv_color_hex(0x333333), LV_PART_SELECTED);
    lv_obj_add_event_cb(s_roller_bg, on_bg_change, LV_EVENT_VALUE_CHANGED, NULL);
}
