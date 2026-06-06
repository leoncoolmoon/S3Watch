#include "scroll_keyboard.h"
#include "ui_fonts.h"
#include <string.h>
#include <stdlib.h>

#define MAX_TEXT_LEN  64
#define KEY_W         72
#define KEY_H         72
#define KEY_GAP        8

// Character sets for each mode
static const char *MODE_LOWER  = "abcdefghijklmnopqrstuvwxyz0123456789.-_@";
static const char *MODE_UPPER  = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_@";
static const char *MODE_SYM    = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~0123456789";

typedef struct {
    char text[MAX_TEXT_LEN + 1];
    int  mode;              // 0=lower 1=upper 2=sym
    const char *charset;

    lv_obj_t *text_label;
    lv_obj_t *scroll_cont;
    lv_obj_t *mode_btn_label;

    scroll_kb_done_cb_t   on_done;
    scroll_kb_cancel_cb_t on_cancel;
    void *user_data;
} kb_ctx_t;

static void rebuild_strip(kb_ctx_t *ctx);

static int get_center_index(lv_obj_t *cont)
{
    int32_t scroll = lv_obj_get_scroll_x(cont);
    int32_t center_x = scroll + lv_obj_get_width(cont) / 2;
    int child_cnt = lv_obj_get_child_count(cont);
    for (int i = 0; i < child_cnt; i++) {
        lv_obj_t *child = lv_obj_get_child(cont, i);
        int32_t cx = lv_obj_get_x(child) + lv_obj_get_width(child) / 2;
        if (abs((int)(cx - center_x)) <= KEY_W / 2) return i;
    }
    return 0;
}

static void highlight_center(lv_obj_t *cont)
{
    int center = get_center_index(cont);
    int child_cnt = lv_obj_get_child_count(cont);
    for (int i = 0; i < child_cnt; i++) {
        lv_obj_t *child = lv_obj_get_child(cont, i);
        if (i == center) {
            lv_obj_set_style_bg_color(child, lv_color_hex(0xF0B000), 0);
            lv_obj_set_style_bg_opa(child, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(child, lv_color_black(), 0);
            lv_obj_set_style_transform_scale(child, 280, 0); // ~110%
        } else {
            lv_obj_set_style_bg_color(child, lv_color_hex(0x303030), 0);
            lv_obj_set_style_bg_opa(child, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(child, lv_color_hex(0xD0D0D0), 0);
            lv_obj_set_style_transform_scale(child, 256, 0); // 100%
        }
    }
}

static void scroll_cb(lv_event_t *e)
{
    lv_obj_t *cont = lv_event_get_target(e);
    highlight_center(cont);
}

static void key_tap_cb(lv_event_t *e)
{
    lv_obj_t *key  = lv_event_get_target(e);
    kb_ctx_t *ctx  = (kb_ctx_t*)lv_event_get_user_data(e);
    lv_obj_t *cont = lv_obj_get_parent(key);

    // Scroll so this key is centered, then register as selected
    lv_obj_scroll_to_view(key, LV_ANIM_ON);

    int idx = get_center_index(cont);
    // Slight delay: wait for snap. Use the idx of the tapped child instead.
    int child_cnt = lv_obj_get_child_count(cont);
    for (int i = 0; i < child_cnt; i++) {
        if (lv_obj_get_child(cont, i) == key) { idx = i; break; }
    }

    const char *ch = lv_label_get_text(key);
    int len = strlen(ctx->text);
    if (len < MAX_TEXT_LEN) {
        ctx->text[len]     = ch[0];
        ctx->text[len + 1] = '\0';
        lv_label_set_text(ctx->text_label, ctx->text);
    }
    (void)idx;
}

static void backspace_cb(lv_event_t *e)
{
    kb_ctx_t *ctx = (kb_ctx_t*)lv_event_get_user_data(e);
    int len = strlen(ctx->text);
    if (len > 0) {
        ctx->text[len - 1] = '\0';
        lv_label_set_text(ctx->text_label, ctx->text);
    }
}

static void space_cb(lv_event_t *e)
{
    kb_ctx_t *ctx = (kb_ctx_t*)lv_event_get_user_data(e);
    int len = strlen(ctx->text);
    if (len < MAX_TEXT_LEN) {
        ctx->text[len]     = ' ';
        ctx->text[len + 1] = '\0';
        lv_label_set_text(ctx->text_label, ctx->text);
    }
}

static void done_cb(lv_event_t *e)
{
    kb_ctx_t *ctx = (kb_ctx_t*)lv_event_get_user_data(e);
    // on_done typically tears down the screen that owns this keyboard (and
    // therefore this button). It must do so via lv_obj_del_async so the
    // teardown happens AFTER this event finishes — we must not touch the
    // event target or any keyboard object after this call returns.
    if (ctx->on_done) ctx->on_done(ctx->text, ctx->user_data);
    // Free our own context after the current event/async cycle completes.
    lv_async_call((lv_async_cb_t)(void(*)(void*))free, ctx);
}

static void cancel_cb(lv_event_t *e)
{
    kb_ctx_t *ctx = (kb_ctx_t*)lv_event_get_user_data(e);
    if (ctx->on_cancel) ctx->on_cancel(ctx->user_data);
    lv_async_call((lv_async_cb_t)(void(*)(void*))free, ctx);
}

static void mode_cb(lv_event_t *e)
{
    kb_ctx_t *ctx = (kb_ctx_t*)lv_event_get_user_data(e);
    ctx->mode = (ctx->mode + 1) % 3;
    switch (ctx->mode) {
        case 0: ctx->charset = MODE_LOWER; lv_label_set_text(ctx->mode_btn_label, "ABC"); break;
        case 1: ctx->charset = MODE_UPPER; lv_label_set_text(ctx->mode_btn_label, "#+="); break;
        case 2: ctx->charset = MODE_SYM;   lv_label_set_text(ctx->mode_btn_label, "abc"); break;
    }
    rebuild_strip(ctx);
}

static lv_obj_t *make_ctrl_btn(lv_obj_t *parent, const char *label_text,
                                lv_event_cb_t cb, kb_ctx_t *ctx,
                                lv_color_t bg)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 88, 52);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ctx);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_font(lbl, &font_normal_26, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_center(lbl);
    return btn;
}

static void rebuild_strip(kb_ctx_t *ctx)
{
    lv_obj_clean(ctx->scroll_cont);

    int screen_w = lv_obj_get_width(lv_scr_act());
    int padding  = screen_w / 2 - KEY_W / 2;

    // Leading spacer so first character can be centered
    lv_obj_t *sp1 = lv_obj_create(ctx->scroll_cont);
    lv_obj_remove_style_all(sp1);
    lv_obj_set_size(sp1, padding, KEY_H);

    const char *cs = ctx->charset;
    int n = strlen(cs);
    char buf[2] = {0, 0};
    for (int i = 0; i < n; i++) {
        buf[0] = cs[i];
        lv_obj_t *key = lv_label_create(ctx->scroll_cont);
        lv_label_set_text(key, buf);
        lv_obj_set_size(key, KEY_W, KEY_H);
        lv_obj_set_style_text_font(key, &font_bold_42, 0);
        lv_obj_set_style_text_align(key, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_bg_color(key, lv_color_hex(0x303030), 0);
        lv_obj_set_style_bg_opa(key, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(key, 10, 0);
        lv_obj_set_style_text_color(key, lv_color_hex(0xD0D0D0), 0);
        lv_obj_set_style_pad_top(key, (KEY_H - 42) / 2, 0);
        lv_obj_add_flag(key, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(key, key_tap_cb, LV_EVENT_CLICKED, ctx);
    }

    // Trailing spacer
    lv_obj_t *sp2 = lv_obj_create(ctx->scroll_cont);
    lv_obj_remove_style_all(sp2);
    lv_obj_set_size(sp2, padding, KEY_H);

    // Scroll to first real character (index 1 = first char after spacer)
    lv_obj_t *first = lv_obj_get_child(ctx->scroll_cont, 1);
    if (first) lv_obj_scroll_to_view(first, LV_ANIM_OFF);

    highlight_center(ctx->scroll_cont);
}

void scroll_keyboard_create(lv_obj_t *parent,
                             const char *initial_text,
                             const char *hint,
                             scroll_kb_done_cb_t   on_done,
                             scroll_kb_cancel_cb_t on_cancel,
                             void *user_data)
{
    kb_ctx_t *ctx = calloc(1, sizeof(kb_ctx_t));
    if (!ctx) return;

    ctx->mode    = 0;
    ctx->charset = MODE_LOWER;
    ctx->on_done   = on_done;
    ctx->on_cancel = on_cancel;
    ctx->user_data = user_data;

    if (initial_text) {
        strncpy(ctx->text, initial_text, MAX_TEXT_LEN);
        ctx->text[MAX_TEXT_LEN] = '\0';
    }

    // Root container — full screen, black bg
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    // ── Hint label ────────────────────────────────────────────────────────────
    lv_obj_t *hint_lbl = lv_label_create(root);
    lv_label_set_text(hint_lbl, hint ? hint : "");
    lv_obj_set_style_text_font(hint_lbl, &font_normal_26, 0);
    lv_obj_set_style_text_color(hint_lbl, lv_color_hex(0x808080), 0);
    lv_obj_set_pos(hint_lbl, 12, 10);

    // ── Text input display ────────────────────────────────────────────────────
    lv_obj_t *text_box = lv_obj_create(root);
    lv_obj_remove_style_all(text_box);
    lv_obj_set_size(text_box, lv_pct(100) - 24, 50);
    lv_obj_set_pos(text_box, 12, 40);
    lv_obj_set_style_bg_color(text_box, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(text_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(text_box, 8, 0);
    lv_obj_set_style_pad_left(text_box, 8, 0);
    lv_obj_set_style_pad_ver(text_box, 6, 0);
    lv_obj_clear_flag(text_box, LV_OBJ_FLAG_SCROLLABLE);

    ctx->text_label = lv_label_create(text_box);
    lv_label_set_text(ctx->text_label, ctx->text);
    lv_obj_set_style_text_font(ctx->text_label, &font_normal_32, 0);
    lv_obj_set_style_text_color(ctx->text_label, lv_color_white(), 0);
    lv_obj_set_align(ctx->text_label, LV_ALIGN_LEFT_MID);
    lv_label_set_long_mode(ctx->text_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(ctx->text_label, lv_pct(100));

    // ── Scroll strip ──────────────────────────────────────────────────────────
    int screen_w = lv_obj_get_width(lv_scr_act());
    ctx->scroll_cont = lv_obj_create(root);
    lv_obj_remove_style_all(ctx->scroll_cont);
    lv_obj_set_size(ctx->scroll_cont, screen_w, KEY_H + KEY_GAP * 2);
    lv_obj_align(ctx->scroll_cont, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_opa(ctx->scroll_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_scroll_dir(ctx->scroll_cont, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(ctx->scroll_cont, LV_SCROLL_SNAP_CENTER);
    lv_obj_clear_flag(ctx->scroll_cont, LV_OBJ_FLAG_SCROLL_ELASTIC |
                                        LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_flex_flow(ctx->scroll_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(ctx->scroll_cont, KEY_GAP, 0);
    lv_obj_set_style_pad_ver(ctx->scroll_cont, KEY_GAP, 0);
    lv_obj_add_event_cb(ctx->scroll_cont, scroll_cb, LV_EVENT_SCROLL, NULL);

    rebuild_strip(ctx);

    // ── Bottom controls ───────────────────────────────────────────────────────
    lv_obj_t *ctrl_row = lv_obj_create(root);
    lv_obj_remove_style_all(ctrl_row);
    lv_obj_set_size(ctrl_row, lv_pct(100), 60);
    lv_obj_align(ctrl_row, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_bg_opa(ctrl_row, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(ctrl_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctrl_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(ctrl_row, LV_OBJ_FLAG_SCROLLABLE);

    // Mode toggle button — store label ptr so mode_cb can update it
    lv_obj_t *mode_btn = make_ctrl_btn(ctrl_row, "ABC", mode_cb, ctx,
                                        lv_color_hex(0x444444));
    ctx->mode_btn_label = lv_obj_get_child(mode_btn, 0);

    make_ctrl_btn(ctrl_row, "SPC",  space_cb,     ctx, lv_color_hex(0x444444));
    make_ctrl_btn(ctrl_row, "⌫",   backspace_cb,  ctx, lv_color_hex(0x664444));
    make_ctrl_btn(ctrl_row, "✓",   done_cb,       ctx, lv_color_hex(0x226622));
}
