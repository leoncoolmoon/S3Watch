#include "setting_time_screen.h"
#include "ui.h"
#include "ui_fonts.h"
#include "rtc_lib.h"
#include "esp_log.h"
#include <time.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "SetTime";
static lv_obj_t *stime_screen;
static void on_delete(lv_event_t *e);

static lv_obj_t *roller_hour, *roller_min, *roller_sec;
static lv_obj_t *roller_year, *roller_mon, *roller_day;

static const char *month_opts = "Jan\nFeb\nMar\nApr\nMay\nJun\nJul\nAug\nSep\nOct\nNov\nDec";

static void save(lv_event_t *e)
{
    (void)e;
    struct tm t = {0};
    // -1 = let mktime decide DST from the TZ rule, so an entered local time in
    // a DST-active window doesn't get shifted by +1 hour on the round-trip
    // through UTC and back.
    t.tm_isdst = -1;
    t.tm_hour = lv_roller_get_selected(roller_hour);
    t.tm_min  = lv_roller_get_selected(roller_min);
    t.tm_sec  = lv_roller_get_selected(roller_sec);
    t.tm_year = 125 + (int)lv_roller_get_selected(roller_year);
    t.tm_mon  = lv_roller_get_selected(roller_mon);
    t.tm_mday = (int)lv_roller_get_selected(roller_day) + 1;

    static const int dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int max_day = dim[t.tm_mon];
    int y = t.tm_year + 1900;
    if (t.tm_mon == 1 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) max_day = 29;
    if (t.tm_mday > max_day) t.tm_mday = max_day;

    // User-entered values are local time under the current TZ. The helper
    // converts to UTC and writes through to the canonical UTC path.
    rtc_set_time_from_local(&t);
    ESP_LOGI(TAG, "Time set (local): %04d-%02d-%02d %02d:%02d:%02d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);
    ui_dynamic_subtile_close();
    stime_screen = NULL;
}

static void screen_events(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_GESTURE &&
        lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_active());
        ui_dynamic_subtile_close();
        stime_screen = NULL;
    }
}

static lv_obj_t *make_roller_col(lv_obj_t *parent, const char *label_txt,
                                  const char *options, lv_roller_mode_t mode,
                                  uint16_t sel)
{
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_remove_style_all(col);
    lv_obj_add_flag(col, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 6, 0);

    lv_obj_t *lbl = lv_label_create(col);
    lv_obj_set_style_text_font(lbl, &font_bold_28, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_label_set_text(lbl, label_txt);

    lv_obj_t *roller = lv_roller_create(col);
    lv_roller_set_options(roller, options, mode);
    lv_roller_set_visible_row_count(roller, 3);
    lv_roller_set_selected(roller, sel, LV_ANIM_OFF);

    lv_obj_set_style_text_font(roller, &font_bold_28, 0);
    lv_obj_set_style_text_color(roller, lv_color_hex(0x888888), 0);
    lv_obj_set_style_bg_color(roller, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(roller, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(roller, 0, 0);

    lv_obj_set_style_text_font(roller, &font_bold_28, LV_PART_SELECTED);
    lv_obj_set_style_text_color(roller, lv_color_white(), LV_PART_SELECTED);
    lv_obj_set_style_bg_color(roller, lv_color_hex(0x333333), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(roller, LV_OPA_COVER, LV_PART_SELECTED);

    return roller;
}

static lv_obj_t *make_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return row;
}

void setting_time_screen_create(lv_obj_t *parent)
{
    // Initial roller values: current local time (PCF / system time is UTC;
    // localtime_r applies the user's TZ).
    struct tm cur = {0};
    time_t now = time(NULL);
    if (now > 0 && localtime_r(&now, &cur) == NULL) {
        cur = (struct tm){ .tm_year = 125, .tm_mon = 0, .tm_mday = 1 };
    }
    if (cur.tm_year < 125) {
        cur = (struct tm){ .tm_year = 125, .tm_mon = 0, .tm_mday = 1 };
    }

    static char hour_opts[24 * 4];
    static char minsec_opts[60 * 4];
    static char year_opts[75 * 6];
    static char day_opts[31 * 4];

    hour_opts[0] = minsec_opts[0] = year_opts[0] = day_opts[0] = '\0';
    for (int i = 0; i < 24; i++) {
        char tmp[8]; snprintf(tmp, sizeof(tmp), i < 23 ? "%02d\n" : "%02d", i);
        strncat(hour_opts, tmp, sizeof(hour_opts) - strlen(hour_opts) - 1);
    }
    for (int i = 0; i < 60; i++) {
        char tmp[8]; snprintf(tmp, sizeof(tmp), i < 59 ? "%02d\n" : "%02d", i);
        strncat(minsec_opts, tmp, sizeof(minsec_opts) - strlen(minsec_opts) - 1);
    }
    for (int y = 2025; y <= 2099; y++) {
        char tmp[8]; snprintf(tmp, sizeof(tmp), y < 2099 ? "%04d\n" : "%04d", y);
        strncat(year_opts, tmp, sizeof(year_opts) - strlen(year_opts) - 1);
    }
    for (int d = 1; d <= 31; d++) {
        char tmp[8]; snprintf(tmp, sizeof(tmp), d < 31 ? "%02d\n" : "%02d", d);
        strncat(day_opts, tmp, sizeof(day_opts) - strlen(day_opts) - 1);
    }

    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_text_color(&style, lv_color_white());
    lv_style_set_bg_color(&style, lv_color_black());
    lv_style_set_bg_opa(&style, LV_OPA_COVER);

    stime_screen = lv_obj_create(parent);
    lv_obj_remove_style_all(stime_screen);
    lv_obj_add_style(stime_screen, &style, 0);
    lv_obj_set_size(stime_screen, lv_pct(100), lv_pct(100));
    lv_obj_add_flag(stime_screen, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_flex_flow(stime_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(stime_screen, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(stime_screen, screen_events, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(stime_screen, on_delete, LV_EVENT_DELETE, NULL);

    lv_obj_t *title = lv_label_create(stime_screen);
    lv_obj_set_style_text_font(title, &font_bold_32, 0);
    lv_label_set_text(title, "Set Time");

    lv_obj_t *time_row = make_row(stime_screen);
    roller_hour = make_roller_col(time_row, "H",   hour_opts,   LV_ROLLER_MODE_INFINITE, cur.tm_hour);
    roller_min  = make_roller_col(time_row, "M",   minsec_opts, LV_ROLLER_MODE_INFINITE, cur.tm_min);
    roller_sec  = make_roller_col(time_row, "S",   minsec_opts, LV_ROLLER_MODE_INFINITE, cur.tm_sec);

    lv_obj_t *date_row = make_row(stime_screen);
    int year_sel = (cur.tm_year >= 125) ? (cur.tm_year - 125) : 0;
    roller_year = make_roller_col(date_row, "Year", year_opts,   LV_ROLLER_MODE_NORMAL,   year_sel);
    roller_mon  = make_roller_col(date_row, "Mon",  month_opts,  LV_ROLLER_MODE_INFINITE, cur.tm_mon);
    roller_day  = make_roller_col(date_row, "Day",  day_opts,    LV_ROLLER_MODE_INFINITE, cur.tm_mday > 0 ? cur.tm_mday - 1 : 0);

    lv_obj_t *btn_save = lv_btn_create(stime_screen);
    lv_obj_set_size(btn_save, 160, 60);
    lv_obj_add_event_cb(btn_save, save, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lsave = lv_label_create(btn_save);
    lv_obj_set_style_text_font(lsave, &font_bold_32, 0);
    lv_label_set_text(lsave, "Set");
    lv_obj_center(lsave);
}

static void on_delete(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Set time screen deleted");
    stime_screen = NULL;
}
