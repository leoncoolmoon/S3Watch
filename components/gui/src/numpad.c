// 3x4 numeric pad. Used by SignalK port entry; reusable for any future
// numeric setting (timeouts, polling intervals, etc.).

#include "numpad.h"
#include "ui.h"
#include "ui_fonts.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *value_label;
    lv_obj_t *done_btn;
    uint32_t  value;
    uint32_t  min_value;
    uint32_t  max_value;
    numpad_done_cb_t   on_done;
    numpad_cancel_cb_t on_cancel;
    void *user;
} np_ctx_t;

static void refresh_value_label(np_ctx_t *ctx) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%u", (unsigned)ctx->value);
    lv_label_set_text(ctx->value_label, buf);
    bool ok = ctx->value >= ctx->min_value && ctx->value <= ctx->max_value;
    if (ok) lv_obj_clear_state(ctx->done_btn, LV_STATE_DISABLED);
    else    lv_obj_add_state  (ctx->done_btn, LV_STATE_DISABLED);
}

static void on_delete(lv_event_t *e) {
    np_ctx_t *ctx = (np_ctx_t *)lv_event_get_user_data(e);
    if (ctx) free(ctx);
}

static void on_gesture(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) return;
    if (lv_indev_get_gesture_dir(lv_indev_active()) != LV_DIR_RIGHT) return;
    np_ctx_t *ctx = (np_ctx_t *)lv_event_get_user_data(e);
    if (!ctx) return;
    lv_indev_wait_release(lv_indev_active());
    if (ctx->on_cancel) ctx->on_cancel(ctx->user);
    lv_obj_del_async(ctx->screen);
}

static void on_digit(lv_event_t *e) {
    np_ctx_t *ctx = (np_ctx_t *)lv_event_get_user_data(e);
    int digit = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e));
    if (!ctx) return;
    // Clamp: ignore the press if it would push past max.
    uint64_t next = (uint64_t)ctx->value * 10 + (uint32_t)digit;
    if (next > ctx->max_value) return;
    ctx->value = (uint32_t)next;
    refresh_value_label(ctx);
}

static void on_backspace(lv_event_t *e) {
    np_ctx_t *ctx = (np_ctx_t *)lv_event_get_user_data(e);
    if (!ctx) return;
    ctx->value /= 10;
    refresh_value_label(ctx);
}

static void on_done_clicked(lv_event_t *e) {
    np_ctx_t *ctx = (np_ctx_t *)lv_event_get_user_data(e);
    if (!ctx) return;
    if (ctx->value < ctx->min_value || ctx->value > ctx->max_value) return;
    if (ctx->on_done) ctx->on_done(ctx->value, ctx->user);
    lv_obj_del_async(ctx->screen);
}

static lv_obj_t *make_key(lv_obj_t *parent, const char *txt,
                          lv_event_cb_t cb, void *user, int digit) {
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, 80, 60);
    lv_obj_set_user_data(b, (void *)(intptr_t)digit);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &font_bold_32, 0);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    return b;
}

void numpad_create(lv_obj_t *parent,
                   const char *title,
                   uint32_t initial,
                   uint32_t min_value,
                   uint32_t max_value,
                   numpad_done_cb_t   done,
                   numpad_cancel_cb_t cancel,
                   void *user) {
    np_ctx_t *ctx = (np_ctx_t *)calloc(1, sizeof(np_ctx_t));
    if (!ctx) return;
    ctx->value     = initial;
    ctx->min_value = min_value ? min_value : 1;
    ctx->max_value = max_value ? max_value : UINT32_MAX;
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

    lv_obj_t *t = lv_label_create(ctx->screen);
    lv_obj_set_style_text_font(t, &font_bold_32, 0);
    lv_label_set_text(t, title ? title : "");

    ctx->value_label = lv_label_create(ctx->screen);
    lv_obj_set_style_text_font(ctx->value_label, &font_bold_32, 0);
    lv_obj_set_style_text_color(ctx->value_label, lv_color_white(), 0);

    lv_obj_t *grid = lv_obj_create(ctx->screen);
    lv_obj_remove_style_all(grid);
    lv_obj_add_flag(grid, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_size(grid, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    static const int32_t col_dsc[] = {80, 80, 80, LV_GRID_TEMPLATE_LAST};
    static const int32_t row_dsc[] = {60, 60, 60, 60, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_row(grid, 8, 0);
    lv_obj_set_style_pad_column(grid, 8, 0);

    // 1 2 3 / 4 5 6 / 7 8 9 / ⌫ 0 ✓
    static const struct { const char *txt; int row; int col; int digit; } digits[] = {
        {"1", 0, 0, 1}, {"2", 0, 1, 2}, {"3", 0, 2, 3},
        {"4", 1, 0, 4}, {"5", 1, 1, 5}, {"6", 1, 2, 6},
        {"7", 2, 0, 7}, {"8", 2, 1, 8}, {"9", 2, 2, 9},
                        {"0", 3, 1, 0},
    };
    for (size_t i = 0; i < sizeof(digits)/sizeof(digits[0]); i++) {
        lv_obj_t *b = make_key(grid, digits[i].txt, on_digit, ctx, digits[i].digit);
        lv_obj_set_grid_cell(b, LV_GRID_ALIGN_STRETCH, digits[i].col, 1,
                             LV_GRID_ALIGN_STRETCH, digits[i].row, 1);
    }
    lv_obj_t *bs = make_key(grid, LV_SYMBOL_BACKSPACE, on_backspace, ctx, -1);
    lv_obj_set_grid_cell(bs, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 3, 1);
    ctx->done_btn = make_key(grid, LV_SYMBOL_OK, on_done_clicked, ctx, -1);
    lv_obj_set_grid_cell(ctx->done_btn, LV_GRID_ALIGN_STRETCH, 2, 1,
                         LV_GRID_ALIGN_STRETCH, 3, 1);

    refresh_value_label(ctx);
}
