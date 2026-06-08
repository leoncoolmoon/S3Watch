// Alarm Clock — launched from the app picker into the row-2 app tile (1,2).
//
// Alarm state (hour, minute, enabled, firing) is static — survives screen
// destroy/recreate so the alarm fires even while the user is in another app.
// A single task_coordinator subscriber does the time-match check at 1 Hz.
// The LVGL display pointers are zeroed on screen delete and null-checked in
// the subscriber before touching any widget (same pattern as signalk_dashboard).

#include "alarm_clock.h"
#include "ui_fonts.h"
#include "settings.h"
#include "audio_alert.h"
#include "display_manager.h"
#include "power_manager.h"
#include "task_coordinator.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "lvgl.h"
#include <time.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "alarm_clock";

// ---------------------------------------------------------------------------
// Persistent alarm state
// ---------------------------------------------------------------------------

static int  s_alarm_hour      = 7;
static int  s_alarm_min       = 0;
static bool s_alarm_enabled   = false;
static bool s_alarm_firing    = false;
static int  s_last_fired_hour = -1;
static int  s_last_fired_min  = -1;
static bool s_subscribed      = false;

// ---------------------------------------------------------------------------
// Live UI pointers — only valid while screen is mounted
// ---------------------------------------------------------------------------

static lv_obj_t   *s_lbl_time    = NULL;  // current time
static lv_obj_t   *s_btn_toggle  = NULL;  // ON/OFF checkable button
static lv_obj_t   *s_lbl_toggle  = NULL;  // label inside toggle
static lv_obj_t   *s_btn_dismiss = NULL;  // only visible when firing
static lv_obj_t   *s_roller_h    = NULL;
static lv_obj_t   *s_roller_m    = NULL;
static lv_timer_t *s_lv_timer    = NULL;

// ---------------------------------------------------------------------------
// UI sync helpers (must be called with LVGL lock held)
// ---------------------------------------------------------------------------

static void sync_toggle_appearance(void)
{
    if (!s_btn_toggle) return;
    if (s_alarm_enabled) {
        lv_obj_add_state(s_btn_toggle, LV_STATE_CHECKED);
        lv_label_set_text(s_lbl_toggle, "Alarm ON");
        lv_obj_set_style_bg_color(s_btn_toggle, lv_color_hex(0x27AE60), LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(s_btn_toggle, LV_STATE_CHECKED);
        lv_label_set_text(s_lbl_toggle, "Alarm OFF");
    }
}

static void sync_dismiss_visibility(void)
{
    if (!s_btn_dismiss) return;
    if (s_alarm_firing) lv_obj_clear_flag(s_btn_dismiss, LV_OBJ_FLAG_HIDDEN);
    else                lv_obj_add_flag(s_btn_dismiss,   LV_OBJ_FLAG_HIDDEN);
}

static void update_time_label(void)
{
    if (!s_lbl_time) return;
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    char buf[12];
    if (settings_get_time_24h()) {
        snprintf(buf, sizeof(buf), "%02d:%02d", lt.tm_hour, lt.tm_min);
    } else {
        int h = lt.tm_hour % 12;
        if (!h) h = 12;
        snprintf(buf, sizeof(buf), "%d:%02d %s", h, lt.tm_min,
                 lt.tm_hour >= 12 ? "PM" : "AM");
    }
    lv_label_set_text(s_lbl_time, buf);
}

// ---------------------------------------------------------------------------
// Task coordinator subscriber — 1 Hz, persists across screen changes
// ---------------------------------------------------------------------------

static void alarm_tick_cb(void *user)
{
    (void)user;

    time_t now_t = time(NULL);
    struct tm now;
    localtime_r(&now_t, &now);

    bool same_minute = (now.tm_hour == s_last_fired_hour &&
                        now.tm_min  == s_last_fired_min);

    // Auto-dismiss once the alarm minute has passed
    if (s_alarm_firing && !same_minute) {
        s_alarm_firing = false;
        ESP_LOGI(TAG, "alarm auto-dismissed (minute passed)");
        if (bsp_display_lock(50)) {
            sync_dismiss_visibility();
            bsp_display_unlock();
        }
    }

    if (!s_alarm_enabled) return;

    bool time_match = (now.tm_hour == s_alarm_hour &&
                       now.tm_min  == s_alarm_min);

    if (time_match && !same_minute) {
        s_alarm_firing    = true;
        s_last_fired_hour = now.tm_hour;
        s_last_fired_min  = now.tm_min;
        ESP_LOGI(TAG, "alarm firing at %02d:%02d", s_alarm_hour, s_alarm_min);
        power_manager_request_wake(PM_WAKE_ALARM);
        display_manager_reset_timer();
        audio_alert_notify();
        if (bsp_display_lock(50)) {
            sync_dismiss_visibility();
            bsp_display_unlock();
        }
    }

    // Repeat sound every 30 seconds while firing, and keep display awake
    if (s_alarm_firing && now.tm_sec % 30 == 0 && now.tm_sec != 0) {
        display_manager_reset_timer();
        audio_alert_notify();
    }

    // Update the live clock if the screen is open (cheap, skip on contention)
    if (bsp_display_lock(0)) {
        update_time_label();
        bsp_display_unlock();
    }
}

// ---------------------------------------------------------------------------
// Button handlers
// ---------------------------------------------------------------------------

static void toggle_cb(lv_event_t *e)
{
    (void)e;
    s_alarm_enabled = !s_alarm_enabled;
    if (!s_alarm_enabled && s_alarm_firing) {
        s_alarm_firing = false;
        sync_dismiss_visibility();
    }
    sync_toggle_appearance();
    ESP_LOGI(TAG, "alarm %s", s_alarm_enabled ? "enabled" : "disabled");
}

static void dismiss_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_wait_release(lv_indev_active());
    s_alarm_firing = false;
    s_alarm_enabled = false;  // disarm after dismiss so it doesn't re-fire
    sync_dismiss_visibility();
    sync_toggle_appearance();
    ESP_LOGI(TAG, "alarm dismissed");
}

static void roller_h_cb(lv_event_t *e)
{
    (void)e;
    if (s_roller_h) s_alarm_hour = (int)lv_roller_get_selected(s_roller_h);
    ESP_LOGI(TAG, "alarm set to %02d:%02d", s_alarm_hour, s_alarm_min);
}

static void roller_m_cb(lv_event_t *e)
{
    (void)e;
    if (s_roller_m) s_alarm_min = (int)lv_roller_get_selected(s_roller_m);
    ESP_LOGI(TAG, "alarm set to %02d:%02d", s_alarm_hour, s_alarm_min);
}

// ---------------------------------------------------------------------------
// Screen lifecycle
// ---------------------------------------------------------------------------

static void alarm_lv_timer_cb(lv_timer_t *t) { (void)t; update_time_label(); }

static void alarm_on_delete(lv_event_t *e)
{
    (void)e;
    if (s_lv_timer) { lv_timer_del(s_lv_timer); s_lv_timer = NULL; }
    s_lbl_time = s_btn_toggle = s_lbl_toggle = s_btn_dismiss = NULL;
    s_roller_h = s_roller_m = NULL;
    ESP_LOGI(TAG, "screen deleted (alarm state preserved)");
}

// ---------------------------------------------------------------------------
// Roller helper — mirrors setting_time_screen.c style
// ---------------------------------------------------------------------------

static lv_obj_t *make_roller(lv_obj_t *parent, const char *label_txt,
                              const char *options, uint16_t sel,
                              lv_event_cb_t cb)
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
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xAAAAAA), 0);
    lv_label_set_text(lbl, label_txt);

    lv_obj_t *roller = lv_roller_create(col);
    lv_roller_set_options(roller, options, LV_ROLLER_MODE_INFINITE);
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

    lv_obj_add_event_cb(roller, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return roller;
}

// ---------------------------------------------------------------------------
// Screen construction
// ---------------------------------------------------------------------------

void alarm_clock_create(lv_obj_t *parent)
{
    lv_obj_t *screen = lv_obj_create(parent);
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, lv_pct(100), lv_pct(100));
    lv_obj_center(screen);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(screen, alarm_on_delete, LV_EVENT_DELETE, NULL);

    // Title
    lv_obj_t *title = lv_label_create(screen);
    lv_obj_set_style_text_font(title, &font_bold_32, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_label_set_text(title, "Alarm");

    // Current time (large, live)
    s_lbl_time = lv_label_create(screen);
    lv_obj_set_style_text_font(s_lbl_time, &font_numbers_80, 0);
    lv_obj_set_style_text_color(s_lbl_time, lv_color_white(), 0);
    lv_label_set_text(s_lbl_time, "--:--");

    // Alarm time rollers
    static char hour_opts[24 * 4];
    static char min_opts[60 * 4];
    hour_opts[0] = min_opts[0] = '\0';
    for (int i = 0; i < 24; i++) {
        char tmp[8]; snprintf(tmp, sizeof(tmp), i < 23 ? "%02d\n" : "%02d", i);
        strncat(hour_opts, tmp, sizeof(hour_opts) - strlen(hour_opts) - 1);
    }
    for (int i = 0; i < 60; i++) {
        char tmp[8]; snprintf(tmp, sizeof(tmp), i < 59 ? "%02d\n" : "%02d", i);
        strncat(min_opts, tmp, sizeof(min_opts) - strlen(min_opts) - 1);
    }

    lv_obj_t *roller_row = lv_obj_create(screen);
    lv_obj_remove_style_all(roller_row);
    lv_obj_add_flag(roller_row, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_size(roller_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(roller_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(roller_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_roller_h = make_roller(roller_row, "H", hour_opts, (uint16_t)s_alarm_hour, roller_h_cb);
    s_roller_m = make_roller(roller_row, "M", min_opts,  (uint16_t)s_alarm_min,  roller_m_cb);

    // ON/OFF toggle button
    s_btn_toggle = lv_obj_create(screen);
    lv_obj_remove_style_all(s_btn_toggle);
    lv_obj_set_size(s_btn_toggle, lv_pct(70), 58);
    lv_obj_set_style_bg_color(s_btn_toggle, lv_color_hex(0x444444), 0);
    lv_obj_set_style_bg_opa(s_btn_toggle, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_btn_toggle, lv_color_hex(0x27AE60), LV_STATE_CHECKED);
    lv_obj_set_style_radius(s_btn_toggle, 16, 0);
    lv_obj_set_flex_flow(s_btn_toggle, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_btn_toggle, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(s_btn_toggle, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CHECKABLE);
    lv_obj_add_event_cb(s_btn_toggle, toggle_cb, LV_EVENT_CLICKED, NULL);

    s_lbl_toggle = lv_label_create(s_btn_toggle);
    lv_obj_set_style_text_font(s_lbl_toggle, &font_bold_32, 0);
    lv_obj_set_style_text_color(s_lbl_toggle, lv_color_white(), 0);

    // Dismiss button — hidden unless firing
    s_btn_dismiss = lv_obj_create(screen);
    lv_obj_remove_style_all(s_btn_dismiss);
    lv_obj_set_size(s_btn_dismiss, lv_pct(70), 58);
    lv_obj_set_style_bg_color(s_btn_dismiss, lv_color_hex(0xC0392B), 0);
    lv_obj_set_style_bg_opa(s_btn_dismiss, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_btn_dismiss, 16, 0);
    lv_obj_set_flex_flow(s_btn_dismiss, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_btn_dismiss, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(s_btn_dismiss, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_btn_dismiss, dismiss_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_dismiss = lv_label_create(s_btn_dismiss);
    lv_obj_set_style_text_font(lbl_dismiss, &font_bold_32, 0);
    lv_obj_set_style_text_color(lbl_dismiss, lv_color_white(), 0);
    lv_label_set_text(lbl_dismiss, "Dismiss");

    // Restore state
    sync_toggle_appearance();
    sync_dismiss_visibility();
    update_time_label();

    // Live clock refresh
    if (s_lv_timer) { lv_timer_del(s_lv_timer); s_lv_timer = NULL; }
    s_lv_timer = lv_timer_create(alarm_lv_timer_cb, 500, NULL);

    // Subscribe once — task_coord has no unsubscribe
    if (!s_subscribed) {
        task_coord_subscribe("alarm_check", alarm_tick_cb, NULL,
                             /*on*/ 1000, /*off*/ 1000);
        s_subscribed = true;
    }
}
