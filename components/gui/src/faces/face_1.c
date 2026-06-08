// Face 1: large stacked HH/MM (coloured digits), centered SS, AM/PM under
// the minute, mm/dd date + short weekday on the right, battery top-left.

#include "watchface_iface.h"
#include "rtc_lib.h"
#include "settings.h"
#include "ui_fonts.h"
#include "lvgl.h"

static lv_obj_t* label_hour;
static lv_obj_t* label_minute;
static lv_obj_t* label_second;
static lv_obj_t* label_ampm;
static lv_obj_t* label_date;
static lv_obj_t* label_weekday;
static lv_obj_t* img_battery;
static lv_obj_t* lbl_batt_pct;
static lv_obj_t* lbl_charge_icon;

static void face1_build(lv_obj_t* c) {
    // Reset all label pointers so the update callback can safely no-op on
    // any that might persist from a prior build.
    label_hour = label_minute = label_second = label_ampm = NULL;
    label_date = label_weekday = NULL;
    img_battery = lbl_batt_pct = lbl_charge_icon = NULL;

    watchface_add_background(c);

    label_hour = lv_label_create(c);
    lv_obj_set_y(label_hour, -95);
    lv_obj_set_align(label_hour, LV_ALIGN_CENTER);
    lv_label_set_text(label_hour, "--");
    lv_obj_set_style_text_letter_space(label_hour, 1, 0);
    lv_obj_set_style_text_font(label_hour, &font_numbers_160, 0);
    lv_obj_set_style_text_color(label_hour, lv_color_hex(0xF0B000), LV_PART_MAIN | LV_STATE_DEFAULT);

    label_minute = lv_label_create(c);
    lv_obj_set_y(label_minute, 105);
    lv_obj_set_align(label_minute, LV_ALIGN_CENTER);
    lv_label_set_text(label_minute, "--");
    lv_obj_set_style_text_letter_space(label_minute, 1, 0);
    lv_obj_set_style_text_font(label_minute, &font_numbers_160, 0);
    lv_obj_set_style_text_color(label_minute, lv_color_hex(0x90F090), LV_PART_MAIN | LV_STATE_DEFAULT);

    label_ampm = lv_label_create(c);
    lv_label_set_text(label_ampm, "AM");
    lv_obj_set_style_text_letter_space(label_ampm, 3, 0);
    lv_obj_set_style_text_font(label_ampm, &font_bold_32, 0);
    lv_obj_set_style_text_color(label_ampm, lv_color_hex(0xc0c0c0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align_to(label_ampm, label_minute, LV_ALIGN_OUT_BOTTOM_MID, 0, -25);
    if (settings_get_time_24h()) lv_obj_add_flag(label_ampm, LV_OBJ_FLAG_HIDDEN);

    label_second = lv_label_create(c);
    lv_obj_set_align(label_second, LV_ALIGN_CENTER);
    lv_label_set_text(label_second, "--");
    lv_obj_set_style_text_letter_space(label_second, 1, 0);
    lv_obj_set_style_text_font(label_second, &font_numbers_80, 0);
    lv_obj_set_style_text_color(label_second, lv_color_hex(0x909090), LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t* date_cont = lv_obj_create(c);
    lv_obj_remove_style_all(date_cont);
    lv_obj_set_size(date_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_x(date_cont, -20);
    lv_obj_set_align(date_cont, LV_ALIGN_RIGHT_MID);
    lv_obj_set_flex_flow(date_cont, LV_FLEX_FLOW_COLUMN);

    label_date = lv_label_create(date_cont);
    lv_label_set_text(label_date, "--/--");
    lv_obj_set_style_text_letter_space(label_date, 1, 0);
    lv_obj_set_style_text_font(label_date, &font_normal_32, 0);
    lv_obj_set_style_text_color(label_date, lv_color_hex(0xc0c0c0), LV_PART_MAIN | LV_STATE_DEFAULT);

    label_weekday = lv_label_create(date_cont);
    lv_label_set_text(label_weekday, "---");
    lv_obj_set_style_text_letter_space(label_weekday, 3, 0);
    lv_obj_set_style_text_font(label_weekday, &font_bold_32, 0);
    lv_obj_set_style_text_color(label_weekday, lv_color_hex(0xc0c0c0), LV_PART_MAIN | LV_STATE_DEFAULT);

    watchface_build_battery_widget(c, &img_battery, &lbl_batt_pct, &lbl_charge_icon);
}

static void face1_update_time(void) {
    // Caller (watchface.c dispatcher) already called rtc_refresh_now().
    watchface_update_hour_label(label_hour, label_ampm);
    if (label_minute) lv_label_set_text_fmt(label_minute, "%02d", rtc_get_minute());
    if (label_second) lv_label_set_text_fmt(label_second, "%02d", rtc_get_second());

    if (label_date) {
        lv_label_set_text_fmt(label_date, "%02d/%02d", rtc_get_month(), rtc_get_day());
    }
    if (label_weekday) {
        lv_label_set_text(label_weekday, rtc_get_weekday_short_string());
    }
}

static void face1_update_power(bool vbus_in, bool charging, int battery_percent) {
    watchface_update_battery_widget(img_battery, lbl_batt_pct, lbl_charge_icon,
                                    vbus_in, charging, battery_percent);
}

const watchface_iface_t watchface_face1 = {
    .name         = "Face 1",
    .build        = face1_build,
    .update_time  = face1_update_time,
    .update_power = face1_update_power,
};
