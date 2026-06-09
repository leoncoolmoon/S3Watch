#include "setting_alarm_timeout_screen.h"
#include "ui.h"
#include "ui_fonts.h"
#include "settings.h"
#include "esp_log.h"
#include "settings_menu_screen.h"
#include <stdio.h>
#include <stdint.h>

// Auto-silence timeout for a ringing alarm. Mirrors setting_timeout_screen.c
// (discrete option list) but writes settings_set_alarm_timeout_min (minutes).

static lv_obj_t* satimeout_screen;
static lv_obj_t* satimeout_content;
static void on_delete(lv_event_t* e);
static const char* TAG = "AlarmTimeout";

static const int s_minutes[] = { 1, 5, 10, 15, 20, 30 };

static void refresh_checked(void)
{
    if (!satimeout_content) return;
    int cur = settings_get_alarm_timeout_min();
    lv_obj_t* selected = NULL;
    for (uint32_t i = 0; i < lv_obj_get_child_count(satimeout_content); ++i) {
        lv_obj_t* row = lv_obj_get_child(satimeout_content, i);
        int val = (int)(intptr_t)lv_obj_get_user_data(row);
        if (val == cur) {
            lv_obj_add_state(row, LV_STATE_CHECKED);
            selected = row;
        } else {
            lv_obj_remove_state(row, LV_STATE_CHECKED);
        }
    }
    if (selected) lv_obj_scroll_to_view(selected, LV_ANIM_ON);
}

static void screen_events(lv_event_t* e)
{
    if (lv_event_get_code(e) == LV_EVENT_GESTURE) {
        if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_RIGHT) {
            lv_indev_wait_release(lv_indev_active());
            ui_dynamic_subtile_close();
            satimeout_screen = NULL;
        }
    }
    else if (lv_event_get_code(e) == LV_EVENT_SCREEN_LOADED) {
        refresh_checked();
    }
}

static void choose(lv_event_t* e)
{
    int val = (int)(intptr_t)lv_event_get_user_data(e);
    settings_set_alarm_timeout_min(val);
    int current = settings_get_alarm_timeout_min();
    lv_obj_t* parent = lv_obj_get_parent(lv_event_get_target(e));
    for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
        lv_obj_t* row = lv_obj_get_child(parent, i);
        int v = (int)(intptr_t)lv_obj_get_user_data(row);
        lv_obj_remove_state(row, LV_STATE_CHECKED);
        if (v == current) lv_obj_add_state(row, LV_STATE_CHECKED);
    }
}

static lv_obj_t* make_opt(lv_obj_t* parent, const char* txt, int val)
{
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, 60);
    lv_obj_set_style_bg_opa(row, LV_OPA_10, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_pad_all(row, 12, 0);
    lv_obj_set_style_margin_bottom(row, 10, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, choose, LV_EVENT_CLICKED, (void*)(intptr_t)val);
    lv_obj_set_user_data(row, (void*)(intptr_t)val);
    lv_obj_t* l = lv_label_create(row);
    lv_obj_set_style_text_font(l, &font_bold_32, 0);
    lv_label_set_text(l, txt);
    return row;
}

void setting_alarm_timeout_screen_create(lv_obj_t* parent)
{
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_text_color(&style, lv_color_white());
    lv_style_set_bg_color(&style, lv_color_black());
    lv_style_set_bg_opa(&style, LV_OPA_COVER);

    satimeout_screen = lv_obj_create(parent);
    lv_obj_remove_style_all(satimeout_screen);
    lv_obj_add_style(satimeout_screen, &style, 0);
    lv_obj_set_size(satimeout_screen, lv_pct(100), lv_pct(100));
    lv_obj_add_event_cb(satimeout_screen, screen_events, LV_EVENT_GESTURE, NULL);
    lv_obj_add_flag(satimeout_screen, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(satimeout_screen, LV_OBJ_FLAG_USER_1);
    lv_obj_add_event_cb(satimeout_screen, on_delete, LV_EVENT_DELETE, NULL);

    lv_obj_t* hdr = lv_obj_create(satimeout_screen);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_t* title = lv_label_create(hdr);
    lv_obj_set_style_text_font(title, &font_bold_32, 0);
    lv_label_set_text(title, "Alarm Timeout");

    satimeout_content = lv_obj_create(satimeout_screen);
    lv_obj_remove_style_all(satimeout_content);
    lv_obj_set_size(satimeout_content, lv_pct(100), lv_pct(80));
    lv_obj_add_flag(satimeout_content, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_pad_top(satimeout_content, 80, 0);
    lv_obj_set_style_pad_bottom(satimeout_content, 10, 0);
    lv_obj_set_style_pad_left(satimeout_content, 12, 0);
    lv_obj_set_style_pad_right(satimeout_content, 12, 0);
    lv_obj_set_flex_flow(satimeout_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(satimeout_content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(satimeout_content, LV_DIR_VER);

    for (uint32_t i = 0; i < sizeof(s_minutes) / sizeof(s_minutes[0]); ++i) {
        char buf[12];
        snprintf(buf, sizeof(buf), "%d min", s_minutes[i]);
        make_opt(satimeout_content, buf, s_minutes[i]);
    }

    for (uint32_t i = 0; i < lv_obj_get_child_count(satimeout_content); ++i) {
        lv_obj_t* row = lv_obj_get_child(satimeout_content, i);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x438bff), LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(row, 255, LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(row, lv_color_white(), LV_PART_MAIN | LV_STATE_CHECKED);
    }

    refresh_checked();
}

static void on_delete(lv_event_t* e)
{
    (void)e;
    ESP_LOGI(TAG, "Alarm timeout screen deleted");
    satimeout_screen = NULL;
}

lv_obj_t* setting_alarm_timeout_screen_get(void)
{
    if (!satimeout_screen) {
        bsp_display_lock(0);
        setting_alarm_timeout_screen_create(NULL);
        bsp_display_unlock();
    }
    return satimeout_screen;
}
