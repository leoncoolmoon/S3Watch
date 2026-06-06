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

static void add_background(lv_obj_t* c) {
    lv_obj_t* image = lv_image_create(c);
    int bg = settings_get_watchface_bg();
    if (bg == 1) {
        LV_IMAGE_DECLARE(background_wf);
        lv_image_set_src(image, &background_wf);
    } else if (bg == 2) {
        LV_IMAGE_DECLARE(background_wf_3);
        lv_image_set_src(image, &background_wf_3);
    } else {
        LV_IMAGE_DECLARE(background_wf_2);
        lv_image_set_src(image, &background_wf_2);
    }
    lv_obj_set_align(image, LV_ALIGN_CENTER);
}

static void face1_build(lv_obj_t* c) {
    // Reset all label pointers so the update callback can safely no-op on
    // any that might persist from a prior build.
    label_hour = label_minute = label_second = label_ampm = NULL;
    label_date = label_weekday = NULL;
    img_battery = lbl_batt_pct = lbl_charge_icon = NULL;

    add_background(c);

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

    extern const lv_image_dsc_t image_battery_icon;
    img_battery = lv_image_create(c);
    lv_image_set_src(img_battery, &image_battery_icon);
    lv_obj_set_align(img_battery, LV_ALIGN_TOP_MID);
    lv_obj_set_x(img_battery, -100);
    lv_obj_set_style_img_recolor_opa(img_battery, LV_OPA_COVER, 0);
    lv_obj_set_style_img_recolor(img_battery, lv_color_hex(0x909090), 0);

    lbl_batt_pct = lv_label_create(c);
    lv_obj_align_to(lbl_batt_pct, img_battery, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    lv_obj_set_style_text_color(lbl_batt_pct, lv_color_hex(0xc0c0c0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(lbl_batt_pct, "--%");
    lv_obj_set_style_text_font(lbl_batt_pct, &font_normal_26, 0);

    lbl_charge_icon = lv_label_create(img_battery);
#ifdef LV_SYMBOL_CHARGE
    lv_label_set_text(lbl_charge_icon, LV_SYMBOL_CHARGE);
#else
    lv_label_set_text(lbl_charge_icon, "⚡");
#endif
    lv_obj_center(lbl_charge_icon);
    lv_obj_set_style_text_font(lbl_charge_icon, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(lbl_charge_icon, lv_color_white(), 0);
    lv_obj_add_flag(lbl_charge_icon, LV_OBJ_FLAG_HIDDEN);
}

static void face1_update_time(void) {
    // Caller (watchface.c dispatcher) already called rtc_refresh_now().
    int hour = rtc_get_hour();
    if (settings_get_time_24h()) {
        if (label_hour) lv_label_set_text_fmt(label_hour, "%02d", hour);
        if (label_ampm) lv_obj_add_flag(label_ampm, LV_OBJ_FLAG_HIDDEN);
    } else {
        int h12 = hour % 12;
        if (h12 == 0) h12 = 12;
        if (label_hour) lv_label_set_text_fmt(label_hour, "%02d", h12);
        if (label_ampm) {
            lv_label_set_text(label_ampm, hour >= 12 ? "PM" : "AM");
            lv_obj_clear_flag(label_ampm, LV_OBJ_FLAG_HIDDEN);
        }
    }
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
    if (!img_battery) return;
    lv_color_t col = lv_color_hex(0x909090);
    if (vbus_in)  col = lv_color_hex(0x00BFFF);
    if (charging) col = lv_color_hex(0x00FF00);
    lv_obj_set_style_img_recolor(img_battery, col, 0);
    if (lbl_batt_pct) {
        if (battery_percent >= 0 && battery_percent <= 100) {
            static char buf[8];
            lv_snprintf(buf, sizeof(buf), "%d%%", battery_percent);
            lv_label_set_text(lbl_batt_pct, buf);
        } else {
            lv_label_set_text(lbl_batt_pct, "--%");
        }
    }
    if (lbl_charge_icon) {
        if (vbus_in || charging) lv_obj_clear_flag(lbl_charge_icon, LV_OBJ_FLAG_HIDDEN);
        else                     lv_obj_add_flag(lbl_charge_icon, LV_OBJ_FLAG_HIDDEN);
    }
}

const watchface_iface_t watchface_face1 = {
    .name         = "Face 1",
    .build        = face1_build,
    .update_time  = face1_update_time,
    .update_power = face1_update_power,
};
