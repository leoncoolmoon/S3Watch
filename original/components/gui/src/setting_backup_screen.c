#include "setting_backup_screen.h"
#include "ui.h"
#include "ui_fonts.h"
#include "settings.h"
#include <string.h>
#include "lvgl.h"
#include "esp_log.h"

static lv_obj_t* sbackup_screen;
static lv_obj_t* status_label;
static void on_delete(lv_event_t* e);
static const char* TAG = "BackupSettings";

static void toast_timer_cb(lv_timer_t* t)
{
    lv_obj_t* obj = (lv_obj_t*)lv_timer_get_user_data(t);
    if (obj) lv_obj_del(obj);
}

static void show_toast(const char* text)
{
    if (!text) return;
    lv_obj_t* toast = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(toast);
    lv_obj_set_style_bg_color(toast, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(toast, LV_OPA_80, 0);
    lv_obj_set_style_radius(toast, 12, 0);
    lv_obj_set_style_pad_all(toast, 10, 0);
    lv_obj_set_style_border_width(toast, 0, 0);
    lv_obj_set_style_min_width(toast, 120, 0);
    lv_obj_set_style_max_width(toast, lv_pct(90), 0);

    lv_obj_t* lbl = lv_label_create(toast);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_center(lbl);

    lv_obj_update_layout(toast);
    lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -20);

    // One-shot: lv_timer_create() defaults to infinite repeats, which would
    // fire toast_timer_cb again 1200ms after it already deleted `toast` —
    // a use-after-free once the freed lv_obj_t memory gets reused.
    lv_timer_t* toast_timer = lv_timer_create(toast_timer_cb, 1200, toast);
    lv_timer_set_repeat_count(toast_timer, 1);
}

static void refresh_status(void)
{
    if (!status_label) return;
    lv_label_set_text(status_label,
        settings_sd_backup_exists() ? "Backup found on SD card" : "No backup found on SD card");
}

static void screen_events(lv_event_t* e)
{
    if (lv_event_get_code(e) == LV_EVENT_GESTURE) {
        if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_RIGHT) {
            lv_indev_wait_release(lv_indev_active());
            ui_dynamic_subtile_close();
            sbackup_screen = NULL;
        }
    }
}

static void modal_btn_evt(lv_event_t* e)
{
    lv_obj_t* btn = lv_event_get_target(e);
    const char* txt = lv_label_get_text(lv_obj_get_child(btn, 0));
    if (txt && strcmp(txt, "Restore") == 0) {
        if (settings_restore_from_sd()) {
            show_toast("Restored — restart to fully apply");
        } else {
            show_toast("Restore failed — no usable backup");
        }
        refresh_status();
    }
    lv_obj_t* row = lv_obj_get_parent(btn);
    lv_obj_t* modal = row ? lv_obj_get_parent(row) : btn;
    lv_obj_del(modal);
}

static void confirm_restore(lv_event_t* e)
{
    (void)e;
    lv_obj_t* modal = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(modal);
    lv_obj_set_size(modal, lv_pct(90), LV_SIZE_CONTENT);
    lv_obj_center(modal);
    lv_obj_set_style_bg_color(modal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(modal, LV_OPA_90, 0);
    lv_obj_set_style_radius(modal, 12, 0);
    lv_obj_set_style_pad_all(modal, 12, 0);
    lv_obj_set_flex_flow(modal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(modal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* lbl_t = lv_label_create(modal);
    lv_label_set_text(lbl_t, "Confirm");
    lv_obj_set_style_text_font(lbl_t, &font_bold_28, 0);
    lv_obj_set_style_text_color(lbl_t, lv_color_hex(0xFFFFFF), 0);

    lv_obj_t* lbl = lv_label_create(modal);
    lv_label_set_text(lbl, "Restore settings from SD card?\nThis will overwrite your current settings.");
    lv_obj_set_style_text_font(lbl, &font_normal_26, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xE0E0E0), 0);

    lv_obj_t* row = lv_obj_create(modal);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* btn_cancel = lv_btn_create(row);
    lv_obj_t* lbl_c = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_c, "Cancel");
    lv_obj_set_style_text_font(lbl_c, &font_normal_26, 0);
    lv_obj_add_event_cb(btn_cancel, modal_btn_evt, LV_EVENT_CLICKED, NULL);

    lv_obj_t* btn_ok = lv_btn_create(row);
    lv_obj_t* lbl_ok = lv_label_create(btn_ok);
    lv_label_set_text(lbl_ok, "Restore");
    lv_obj_set_style_text_font(lbl_ok, &font_normal_26, 0);
    lv_obj_add_event_cb(btn_ok, modal_btn_evt, LV_EVENT_CLICKED, NULL);
}

static void do_backup(lv_event_t* e)
{
    (void)e;
    if (settings_backup_to_sd()) {
        show_toast("Backed up to SD");
    } else {
        show_toast("Backup failed — check SD card");
    }
    refresh_status();
}

void setting_backup_screen_create(lv_obj_t* parent)
{
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_text_color(&style, lv_color_white());
    lv_style_set_bg_color(&style, lv_color_black());
    lv_style_set_bg_opa(&style, LV_OPA_COVER);

    sbackup_screen = lv_obj_create(parent);
    lv_obj_remove_style_all(sbackup_screen);
    lv_obj_add_style(sbackup_screen, &style, 0);
    lv_obj_set_size(sbackup_screen, lv_pct(100), lv_pct(100));
    lv_obj_add_event_cb(sbackup_screen, screen_events, LV_EVENT_GESTURE, NULL);
    lv_obj_add_flag(sbackup_screen, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(sbackup_screen, LV_OBJ_FLAG_USER_1);
    lv_obj_add_event_cb(sbackup_screen, on_delete, LV_EVENT_DELETE, NULL);

    lv_obj_t* hdr = lv_obj_create(sbackup_screen);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_t* title = lv_label_create(hdr);
    lv_obj_set_style_text_font(title, &font_bold_32, 0);
    lv_label_set_text(title, "Backup & Restore");

    lv_obj_t* content = lv_obj_create(sbackup_screen);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, lv_pct(100), lv_pct(80));
    lv_obj_set_style_pad_top(content, 80, 0);
    lv_obj_set_style_pad_bottom(content, 10, 0);
    lv_obj_set_style_pad_left(content, 12, 0);
    lv_obj_set_style_pad_right(content, 12, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

    status_label = lv_label_create(content);
    lv_obj_set_style_text_font(status_label, &font_normal_26, 0);
    lv_label_set_text(status_label, "Checking SD card...");

    lv_obj_t* btn_backup = lv_btn_create(content);
    lv_obj_set_size(btn_backup, lv_pct(100), 60);
    lv_obj_add_event_cb(btn_backup, do_backup, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_b = lv_label_create(btn_backup);
    lv_obj_set_style_text_font(lbl_b, &font_bold_28, 0);
    lv_label_set_text(lbl_b, "Backup Settings to SD");

    lv_obj_t* btn_restore = lv_btn_create(content);
    lv_obj_set_size(btn_restore, lv_pct(100), 60);
    lv_obj_add_event_cb(btn_restore, confirm_restore, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_r = lv_label_create(btn_restore);
    lv_obj_set_style_text_font(lbl_r, &font_bold_28, 0);
    lv_label_set_text(lbl_r, "Restore from SD Backup");

    refresh_status();
}

static void on_delete(lv_event_t* e)
{
    (void)e;
    ESP_LOGI(TAG, "Backup & Restore screen deleted");
    sbackup_screen = NULL;
    status_label = NULL;
}
