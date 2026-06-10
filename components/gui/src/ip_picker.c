// IPv4 picker: four 0..255 rollers + a Set button. Used by SignalK server
// entry; reusable for any future IPv4 setting.

#include "ip_picker.h"
#include "ip_parse.h"
#include "ui.h"
#include "ui_fonts.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *rollers[4];
    ip_picker_done_cb_t   on_done;
    ip_picker_cancel_cb_t on_cancel;
    void *user;
} ip_ctx_t;

// "000\n001\n...\n255" — built once, in PSRAM (off scarce internal RAM), reused
// by every picker. Lazily allocated on first use (when the IP picker is shown).
#define OCTET_OPTS_SIZE (256 * 4 + 1)
static char *s_octet_opts;
static bool s_octet_opts_ready = false;

static void build_octet_opts(void) {
    if (s_octet_opts_ready) return;
    if (!s_octet_opts) {
        s_octet_opts = heap_caps_malloc(OCTET_OPTS_SIZE, MALLOC_CAP_SPIRAM);
        if (!s_octet_opts) return;
    }
    char *p = s_octet_opts;
    for (int i = 0; i < 256; i++) {
        p += snprintf(p, 5, i < 255 ? "%d\n" : "%d", i);
    }
    s_octet_opts_ready = true;
}

static void on_delete(lv_event_t *e) {
    ip_ctx_t *ctx = (ip_ctx_t *)lv_event_get_user_data(e);
    if (ctx) free(ctx);
}

static void on_done_clicked(lv_event_t *e) {
    ip_ctx_t *ctx = (ip_ctx_t *)lv_event_get_user_data(e);
    if (!ctx) return;
    int v[4] = {0};
    for (int i = 0; i < 4; i++) v[i] = (int)lv_roller_get_selected(ctx->rollers[i]);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d.%d.%d.%d", v[0], v[1], v[2], v[3]);
    if (ctx->on_done) ctx->on_done(buf, ctx->user);
    lv_obj_del_async(ctx->screen);
}

static void on_gesture(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) return;
    if (lv_indev_get_gesture_dir(lv_indev_active()) != LV_DIR_RIGHT) return;
    ip_ctx_t *ctx = (ip_ctx_t *)lv_event_get_user_data(e);
    if (!ctx) return;
    lv_indev_wait_release(lv_indev_active());
    if (ctx->on_cancel) ctx->on_cancel(ctx->user);
    lv_obj_del_async(ctx->screen);
}

static lv_obj_t *make_roller_col(lv_obj_t *parent, const char *label_txt,
                                  uint16_t sel) {
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_remove_style_all(col);
    lv_obj_add_flag(col, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 6, 0);

    lv_obj_t *lbl = lv_label_create(col);
    lv_obj_set_style_text_font(lbl, &font_bold_28, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_label_set_text(lbl, label_txt);

    lv_obj_t *roller = lv_roller_create(col);
    lv_roller_set_options(roller, s_octet_opts, LV_ROLLER_MODE_NORMAL);
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

    return roller;
}

void ip_picker_create(lv_obj_t *parent,
                      const char *initial,
                      ip_picker_done_cb_t   done,
                      ip_picker_cancel_cb_t cancel,
                      void *user) {
    build_octet_opts();

    int v[4] = {0, 0, 0, 0};
    ip_parse_v4(initial, v);

    ip_ctx_t *ctx = (ip_ctx_t *)calloc(1, sizeof(ip_ctx_t));
    if (!ctx) return;
    ctx->on_done   = done;
    ctx->on_cancel = cancel;
    ctx->user      = user;

    static lv_style_t style;
    static bool style_ready = false;
    if (!style_ready) {
        lv_style_init(&style);
        lv_style_set_text_color(&style, lv_color_white());
        lv_style_set_bg_color(&style, lv_color_black());
        lv_style_set_bg_opa(&style, LV_OPA_COVER);
        style_ready = true;
    }

    ctx->screen = lv_obj_create(parent);
    lv_obj_remove_style_all(ctx->screen);
    lv_obj_add_style(ctx->screen, &style, 0);
    lv_obj_set_size(ctx->screen, lv_pct(100), lv_pct(100));
    lv_obj_add_flag(ctx->screen, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_flex_flow(ctx->screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ctx->screen, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(ctx->screen, on_gesture, LV_EVENT_GESTURE, ctx);
    lv_obj_add_event_cb(ctx->screen, on_delete,  LV_EVENT_DELETE,  ctx);

    lv_obj_t *title = lv_label_create(ctx->screen);
    lv_obj_set_style_text_font(title, &font_bold_32, 0);
    lv_label_set_text(title, "IP Address");

    lv_obj_t *row = lv_obj_create(ctx->screen);
    lv_obj_remove_style_all(row);
    lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    static const char *labels[4] = {"1", "2", "3", "4"};
    for (int i = 0; i < 4; i++) {
        ctx->rollers[i] = make_roller_col(row, labels[i], (uint16_t)v[i]);
    }

    lv_obj_t *btn = lv_btn_create(ctx->screen);
    lv_obj_set_size(btn, 160, 60);
    lv_obj_add_event_cb(btn, on_done_clicked, LV_EVENT_CLICKED, ctx);
    lv_obj_t *blbl = lv_label_create(btn);
    lv_obj_set_style_text_font(blbl, &font_bold_32, 0);
    lv_label_set_text(blbl, "Set");
    lv_obj_center(blbl);
}
