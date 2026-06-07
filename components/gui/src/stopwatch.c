// Stopwatch — launched from the app picker into the row-2 app tile (1,2).
//
// Timer state is static so it survives screen destroy/recreate — the watch
// keeps running if the user navigates away and returns.
//
// States:
//   RESET   → Start → RUNNING
//   RUNNING → Stop  → PAUSED,  Lap → record split, stay RUNNING
//   PAUSED  → Resume → RUNNING, Reset → RESET

#include "stopwatch.h"
#include "ui_fonts.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "stopwatch";

#define MAX_LAPS 50

// ---------------------------------------------------------------------------
// Persistent timer state
// ---------------------------------------------------------------------------

typedef enum { SW_RESET, SW_RUNNING, SW_PAUSED } sw_state_t;

static sw_state_t s_state    = SW_RESET;
static int64_t    s_start_us = 0;   // esp_timer_get_time() at last start/resume
static int64_t    s_accum_us = 0;   // accumulated µs before last pause
static int64_t    s_laps_us[MAX_LAPS];
static int        s_lap_count = 0;

static int64_t sw_elapsed_us(void)
{
    if (s_state == SW_RUNNING)
        return s_accum_us + (esp_timer_get_time() - s_start_us);
    return s_accum_us;
}

// ---------------------------------------------------------------------------
// Live UI pointers — only valid while screen is mounted
// ---------------------------------------------------------------------------

static lv_obj_t   *s_lbl_main  = NULL;   // "MM:SS" in font_numbers_80
static lv_obj_t   *s_lbl_frac  = NULL;   // ".cc"   in font_bold_42
static lv_obj_t   *s_lap_list  = NULL;   // scrollable container for lap rows
static lv_obj_t   *s_btn_left  = NULL;
static lv_obj_t   *s_btn_right = NULL;
static lv_obj_t   *s_lbl_left  = NULL;
static lv_obj_t   *s_lbl_right = NULL;
static lv_timer_t *s_lv_timer  = NULL;

// ---------------------------------------------------------------------------
// Formatting helpers
// ---------------------------------------------------------------------------

static void format_us(int64_t us, char *main_buf, char *frac_buf)
{
    int64_t total_cs = us / 10000;
    int cs = (int)(total_cs % 100);
    int64_t total_s = total_cs / 100;
    int s  = (int)(total_s % 60);
    int m  = (int)((total_s / 60) % 100);
    snprintf(main_buf, 8, "%02d:%02d", m, s);
    snprintf(frac_buf, 5, ".%02d", cs);
}

// ---------------------------------------------------------------------------
// UI state sync — updates buttons and (optionally) rebuilds the lap list
// ---------------------------------------------------------------------------

static void sync_buttons(void)
{
    if (!s_btn_left) return;

    // Left: Start (RESET), Stop (RUNNING), Resume (PAUSED)
    const char *left_text;
    lv_color_t  left_col;
    if (s_state == SW_RUNNING) {
        left_text = "Stop";
        left_col  = lv_color_hex(0xC0392B);  // red
    } else {
        left_text = (s_state == SW_RESET) ? "Start" : "Resume";
        left_col  = lv_color_hex(0x27AE60);  // green
    }
    lv_label_set_text(s_lbl_left, left_text);
    lv_obj_set_style_bg_color(s_btn_left, left_col, 0);

    // Right: hidden (RESET), Lap (RUNNING), Reset (PAUSED)
    if (s_state == SW_RESET) {
        lv_obj_add_flag(s_btn_right, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_btn_right, LV_OBJ_FLAG_HIDDEN);
        const char *right_text = (s_state == SW_RUNNING) ? "Lap" : "Reset";
        lv_label_set_text(s_lbl_right, right_text);
    }
}

// Prepend a lap row at the top of s_lap_list.
// lap_us = total elapsed at that lap.  num = 1-based lap number.
static void prepend_lap_row(int num, int64_t lap_us)
{
    if (!s_lap_list) return;

    char main_buf[8], frac_buf[5];
    format_us(lap_us, main_buf, frac_buf);

    lv_obj_t *row = lv_obj_create(s_lap_list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 34);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    if (num % 2 == 0) {
        lv_obj_set_style_bg_color(row, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_bg_opa(row, 12, 0);
        lv_obj_set_style_radius(row, 6, 0);
    }

    // "Lap N"
    lv_obj_t *lbl_num = lv_label_create(row);
    lv_obj_set_style_text_font(lbl_num, &font_normal_26, 0);
    lv_obj_set_style_text_color(lbl_num, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_pad_left(lbl_num, 4, 0);
    lv_obj_set_flex_grow(lbl_num, 1);
    char lap_label[12];
    snprintf(lap_label, sizeof(lap_label), "Lap %d", num);
    lv_label_set_text(lbl_num, lap_label);

    // Time
    lv_obj_t *lbl_t = lv_label_create(row);
    lv_obj_set_style_text_font(lbl_t, &font_normal_26, 0);
    lv_obj_set_style_text_color(lbl_t, lv_color_white(), 0);
    lv_obj_set_style_pad_right(lbl_t, 4, 0);
    char tbuf[14];
    snprintf(tbuf, sizeof(tbuf), "%s%s", main_buf, frac_buf);
    lv_label_set_text(lbl_t, tbuf);

    // Newest lap at top
    lv_obj_move_to_index(row, 0);
}

static void rebuild_lap_list(void)
{
    if (!s_lap_list) return;
    lv_obj_clean(s_lap_list);
    // Add in reverse so index-0 move puts them in correct order
    for (int i = s_lap_count - 1; i >= 0; i--) {
        prepend_lap_row(i + 1, s_laps_us[i]);
    }
}

// ---------------------------------------------------------------------------
// Display refresh (called by LVGL timer and after state changes)
// ---------------------------------------------------------------------------

static void refresh_display(void)
{
    if (!s_lbl_main) return;
    char main_buf[8], frac_buf[5];
    format_us(sw_elapsed_us(), main_buf, frac_buf);
    lv_label_set_text(s_lbl_main, main_buf);
    lv_label_set_text(s_lbl_frac, frac_buf);
}

static void sw_timer_cb(lv_timer_t *t) { (void)t; refresh_display(); }

// ---------------------------------------------------------------------------
// Button handlers
// ---------------------------------------------------------------------------

static void btn_left_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_wait_release(lv_indev_active());
    if (s_state == SW_RESET || s_state == SW_PAUSED) {
        // Start / Resume
        s_start_us = esp_timer_get_time();
        s_state    = SW_RUNNING;
        ESP_LOGI(TAG, "started/resumed");
    } else {
        // Stop
        s_accum_us = sw_elapsed_us();
        s_state    = SW_PAUSED;
        ESP_LOGI(TAG, "paused at %lld us", s_accum_us);
    }
    refresh_display();
    sync_buttons();
}

static void btn_right_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_wait_release(lv_indev_active());
    if (s_state == SW_RUNNING) {
        // Lap
        if (s_lap_count < MAX_LAPS) {
            int64_t t = sw_elapsed_us();
            s_laps_us[s_lap_count++] = t;
            prepend_lap_row(s_lap_count, t);
            ESP_LOGI(TAG, "lap %d: %lld us", s_lap_count, t);
        }
    } else if (s_state == SW_PAUSED) {
        // Reset
        s_state     = SW_RESET;
        s_accum_us  = 0;
        s_start_us  = 0;
        s_lap_count = 0;
        rebuild_lap_list();
        refresh_display();
        sync_buttons();
        ESP_LOGI(TAG, "reset");
    }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

static void sw_on_delete(lv_event_t *e)
{
    (void)e;
    if (s_lv_timer) { lv_timer_del(s_lv_timer); s_lv_timer = NULL; }
    s_lbl_main = s_lbl_frac = s_lap_list = NULL;
    s_btn_left = s_btn_right = s_lbl_left = s_lbl_right = NULL;
    ESP_LOGI(TAG, "screen deleted (timer state preserved)");
}

// ---------------------------------------------------------------------------
// Screen construction
// ---------------------------------------------------------------------------

static lv_obj_t *make_button(lv_obj_t *parent, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_width(btn, lv_pct(46));
    lv_obj_set_height(btn, 58);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

void stopwatch_create(lv_obj_t *parent)
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
    lv_obj_add_event_cb(screen, sw_on_delete, LV_EVENT_DELETE, NULL);

    // Title
    lv_obj_t *title = lv_label_create(screen);
    lv_obj_set_style_text_font(title, &font_bold_32, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_pad_top(title, 14, 0);
    lv_obj_set_style_pad_bottom(title, 8, 0);
    lv_label_set_text(title, "Stopwatch");
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    // Time display row: [MM:SS] [.cc]
    lv_obj_t *time_row = lv_obj_create(screen);
    lv_obj_remove_style_all(time_row);
    lv_obj_set_size(time_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(time_row, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(time_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(time_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_bottom(time_row, 8, 0);

    s_lbl_main = lv_label_create(time_row);
    lv_obj_set_style_text_font(s_lbl_main, &font_numbers_80, 0);
    lv_obj_set_style_text_color(s_lbl_main, lv_color_white(), 0);
    lv_label_set_text(s_lbl_main, "00:00");

    s_lbl_frac = lv_label_create(time_row);
    lv_obj_set_style_text_font(s_lbl_frac, &font_bold_42, 0);
    lv_obj_set_style_text_color(s_lbl_frac, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_pad_bottom(s_lbl_frac, 8, 0);  // baseline-align with main
    lv_label_set_text(s_lbl_frac, ".00");

    // Lap list
    s_lap_list = lv_obj_create(screen);
    lv_obj_remove_style_all(s_lap_list);
    lv_obj_set_width(s_lap_list, lv_pct(100));
    lv_obj_set_flex_grow(s_lap_list, 1);
    lv_obj_set_style_bg_opa(s_lap_list, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(s_lap_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_lap_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(s_lap_list, 16, 0);
    lv_obj_set_style_pad_right(s_lap_list, 16, 0);
    lv_obj_set_style_pad_top(s_lap_list, 2, 0);
    lv_obj_set_style_pad_bottom(s_lap_list, 4, 0);
    lv_obj_set_style_pad_row(s_lap_list, 0, 0);
    lv_obj_set_scroll_dir(s_lap_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_lap_list, LV_SCROLLBAR_MODE_OFF);

    // Button row
    lv_obj_t *btn_row = lv_obj_create(screen);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_size(btn_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(btn_row, 4, 0);
    lv_obj_set_style_pad_bottom(btn_row, 12, 0);

    s_btn_left = make_button(btn_row, btn_left_cb);
    s_lbl_left = lv_label_create(s_btn_left);
    lv_obj_set_style_text_font(s_lbl_left, &font_bold_32, 0);
    lv_obj_set_style_text_color(s_lbl_left, lv_color_white(), 0);
    lv_label_set_text(s_lbl_left, "Start");

    s_btn_right = make_button(btn_row, btn_right_cb);
    lv_obj_set_style_bg_color(s_btn_right, lv_color_hex(0x555555), 0);
    s_lbl_right = lv_label_create(s_btn_right);
    lv_obj_set_style_text_font(s_lbl_right, &font_bold_32, 0);
    lv_obj_set_style_text_color(s_lbl_right, lv_color_white(), 0);
    lv_label_set_text(s_lbl_right, "Lap");

    // Restore state from persistent storage
    rebuild_lap_list();
    refresh_display();
    sync_buttons();

    // 100 ms refresh timer
    if (s_lv_timer) { lv_timer_del(s_lv_timer); s_lv_timer = NULL; }
    s_lv_timer = lv_timer_create(sw_timer_cb, 100, NULL);
}
