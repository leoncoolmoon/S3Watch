// Calculator app — launched from app picker into the row-2 app tile (1,2).
//
// State persists across screen destroy/recreate; all logic lives in
// calc_engine.c so it can be exercised by host unit tests.

#include "calculator_app.h"
#include "calc_engine.h"
#include "ui_fonts.h"
#include "lvgl.h"
#include <string.h>

// ---------------------------------------------------------------------------
// Live UI pointers — NULL while screen is not mounted
// ---------------------------------------------------------------------------

static lv_obj_t *s_lbl_expr = NULL;
static lv_obj_t *s_lbl_num  = NULL;

// ---------------------------------------------------------------------------
// Display refresh
// ---------------------------------------------------------------------------

static void refresh_display(void)
{
    if (!s_lbl_num) return;
    lv_label_set_text(s_lbl_num, calc_display());
    char expr[56];
    calc_get_expr(expr, sizeof(expr));
    lv_label_set_text(s_lbl_expr, expr);
}

// ---------------------------------------------------------------------------
// Button type enum + button grid (rows 1-4: 4 each; row 5: 3, zero wide)
// ---------------------------------------------------------------------------

typedef enum {
    BT_DIGIT, BT_DOT,
    BT_CLEAR, BT_SIGN, BT_PCT,
    BT_DIV, BT_MUL, BT_SUB, BT_ADD, BT_EQ,
} btn_type_t;

typedef struct { btn_type_t type; int val; const char *label; } btn_def_t;

// "\xc3\xb7" = ÷, "\xc3\x97" = ×, "\xc2\xb1" = ±
static const btn_def_t s_btns[] = {
    {BT_CLEAR,0,"C"},        {BT_SIGN,0,"\xc2\xb1"}, {BT_PCT,0,"%"},       {BT_DIV,0,"\xc3\xb7"},
    {BT_DIGIT,7,"7"},        {BT_DIGIT,8,"8"},        {BT_DIGIT,9,"9"},     {BT_MUL,0,"\xc3\x97"},
    {BT_DIGIT,4,"4"},        {BT_DIGIT,5,"5"},        {BT_DIGIT,6,"6"},     {BT_SUB,0,"-"},
    {BT_DIGIT,1,"1"},        {BT_DIGIT,2,"2"},        {BT_DIGIT,3,"3"},     {BT_ADD,0,"+"},
    {BT_DIGIT,0,"0"},        {BT_DOT,0,"."},           {BT_EQ,0,"="},
};

// ---------------------------------------------------------------------------
// Button press handler
// ---------------------------------------------------------------------------

static void btn_cb(lv_event_t *e)
{
    const btn_def_t *def = lv_event_get_user_data(e);
    lv_indev_wait_release(lv_indev_active());

    switch (def->type) {
    case BT_DIGIT: calc_digit(def->val); break;
    case BT_DOT:   calc_dot();           break;
    case BT_CLEAR: calc_clear();         break;
    case BT_SIGN:  calc_sign();          break;
    case BT_PCT:   calc_pct();           break;
    case BT_ADD:   calc_op(OP_ADD);      break;
    case BT_SUB:   calc_op(OP_SUB);      break;
    case BT_MUL:   calc_op(OP_MUL);      break;
    case BT_DIV:   calc_op(OP_DIV);      break;
    case BT_EQ:    calc_eq();            break;
    }

    refresh_display();
}

// ---------------------------------------------------------------------------
// Screen construction helpers
// ---------------------------------------------------------------------------

static void calc_on_delete(lv_event_t *e)
{
    (void)e;
    s_lbl_expr = s_lbl_num = NULL;
}

static lv_obj_t *make_btn(lv_obj_t *parent, const btn_def_t *def, int grow)
{
    lv_color_t bg, fg;
    switch (def->type) {
    case BT_CLEAR: case BT_SIGN: case BT_PCT:
        bg = lv_color_hex(0x9E9E9E); fg = lv_color_black(); break;
    case BT_DIV: case BT_MUL: case BT_SUB: case BT_ADD: case BT_EQ:
        bg = lv_color_hex(0xFF9500); fg = lv_color_white(); break;
    default:
        bg = lv_color_hex(0x333333); fg = lv_color_white(); break;
    }

    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_height(btn, lv_pct(100));
    lv_obj_set_flex_grow(btn, grow);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, btn_cb, LV_EVENT_CLICKED, (void *)def);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &font_bold_32, 0);
    lv_obj_set_style_text_color(lbl, fg, 0);
    lv_label_set_text(lbl, def->label);
    lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
    return btn;
}

static lv_obj_t *make_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_flex_grow(row, 1);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, 0);
    return row;
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

void calculator_app_create(lv_obj_t *parent)
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
    lv_obj_set_style_pad_left(screen, 8, 0);
    lv_obj_set_style_pad_right(screen, 8, 0);
    lv_obj_add_event_cb(screen, calc_on_delete, LV_EVENT_DELETE, NULL);

    // Display area — right-aligned expression + number labels
    lv_obj_t *disp = lv_obj_create(screen);
    lv_obj_remove_style_all(disp);
    lv_obj_set_width(disp, lv_pct(100));
    lv_obj_set_height(disp, 100);
    lv_obj_set_style_bg_opa(disp, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(disp, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(disp, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(disp, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_right(disp, 10, 0);
    lv_obj_set_style_pad_top(disp, 8, 0);
    lv_obj_set_style_pad_bottom(disp, 6, 0);

    s_lbl_expr = lv_label_create(disp);
    lv_obj_set_width(s_lbl_expr, lv_pct(100));
    lv_obj_set_style_text_font(s_lbl_expr, &font_normal_26, 0);
    lv_obj_set_style_text_color(s_lbl_expr, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_align(s_lbl_expr, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(s_lbl_expr, "");

    s_lbl_num = lv_label_create(disp);
    lv_obj_set_width(s_lbl_num, lv_pct(100));
    lv_obj_set_style_text_font(s_lbl_num, &font_bold_42, 0);
    lv_obj_set_style_text_color(s_lbl_num, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_lbl_num, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(s_lbl_num, "");

    // Button grid — fills remaining height
    lv_obj_t *grid = lv_obj_create(screen);
    lv_obj_remove_style_all(grid);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(grid, 6, 0);
    lv_obj_set_style_pad_bottom(grid, 8, 0);

    // Rows 1–4: 4 equal buttons each
    for (int r = 0; r < 4; r++) {
        lv_obj_t *row = make_row(grid);
        for (int c = 0; c < 4; c++)
            make_btn(row, &s_btns[r * 4 + c], 1);
    }

    // Row 5: 0 (double-wide), . , =
    lv_obj_t *row5 = make_row(grid);
    make_btn(row5, &s_btns[16], 2);   // 0 — flex_grow 2
    make_btn(row5, &s_btns[17], 1);   // .
    make_btn(row5, &s_btns[18], 1);   // =

    refresh_display();
}
