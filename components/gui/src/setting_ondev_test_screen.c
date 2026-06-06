// Settings → Run Tests sub-screen. Pre-populates one row per registered
// test (with section headers Software / Hardware) and a Run button. On
// Run, spawns a worker task that calls ondev_test_run_all; progress is
// delivered via a callback that hops onto the LVGL thread with
// lv_async_call to update widgets safely.

#include "setting_ondev_test_screen.h"
#include "ui.h"
#include "ui_fonts.h"
#include "ondev_test.h"
#include "display_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "SettingTest";

typedef struct {
    lv_obj_t *dot;     // status glyph
    lv_obj_t *name;
    lv_obj_t *detail;
} row_t;

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *header_label;     // "Run" / "Running n/N…" / "n/N passed"
    lv_obj_t *run_btn;
    row_t    *rows;             // sized to ondev_test_count()
    int       n_rows;
    int       passed;
    int       failed;
    volatile bool running;
} ctx_t;

static ctx_t *s_ctx;

// ── Async-call payload ───────────────────────────────────────────────────

typedef struct {
    int index;
    int total;
    bool pass;
    char detail[96];
} progress_msg_t;

static const char *DOT_PENDING = "·";
static const char *DOT_RUNNING = "…";
static const char *DOT_PASS    = LV_SYMBOL_OK;
static const char *DOT_FAIL    = LV_SYMBOL_CLOSE;

static void apply_progress(void *arg) {
    progress_msg_t *m = (progress_msg_t *)arg;
    if (!s_ctx) { free(m); return; }
    if (m->index < 0 || m->index >= s_ctx->n_rows) { free(m); return; }
    row_t *r = &s_ctx->rows[m->index];
    if (m->pass) {
        lv_label_set_text(r->dot, DOT_PASS);
        lv_obj_set_style_text_color(r->dot, lv_color_hex(0x60D060), 0);
        s_ctx->passed++;
    } else {
        lv_label_set_text(r->dot, DOT_FAIL);
        lv_obj_set_style_text_color(r->dot, lv_color_hex(0xFF4040), 0);
        s_ctx->failed++;
    }
    lv_label_set_text(r->detail, m->detail);

    char hdr[40];
    int total = m->total;
    int done = s_ctx->passed + s_ctx->failed;
    if (done < total) {
        // Show next row as "running"
        if (done < s_ctx->n_rows) {
            row_t *nxt = &s_ctx->rows[done];
            lv_label_set_text(nxt->dot, DOT_RUNNING);
            lv_obj_set_style_text_color(nxt->dot, lv_color_hex(0xFFC000), 0);
        }
        snprintf(hdr, sizeof(hdr), "Running %d/%d…", done + 1, total);
    } else {
        snprintf(hdr, sizeof(hdr), "%d/%d passed", s_ctx->passed, total);
    }
    lv_label_set_text(s_ctx->header_label, hdr);

    display_manager_reset_timer();
    free(m);
}

static void on_progress(int index, int total,
                        ondev_test_category_t cat,
                        const char *name,
                        ondev_test_result_t r,
                        const char *detail,
                        void *user) {
    (void)cat; (void)name; (void)user;
    progress_msg_t *m = (progress_msg_t *)calloc(1, sizeof(progress_msg_t));
    if (!m) return;
    m->index = index;
    m->total = total;
    m->pass  = (r == ONDEV_TEST_PASS);
    strncpy(m->detail, detail ? detail : "", sizeof(m->detail) - 1);
    lv_async_call(apply_progress, m);
}

// ── Worker task ──────────────────────────────────────────────────────────

static void run_task(void *arg) {
    (void)arg;
    if (!s_ctx) { vTaskDelete(NULL); return; }
    s_ctx->running = true;
    s_ctx->passed = 0;
    s_ctx->failed = 0;
    ESP_LOGI(TAG, "Self-test run starting");
    (void)ondev_test_run_all(on_progress, NULL);
    ESP_LOGI(TAG, "Self-test run complete");
    s_ctx->running = false;
    vTaskDelete(NULL);
}

static void on_run_clicked(lv_event_t *e) {
    (void)e;
    if (!s_ctx || s_ctx->running) return;
    // Reset all row indicators to pending and the first to running.
    for (int i = 0; i < s_ctx->n_rows; i++) {
        lv_label_set_text(s_ctx->rows[i].dot, DOT_PENDING);
        lv_obj_set_style_text_color(s_ctx->rows[i].dot, lv_color_hex(0x9099A8), 0);
        lv_label_set_text(s_ctx->rows[i].detail, "");
    }
    if (s_ctx->n_rows > 0) {
        lv_label_set_text(s_ctx->rows[0].dot, DOT_RUNNING);
        lv_obj_set_style_text_color(s_ctx->rows[0].dot, lv_color_hex(0xFFC000), 0);
    }
    char hdr[40];
    snprintf(hdr, sizeof(hdr), "Running 1/%d…", s_ctx->n_rows);
    lv_label_set_text(s_ctx->header_label, hdr);

    xTaskCreate(run_task, "ondev_test", 8192, NULL, 4, NULL);
}

// ── Layout helpers ───────────────────────────────────────────────────────

static lv_obj_t *make_section_header(lv_obj_t *parent, const char *txt) {
    lv_obj_t *h = lv_label_create(parent);
    lv_obj_set_width(h, lv_pct(100));
    lv_obj_set_style_text_font(h, &font_bold_28, 0);
    lv_obj_set_style_text_color(h, lv_color_hex(0x9099A8), 0);
    lv_obj_set_style_pad_top(h, 8, 0);
    lv_obj_set_style_pad_bottom(h, 4, 0);
    lv_label_set_text(h, txt);
    return h;
}

static void build_row(lv_obj_t *parent, const char *name, row_t *out) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_style_margin_bottom(row, 4, 0);

    out->dot = lv_label_create(row);
    lv_obj_set_style_text_font(out->dot, &font_bold_28, 0);
    lv_obj_set_style_text_color(out->dot, lv_color_hex(0x9099A8), 0);
    lv_label_set_text(out->dot, DOT_PENDING);

    lv_obj_t *col = lv_obj_create(row);
    lv_obj_remove_style_all(col);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_height(col, LV_SIZE_CONTENT);

    out->name = lv_label_create(col);
    lv_obj_set_style_text_font(out->name, &font_normal_28, 0);
    lv_obj_set_style_text_color(out->name, lv_color_white(), 0);
    lv_label_set_text(out->name, name);

    out->detail = lv_label_create(col);
    lv_obj_set_style_text_font(out->detail, &font_normal_26, 0);
    lv_obj_set_style_text_color(out->detail, lv_color_hex(0x9099A8), 0);
    lv_label_set_long_mode(out->detail, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(out->detail, lv_pct(100));
    lv_label_set_text(out->detail, "");
}

static void on_delete(lv_event_t *e) {
    (void)e;
    if (!s_ctx) return;
    free(s_ctx->rows);
    free(s_ctx);
    s_ctx = NULL;
}

static void screen_events(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) return;
    if (lv_indev_get_gesture_dir(lv_indev_active()) != LV_DIR_RIGHT) return;
    lv_indev_wait_release(lv_indev_active());
    ui_dynamic_subtile_close();
}

void setting_ondev_test_screen_create(lv_obj_t *parent) {
    int n = ondev_test_count();
    s_ctx = (ctx_t *)calloc(1, sizeof(ctx_t));
    if (!s_ctx) return;
    s_ctx->rows = (row_t *)calloc(n, sizeof(row_t));
    s_ctx->n_rows = n;

    static lv_style_t style;
    static bool style_ready = false;
    if (!style_ready) {
        lv_style_init(&style);
        lv_style_set_text_color(&style, lv_color_white());
        lv_style_set_bg_color(&style, lv_color_black());
        lv_style_set_bg_opa(&style, LV_OPA_COVER);
        style_ready = true;
    }

    s_ctx->screen = lv_obj_create(parent);
    lv_obj_remove_style_all(s_ctx->screen);
    lv_obj_add_style(s_ctx->screen, &style, 0);
    lv_obj_set_size(s_ctx->screen, lv_pct(100), lv_pct(100));
    lv_obj_add_flag(s_ctx->screen, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_ctx->screen, screen_events, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(s_ctx->screen, on_delete,    LV_EVENT_DELETE,  NULL);

    // Title
    lv_obj_t *title = lv_label_create(s_ctx->screen);
    lv_obj_set_style_text_font(title, &font_bold_32, 0);
    lv_label_set_text(title, "Run Tests");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    // Header status + Run button
    lv_obj_t *hdr_row = lv_obj_create(s_ctx->screen);
    lv_obj_remove_style_all(hdr_row);
    lv_obj_set_size(hdr_row, lv_pct(95), LV_SIZE_CONTENT);
    lv_obj_align(hdr_row, LV_ALIGN_TOP_MID, 0, 44);
    lv_obj_set_flex_flow(hdr_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_ctx->header_label = lv_label_create(hdr_row);
    lv_obj_set_style_text_font(s_ctx->header_label, &font_normal_28, 0);
    lv_obj_set_style_text_color(s_ctx->header_label, lv_color_hex(0x9099A8), 0);
    char hdr0[40];
    snprintf(hdr0, sizeof(hdr0), "0/%d", n);
    lv_label_set_text(s_ctx->header_label, hdr0);

    s_ctx->run_btn = lv_btn_create(hdr_row);
    lv_obj_set_size(s_ctx->run_btn, 100, 44);
    lv_obj_add_event_cb(s_ctx->run_btn, on_run_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_l = lv_label_create(s_ctx->run_btn);
    lv_obj_set_style_text_font(btn_l, &font_bold_28, 0);
    lv_label_set_text(btn_l, "Run");
    lv_obj_center(btn_l);

    // Scroll container for rows
    lv_obj_t *list = lv_obj_create(s_ctx->screen);
    lv_obj_remove_style_all(list);
    lv_obj_add_flag(list, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_size(list, lv_pct(94), lv_pct(70));
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_set_style_pad_left(list, 8, 0);
    lv_obj_set_style_pad_right(list, 8, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    ondev_test_category_t prev_cat = ONDEV_TEST_SW;
    bool need_sw_header = true, need_hw_header = true;
    for (int i = 0; i < n; i++) {
        ondev_test_category_t cat;
        const char *name = NULL;
        if (!ondev_test_info(i, &cat, &name)) continue;
        if (cat == ONDEV_TEST_SW && need_sw_header) {
            make_section_header(list, "Software");
            need_sw_header = false;
        }
        if (cat == ONDEV_TEST_HW && need_hw_header) {
            make_section_header(list, "Hardware");
            need_hw_header = false;
        }
        build_row(list, name, &s_ctx->rows[i]);
        prev_cat = cat;
        (void)prev_cat;
    }
}
