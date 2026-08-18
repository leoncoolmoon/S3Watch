#include "setting_time_format_screen.h"
#include "ui.h"
#include "ui_fonts.h"
#include "settings.h"
#include "esp_log.h"

static lv_obj_t* s_screen;
static lv_obj_t* s_switch;
static const char* TAG = "TimeFormatSettings";

static void on_delete(lv_event_t* e)
{
    (void)e;
    ESP_LOGI(TAG, "Time format screen deleted");
    s_screen = NULL;
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

static void toggle(lv_event_t* e)
{
    (void)e;
    bool is_24h = lv_obj_has_state(s_switch, LV_STATE_CHECKED);
    settings_set_time_24h(is_24h);
}

void setting_time_format_screen_create(lv_obj_t* parent)
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
    lv_label_set_text(title, "Time Format");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    lv_obj_t* content = lv_obj_create(s_screen);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, lv_pct(100), lv_pct(85));
    lv_obj_add_flag(content, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_pad_all(content, 24, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(content, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_t* row = lv_obj_create(content);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* lbl = lv_label_create(row);
    lv_obj_set_style_text_font(lbl, &font_normal_28, 0);
    lv_label_set_text(lbl, "24-hour clock");

    s_switch = lv_switch_create(row);
    lv_obj_set_size(s_switch, 120, 50);
    if (settings_get_time_24h()) lv_obj_add_state(s_switch, LV_STATE_CHECKED);
    else lv_obj_clear_state(s_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_switch, toggle, LV_EVENT_VALUE_CHANGED, NULL);
}
