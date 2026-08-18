// Calendar — launched from the app picker into the row-2 app tile (1,2).
//
// Read-only monthly reference calendar built on LVGL's lv_calendar widget.
// Shows the current month with today highlighted; the built-in header
// arrows page to the previous/next month. No events or notifications —
// purely a reference view, rebuilt fresh each time the app is opened.

#include "calendar_app.h"
#include "ui_fonts.h"
#include "esp_log.h"
#include "lvgl.h"
#include <time.h>

static const char *TAG = "calendar_app";

void calendar_app_create(lv_obj_t *parent)
{
    lv_obj_t *screen = lv_obj_create(parent);
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, lv_pct(100), lv_pct(100));
    lv_obj_center(screen);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_GESTURE_BUBBLE);

    // Title
    lv_obj_t *title = lv_label_create(screen);
    lv_obj_set_style_text_font(title, &font_bold_32, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_pad_top(title, 16, 0);
    lv_obj_set_style_pad_bottom(title, 8, 0);
    lv_label_set_text(title, "Calendar");

    // Today's date drives both the highlight and the initially-shown month
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    int year  = lt.tm_year + 1900;
    int month = lt.tm_mon + 1;
    int day   = lt.tm_mday;

    lv_obj_t *cal = lv_calendar_create(screen);
    lv_obj_set_size(cal, lv_pct(94), lv_pct(80));
    lv_obj_add_flag(cal, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_bg_opa(cal, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(cal, lv_color_white(), 0);
    lv_obj_set_style_text_font(cal, &font_normal_26, 0);
    lv_obj_set_style_pad_row(cal, 6, 0);

    lv_calendar_set_today_date(cal, year, month, day);
    lv_calendar_set_month_shown(cal, year, month);

    // Highlight today with a filled tint (in addition to the widget's
    // built-in "today" border) so it stands out on the dark background.
    // Static — lv_calendar only stores the pointer, not a copy.
    static lv_calendar_date_t s_today;
    s_today.year  = (uint16_t)year;
    s_today.month = (int8_t)month;
    s_today.day   = (int8_t)day;
    lv_calendar_set_highlighted_dates(cal, &s_today, 1);

    lv_calendar_add_header_arrow(cal);

    // Day-grid styling: dark cells, light text, dimmed adjacent-month days
    lv_obj_t *btnm = lv_calendar_get_btnmatrix(cal);
    lv_obj_set_style_text_font(btnm, &font_normal_26, LV_PART_ITEMS);
    lv_obj_set_style_text_color(btnm, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_text_color(btnm, lv_color_hex(0x555555), LV_PART_ITEMS | LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(btnm, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btnm, lv_color_hex(0x1c1c1c), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(btnm, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_width(btnm, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(btnm, 8, LV_PART_ITEMS);
    lv_obj_set_style_pad_all(btnm, 2, LV_PART_ITEMS);

    ESP_LOGI(TAG, "showing %04d-%02d (today=%d)", year, month, day);
}
