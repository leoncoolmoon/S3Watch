// Face 2: horizontal HH:MM with subscript AM/PM and SS in a right-side
// column, full mm/dd/yyyy date, full weekday name, battery top-left.

#include "watchface_iface.h"
#include "rtc_lib.h"
#include "settings.h"
#include "ui_fonts.h"
#include "lvgl.h"

static lv_obj_t* label_hour;
static lv_obj_t* label_minute;
static lv_obj_t* label_second;
static lv_obj_t* label_colon;
static lv_obj_t* label_ampm;
static lv_obj_t* label_date;
static lv_obj_t* label_weekday;
static lv_obj_t* img_battery;
static lv_obj_t* lbl_batt_pct;
static lv_obj_t* lbl_charge_icon;

static void face2_build(lv_obj_t* c) {
    label_hour = label_minute = label_second = label_colon = label_ampm = NULL;
    label_date = label_weekday = NULL;
    img_battery = lbl_batt_pct = lbl_charge_icon = NULL;

    watchface_add_background(c);

    // Flex row: HH:MM with ss bottom-aligned (subscript effect)
    lv_obj_t* time_row = lv_obj_create(c);
    lv_obj_remove_style_all(time_row);
    lv_obj_remove_flag(time_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(time_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(time_row, 0, 0);
    lv_obj_set_style_pad_column(time_row, -6, 0);
    lv_obj_set_flex_flow(time_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_align(time_row, LV_ALIGN_CENTER);
    lv_obj_set_y(time_row, -50);

    label_hour = lv_label_create(time_row);
    lv_label_set_text(label_hour, "--");
    lv_obj_set_style_text_font(label_hour, &font_numbers_120, 0);
    lv_obj_set_style_text_color(label_hour, lv_color_hex(0xc0c0c0), 0);
    lv_obj_set_style_text_letter_space(label_hour, -6, 0);

    label_colon = lv_label_create(time_row);
    lv_label_set_text(label_colon, ":");
    lv_obj_set_style_text_font(label_colon, &font_numbers_120, 0);
    lv_obj_set_style_text_color(label_colon, lv_color_hex(0xc0c0c0), 0);

    label_minute = lv_label_create(time_row);
    lv_label_set_text(label_minute, "--");
    lv_obj_set_style_text_font(label_minute, &font_numbers_120, 0);
    lv_obj_set_style_text_color(label_minute, lv_color_hex(0xc0c0c0), 0);
    lv_obj_set_style_text_letter_space(label_minute, -6, 0);

    // Side column: AM/PM pinned top-right, SS pinned bottom-right.
    // Height = font_numbers_120.line_height (130px) so the column spans exactly
    // from the top of the big digits to their bottom when cross-aligned END.
    lv_obj_t* side_col = lv_obj_create(time_row);
    lv_obj_remove_style_all(side_col);
    lv_obj_remove_flag(side_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(side_col, 70, 130);
    lv_obj_set_style_pad_all(side_col, 0, 0);

    label_ampm = lv_label_create(side_col);
    lv_label_set_text(label_ampm, "AM");
    lv_obj_set_style_text_font(label_ampm, &font_bold_42, 0);
    lv_obj_set_style_text_color(label_ampm, lv_color_hex(0xc0c0c0), 0);
    lv_obj_align(label_ampm, LV_ALIGN_TOP_RIGHT, 0, 3);
    if (settings_get_time_24h()) lv_obj_add_flag(label_ampm, LV_OBJ_FLAG_HIDDEN);

    label_second = lv_label_create(side_col);
    lv_label_set_text(label_second, "--");
    lv_obj_set_style_text_font(label_second, &font_bold_42, 0);
    lv_obj_set_style_text_color(label_second, lv_color_hex(0xc0c0c0), 0);
    lv_obj_align(label_second, LV_ALIGN_BOTTOM_RIGHT, 0, -20);

    // Date: mm/dd/yyyy
    label_date = lv_label_create(c);
    lv_obj_align_to(label_date, time_row, LV_ALIGN_OUT_BOTTOM_MID, 0, 16);
    lv_label_set_text(label_date, "--/--/----");
    lv_obj_set_style_text_font(label_date, &font_normal_32, 0);
    lv_obj_set_style_text_color(label_date, lv_color_hex(0xc0c0c0), 0);

    // Weekday: full name
    label_weekday = lv_label_create(c);
    lv_obj_align_to(label_weekday, label_date, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
    lv_label_set_text(label_weekday, "---");
    lv_obj_set_style_text_font(label_weekday, &font_bold_32, 0);
    lv_obj_set_style_text_color(label_weekday, lv_color_hex(0xc0c0c0), 0);

    // Battery — same position as face 1
    watchface_build_battery_widget(c, &img_battery, &lbl_batt_pct, &lbl_charge_icon);
}

static void face2_update_time(void) {
    watchface_update_hour_label(label_hour, label_ampm);
    if (label_minute) lv_label_set_text_fmt(label_minute, "%02d", rtc_get_minute());
    if (label_second) lv_label_set_text_fmt(label_second, "%02d", rtc_get_second());

    if (label_date) {
        lv_label_set_text_fmt(label_date, "%02d/%02d/%04d",
                              rtc_get_month(), rtc_get_day(), rtc_get_year());
    }
    if (label_weekday) {
        lv_label_set_text(label_weekday, rtc_get_weekday_string());
    }
}

static void face2_update_power(bool vbus_in, bool charging, int battery_percent) {
    watchface_update_battery_widget(img_battery, lbl_batt_pct, lbl_charge_icon,
                                    vbus_in, charging, battery_percent);
}

const watchface_iface_t watchface_face2 = {
    .name         = "Face 2",
    .build        = face2_build,
    .update_time  = face2_update_time,
    .update_power = face2_update_power,
};
