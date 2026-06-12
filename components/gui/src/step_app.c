// Step counter app — displays today's step count big, with week / lifetime totals
// and best-day / best-week records below. Steps are detected by imu_manager's
// software counter and aggregated/persisted by step_tracker; this screen just polls
// step_tracker once a second while open. See doc/writing-an-app.md.

#include "step_app.h"
#include "ui_fonts.h"
#include "step_tracker.h"
#include "settings.h"
#include "lvgl.h"
#include <stdio.h>

// ── Live UI pointers — NULL while screen is not mounted ──────────────────────
static lv_obj_t   *s_today_lbl = NULL;
static lv_obj_t   *s_week_lbl  = NULL;
static lv_obj_t   *s_life_lbl  = NULL;
static lv_obj_t   *s_bday_lbl  = NULL;
static lv_obj_t   *s_bweek_lbl = NULL;
static lv_timer_t *s_timer     = NULL;

// ── Logic ────────────────────────────────────────────────────────────────────

// Format an integer with thousands separators (e.g. 2104887 → "2,104,887").
static void fmt_grouped(uint32_t v, char *out, size_t n)
{
    char d[12];
    int len = snprintf(d, sizeof(d), "%lu", (unsigned long)v);
    int o = 0;
    for (int i = 0; i < len && o < (int)n - 1; i++) {
        if (i > 0 && (len - i) % 3 == 0 && o < (int)n - 1) out[o++] = ',';
        out[o++] = d[i];
    }
    out[o] = '\0';
}

static void set_stat(lv_obj_t *lbl, const char *name, uint32_t v)
{
    if (!lbl) return;
    char num[16];
    fmt_grouped(v, num, sizeof(num));
    lv_label_set_text_fmt(lbl, "%s  %s", name, num);
}

static void refresh(void)
{
    if (s_today_lbl) {
        char num[16];
        fmt_grouped(step_tracker_today(), num, sizeof(num));
        lv_label_set_text(s_today_lbl, num);
    }
    set_stat(s_week_lbl,  "Week",      step_tracker_week());
    set_stat(s_life_lbl,  "Total",     step_tracker_lifetime());
    set_stat(s_bday_lbl,  "Best day",  step_tracker_best_day());
    set_stat(s_bweek_lbl, "Best week", step_tracker_best_week());
}

static void timer_cb(lv_timer_t *t) { (void)t; refresh(); }

// ── Lifecycle ────────────────────────────────────────────────────────────────

static void on_delete(lv_event_t *e)
{
    (void)e;
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    s_today_lbl = s_week_lbl = s_life_lbl = s_bday_lbl = s_bweek_lbl = NULL;
}

static lv_obj_t *make_stat_row(lv_obj_t *parent)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, &font_normal_26, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xAAAAAA), 0);
    return l;
}

// ── Screen construction ──────────────────────────────────────────────────────

void step_app_create(lv_obj_t *parent)
{
    lv_obj_t *screen = lv_obj_create(parent);
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, lv_pct(100), lv_pct(100));
    lv_obj_center(screen);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(screen, on_delete, LV_EVENT_DELETE, NULL);

    lv_obj_t *title = lv_label_create(screen);
    lv_obj_set_style_text_font(title, &font_bold_32, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x888888), 0);
    lv_obj_set_style_pad_bottom(title, 2, 0);
    lv_label_set_text(title, "Steps");

    if (settings_get_step_counter_enabled()) {
        // Today's count, big.
        s_today_lbl = lv_label_create(screen);
        lv_obj_set_style_text_font(s_today_lbl, &font_numbers_80, 0);
        lv_obj_set_style_text_color(s_today_lbl, lv_color_white(), 0);
        lv_label_set_text(s_today_lbl, "0");

        // Totals + records, small.
        s_week_lbl  = make_stat_row(screen);
        lv_obj_set_style_pad_top(s_week_lbl, 8, 0);   // gap under the big number
        s_life_lbl  = make_stat_row(screen);
        s_bday_lbl  = make_stat_row(screen);
        s_bweek_lbl = make_stat_row(screen);

        if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
        s_timer = lv_timer_create(timer_cb, 1000, NULL);
        refresh();
    } else {
        // Disabled — prompt the user (font_numbers_* has digits only, so use a
        // text font here).
        lv_obj_t *hint = lv_label_create(screen);
        lv_obj_set_style_text_font(hint, &font_bold_42, 0);
        lv_obj_set_style_text_color(hint, lv_color_white(), 0);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(hint, lv_pct(78));
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(hint, "Enable the Step Counter in Settings");
    }
}
