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

static void face2_build(lv_obj_t* c) {
    label_hour = label_minute = label_second = label_colon = label_ampm = NULL;
    label_date = label_weekday = NULL;
    img_battery = lbl_batt_pct = lbl_charge_icon = NULL;

    add_background(c);

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

static void face2_update_time(void) {
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
        lv_label_set_text_fmt(label_date, "%02d/%02d/%04d",
                              rtc_get_month(), rtc_get_day(), rtc_get_year());
    }
    if (label_weekday) {
        lv_label_set_text(label_weekday, rtc_get_weekday_string());
    }
}

static void face2_update_power(bool vbus_in, bool charging, int battery_percent) {
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

const watchface_iface_t watchface_face2 = {
    .name         = "Face 2",
    .build        = face2_build,
    .update_time  = face2_update_time,
    .update_power = face2_update_power,
};
