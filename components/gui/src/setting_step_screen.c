// Settings → Step Counter. Enable toggle (applies immediately via imu_manager —
// no restart needed) + a "Reset Stats" button (wipes step_tracker totals/records).
// Mirrors setting_storage_screen.c.

#include "setting_step_screen.h"
#include "ui.h"
#include "ui_fonts.h"
#include "settings.h"
#include "imu_manager.h"
#include "step_tracker.h"
#include "lvgl.h"
#include "esp_log.h"

static lv_obj_t* sstep_screen;
static const char* TAG = "StepSettings";

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
    lv_obj_set_style_min_width(toast, 120, 0);
    lv_obj_set_style_max_width(toast, lv_pct(90), 0);
    lv_obj_t* lbl = lv_label_create(toast);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_center(lbl);
    lv_obj_update_layout(toast);
    lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_timer_t* t = lv_timer_create(toast_timer_cb, 1200, toast);
    lv_timer_set_repeat_count(t, 1);   // one-shot (see storage screen note)
}

static void screen_events(lv_event_t* e)
{
    if (lv_event_get_code(e) == LV_EVENT_GESTURE) {
        if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_RIGHT) {
            lv_indev_wait_release(lv_indev_active());
            ui_dynamic_subtile_close();
            sstep_screen = NULL;
        }
    }
}

static void toggle_step_counter(lv_event_t* e)
{
    lv_obj_t* sw = lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    settings_set_step_counter_enabled(on);
    imu_manager_set_step_counting(on);   // applies immediately
    show_toast(on ? "Step counter on" : "Step counter off");
}

static void reset_steps_cb(lv_event_t* e)
{
    (void)e;
    lv_indev_wait_release(lv_indev_active());
    // Wipes lifetime/today/history/records — independent of the enable state.
    step_tracker_reset_all();
    show_toast("Stats reset");
}

static void on_delete(lv_event_t* e)
{
    (void)e;
    ESP_LOGI(TAG, "Step settings screen deleted");
    sstep_screen = NULL;
}

void setting_step_screen_create(lv_obj_t* parent)
{
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_text_color(&style, lv_color_white());
    lv_style_set_bg_color(&style, lv_color_black());
    lv_style_set_bg_opa(&style, LV_OPA_COVER);

    sstep_screen = lv_obj_create(parent);
    lv_obj_remove_style_all(sstep_screen);
    lv_obj_add_style(sstep_screen, &style, 0);
    lv_obj_set_size(sstep_screen, lv_pct(100), lv_pct(100));
    lv_obj_add_event_cb(sstep_screen, screen_events, LV_EVENT_GESTURE, NULL);
    lv_obj_add_flag(sstep_screen, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(sstep_screen, LV_OBJ_FLAG_USER_1);
    lv_obj_add_event_cb(sstep_screen, on_delete, LV_EVENT_DELETE, NULL);

    lv_obj_t* hdr = lv_obj_create(sstep_screen);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_t* title = lv_label_create(hdr);
    lv_obj_set_style_text_font(title, &font_bold_32, 0);
    lv_label_set_text(title, "Step Counter");

    lv_obj_t* content = lv_obj_create(sstep_screen);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, lv_pct(100), lv_pct(80));
    lv_obj_set_style_pad_top(content, 80, 0);
    lv_obj_set_style_pad_bottom(content, 10, 0);
    lv_obj_set_style_pad_left(content, 12, 0);
    lv_obj_set_style_pad_right(content, 12, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

    lv_obj_t* row = lv_obj_create(content);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t* lbl = lv_label_create(row);
    lv_obj_set_style_text_font(lbl, &font_normal_26, 0);
    lv_label_set_text(lbl, "Count steps");
    lv_obj_t* sw = lv_switch_create(row);
    lv_obj_set_size(sw, 120, 50);
    if (settings_get_step_counter_enabled()) lv_obj_add_state(sw, LV_STATE_CHECKED);
    else lv_obj_clear_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, toggle_step_counter, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t* btn_reset = lv_btn_create(content);
    lv_obj_set_size(btn_reset, lv_pct(100), 60);
    lv_obj_add_event_cb(btn_reset, reset_steps_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_r = lv_label_create(btn_reset);
    lv_obj_set_style_text_font(lbl_r, &font_bold_28, 0);
    lv_label_set_text(lbl_r, "Reset Stats");
}
