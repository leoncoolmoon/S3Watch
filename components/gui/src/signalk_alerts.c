// SignalK alerts tile. Lives above the watchface at (0,0); user reaches it
// by swiping down.
//
// Pure read: a 1 Hz task_coordinator subscriber pulls the snapshot from
// signalk_client and renders one row per active alert. The boat is the
// real alerting authority; this tile is a passive viewer that's only
// alive while the user is looking at it.

#include "signalk_alerts.h"
#include "ui_fonts.h"
#include "signalk_client.h"
#include "task_coordinator.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    lv_obj_t *title;        // "ALERTS  (n)" or "ALERTS"
    lv_obj_t *list;         // flex column of row objects
    lv_obj_t *empty_label;  // shown when count == 0
    lv_obj_t *status;       // connection state at bottom
    int       last_count;   // detect "rebuild needed"
} alerts_t;

static alerts_t s_a;

static const char *state_text(signalk_state_t st) {
    switch (st) {
    case SIGNALK_STATE_IDLE:       return "Idle";
    case SIGNALK_STATE_NO_CONFIG:  return "Not configured";
    case SIGNALK_STATE_WIFI_UP:    return "Connecting Wi-Fi…";
    case SIGNALK_STATE_CONNECTING: return "Connecting…";
    case SIGNALK_STATE_CONNECTED:  return "Connected";
    case SIGNALK_STATE_ERROR:      return "Error";
    }
    return "?";
}

static const char *severity_label(signalk_alert_state_t s) {
    switch (s) {
    case SIGNALK_ALERT_EMERGENCY: return "EMERGENCY";
    case SIGNALK_ALERT_ALARM:     return "ALARM";
    case SIGNALK_ALERT_WARN:      return "WARN";
    case SIGNALK_ALERT_ALERT:     return "ALERT";
    case SIGNALK_ALERT_NORMAL:    return "";
    }
    return "";
}

static lv_color_t severity_color(signalk_alert_state_t s) {
    switch (s) {
    case SIGNALK_ALERT_EMERGENCY: return lv_color_hex(0xFF1F1F);
    case SIGNALK_ALERT_ALARM:     return lv_color_hex(0xFF4040);
    case SIGNALK_ALERT_WARN:      return lv_color_hex(0xFFC000);
    case SIGNALK_ALERT_ALERT:     return lv_color_hex(0x4090FF);
    case SIGNALK_ALERT_NORMAL:    return lv_color_hex(0x60D060);
    }
    return lv_color_white();
}

// Subpath after the leading "notifications." prefix.
static const char *short_path(const char *full) {
    if (!full) return "";
    if (strncmp(full, "notifications.", 14) == 0) return full + 14;
    return full;
}

static void build_row(lv_obj_t *parent, const signalk_alert_t *a) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_10, 0);
    lv_obj_set_style_bg_color(row, severity_color(a->state), 0);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_set_style_margin_bottom(row, 6, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(row, 6, 0);
    lv_obj_set_style_border_color(row, severity_color(a->state), 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *sev = lv_label_create(row);
    lv_obj_set_style_text_font(sev, &font_bold_28, 0);
    lv_obj_set_style_text_color(sev, severity_color(a->state), 0);
    lv_label_set_text(sev, severity_label(a->state));

    lv_obj_t *msg = lv_label_create(row);
    lv_obj_set_style_text_font(msg, &font_normal_28, 0);
    lv_obj_set_style_text_color(msg, lv_color_white(), 0);
    lv_label_set_long_mode(msg, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(msg, lv_pct(100));
    lv_label_set_text(msg, (a->message[0] ? a->message : "(no message)"));

    lv_obj_t *sub = lv_label_create(row);
    lv_obj_set_style_text_font(sub, &font_normal_26, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(0x9099A8), 0);
    lv_obj_set_width(sub, lv_pct(100));
    lv_label_set_long_mode(sub, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(sub, short_path(a->path));
}

// 1 Hz refresh; runs on task_coord, must take the LVGL lock before touching
// widgets. Rebuilds the row list each tick — small list, cheap cost.
static void tick_cb(void *user) {
    (void)user;
    int n = signalk_alerts_count();
    if (n > SIGNALK_ALERT_MAX_ACTIVE) n = SIGNALK_ALERT_MAX_ACTIVE;
    signalk_alert_t snap[SIGNALK_ALERT_MAX_ACTIVE];
    int got = 0;
    for (int i = 0; i < n; i++) {
        if (signalk_alerts_get(i, &snap[got])) got++;
    }
    char title_buf[24];
    if (got > 0) snprintf(title_buf, sizeof(title_buf), "ALERTS  (%d)", got);
    else         snprintf(title_buf, sizeof(title_buf), "ALERTS");
    const char *status = state_text(signalk_client_state());

    if (!bsp_display_lock(50)) return;
    lv_label_set_text(s_a.title, title_buf);
    lv_label_set_text(s_a.status, status);
    // Rebuild list
    lv_obj_clean(s_a.list);
    if (got == 0) {
        lv_obj_clear_flag(s_a.empty_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_a.list, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_a.empty_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_a.list, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < got; i++) build_row(s_a.list, &snap[i]);
    }
    s_a.last_count = got;
    bsp_display_unlock();
}

void signalk_alerts_create(lv_obj_t *parent) {
    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    s_a.title = lv_label_create(parent);
    lv_obj_set_style_text_font(s_a.title, &font_bold_32, 0);
    lv_obj_set_style_text_color(s_a.title, lv_color_white(), 0);
    lv_label_set_text(s_a.title, "ALERTS");
    lv_obj_align(s_a.title, LV_ALIGN_TOP_MID, 0, 10);

    s_a.list = lv_obj_create(parent);
    lv_obj_remove_style_all(s_a.list);
    lv_obj_set_size(s_a.list, lv_pct(92), lv_pct(70));
    lv_obj_align(s_a.list, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_pad_left(s_a.list, 8, 0);
    lv_obj_set_style_pad_right(s_a.list, 8, 0);
    lv_obj_set_flex_flow(s_a.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_a.list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(s_a.list, LV_DIR_VER);
    lv_obj_add_flag(s_a.list, LV_OBJ_FLAG_HIDDEN);   // hidden until first non-empty tick

    s_a.empty_label = lv_label_create(parent);
    lv_obj_set_style_text_font(s_a.empty_label, &font_bold_32, 0);
    lv_obj_set_style_text_color(s_a.empty_label, lv_color_hex(0x60D060), 0);
    lv_label_set_text(s_a.empty_label, "All Normal");
    lv_obj_center(s_a.empty_label);

    s_a.status = lv_label_create(parent);
    lv_obj_set_style_text_font(s_a.status, &font_normal_26, 0);
    lv_obj_set_style_text_color(s_a.status, lv_color_hex(0x9099A8), 0);
    lv_obj_align(s_a.status, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_label_set_text(s_a.status, "—");

    s_a.last_count = 0;

    task_coord_subscribe("signalk_alerts", tick_cb, NULL,
                         /*on*/  1000,   // 1 Hz while display on
                         /*off*/    0);  // skipped while off
}
