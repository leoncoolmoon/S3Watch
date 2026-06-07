// World Clock — launched from the app picker into the row-2 app tile (1,2).
// Displays a scrollable list of world cities and their current local times.
// Times update every second. DST is handled via POSIX tz strings.

#include "world_clock.h"
#include "ui_fonts.h"
#include "settings.h"
#include "esp_log.h"
#include "lvgl.h"
#include <time.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "world_clock";

// ---------------------------------------------------------------------------
// City table — POSIX TZ strings with DST rules
// ---------------------------------------------------------------------------

typedef struct {
    const char *city;
    const char *tz_posix;
} wc_city_t;

static const wc_city_t s_cities[] = {
    { "UTC",           "UTC0"                                    },
    { "London",        "GMT0BST,M3.5.0/1,M10.5.0"               },
    { "Paris",         "CET-1CEST,M3.5.0,M10.5.0/3"             },
    { "Dubai",         "GST-4"                                   },
    { "Mumbai",        "IST-5:30"                                },
    { "Singapore",     "SGT-8"                                   },
    { "Tokyo",         "JST-9"                                   },
    { "Sydney",        "AEST-10AEDT,M10.1.0,M4.1.0/3"           },
    { "Auckland",      "NZST-12NZDT,M9.5.0,M4.1.0/3"            },
    { "Los Angeles",   "PST8PDT,M3.2.0,M11.1.0"                 },
    { "Chicago",       "CST6CDT,M3.2.0,M11.1.0"                 },
    { "New York",      "EST5EDT,M3.2.0,M11.1.0"                 },
    { "São Paulo",     "BRT3"                                    },
};
#define CITY_COUNT ((int)(sizeof(s_cities) / sizeof(s_cities[0])))

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static lv_obj_t  *s_time_lbl[CITY_COUNT];
static lv_obj_t  *s_day_lbl[CITY_COUNT];
static lv_timer_t *s_timer = NULL;

// ---------------------------------------------------------------------------
// Time computation — temporarily sets TZ for each city, then restores
// the user's configured TZ. Runs on the LVGL thread; the window where
// TZ differs from the user's setting is sub-millisecond per city.
// ---------------------------------------------------------------------------

static void get_city_tm(const char *tz_posix, int *hour, int *min, int *mday)
{
    time_t now = time(NULL);
    struct tm tm_city;
    setenv("TZ", tz_posix, 1);
    tzset();
    localtime_r(&now, &tm_city);
    *hour = tm_city.tm_hour;
    *min  = tm_city.tm_min;
    *mday = tm_city.tm_mday;
}

static void update_times(void)
{
    bool use_24h = settings_get_time_24h();

    // Home mday for day-offset badge
    time_t now = time(NULL);
    struct tm home_tm;
    {
        const char *home_tz = settings_get_tz();
        setenv("TZ", home_tz, 1);
        tzset();
        localtime_r(&now, &home_tm);
    }
    int home_mday = home_tm.tm_mday;

    for (int i = 0; i < CITY_COUNT; i++) {
        if (!s_time_lbl[i]) continue;

        int h, m, mday;
        get_city_tm(s_cities[i].tz_posix, &h, &m, &mday);

        char tbuf[16];
        if (use_24h) {
            snprintf(tbuf, sizeof(tbuf), "%02d:%02d", h, m);
        } else {
            int h12 = h % 12;
            if (!h12) h12 = 12;
            snprintf(tbuf, sizeof(tbuf), "%d:%02d %s", h12, m, h >= 12 ? "PM" : "AM");
        }
        lv_label_set_text(s_time_lbl[i], tbuf);

        // Day offset badge (+1 / -1 when the city is on a different calendar day)
        if (s_day_lbl[i]) {
            int diff = mday - home_mday;
            if (diff > 1)  diff = -1;   // month wrap-around (e.g. 31 → 1)
            if (diff < -1) diff = 1;
            if (diff == 0) {
                lv_label_set_text(s_day_lbl[i], "");
            } else {
                char dbuf[5];
                snprintf(dbuf, sizeof(dbuf), "%+d", diff);
                lv_label_set_text(s_day_lbl[i], dbuf);
            }
        }
    }

    // Restore user's TZ
    setenv("TZ", settings_get_tz(), 1);
    tzset();
}

static void wc_timer_cb(lv_timer_t *t) { (void)t; update_times(); }

static void wc_on_delete(lv_event_t *e)
{
    (void)e;
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    for (int i = 0; i < CITY_COUNT; i++) { s_time_lbl[i] = s_day_lbl[i] = NULL; }
    ESP_LOGI(TAG, "world clock deleted");
}

// ---------------------------------------------------------------------------
// Screen construction
// ---------------------------------------------------------------------------

void world_clock_create(lv_obj_t *parent)
{
    for (int i = 0; i < CITY_COUNT; i++) { s_time_lbl[i] = s_day_lbl[i] = NULL; }

    lv_obj_t *screen = lv_obj_create(parent);
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, lv_pct(100), lv_pct(100));
    lv_obj_center(screen);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(screen, wc_on_delete, LV_EVENT_DELETE, NULL);

    // Title bar
    lv_obj_t *title = lv_label_create(screen);
    lv_obj_set_style_text_font(title, &font_bold_32, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_pad_top(title, 16, 0);
    lv_obj_set_style_pad_bottom(title, 10, 0);
    lv_label_set_text(title, "World Clock");
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    // Divider
    lv_obj_t *div = lv_obj_create(screen);
    lv_obj_remove_style_all(div);
    lv_obj_set_size(div, lv_pct(90), 1);
    lv_obj_set_style_bg_color(div, lv_color_hex(0x444444), 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);

    // Scrollable city list
    lv_obj_t *list = lv_obj_create(screen);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(list, 16, 0);
    lv_obj_set_style_pad_right(list, 16, 0);
    lv_obj_set_style_pad_top(list, 4, 0);
    lv_obj_set_style_pad_bottom(list, 8, 0);
    lv_obj_set_style_pad_row(list, 0, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < CITY_COUNT; i++) {
        lv_obj_t *row = lv_obj_create(list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(100), 38);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // Alternating row tint
        if (i % 2 == 0) {
            lv_obj_set_style_bg_color(row, lv_color_hex(0xffffff), 0);
            lv_obj_set_style_bg_opa(row, 12, 0);
            lv_obj_set_style_radius(row, 6, 0);
        }

        // City name
        lv_obj_t *city_lbl = lv_label_create(row);
        lv_obj_set_style_text_font(city_lbl, &font_normal_26, 0);
        lv_obj_set_style_text_color(city_lbl, lv_color_hex(0xCCCCCC), 0);
        lv_obj_set_style_pad_left(city_lbl, 4, 0);
        lv_label_set_text(city_lbl, s_cities[i].city);
        lv_obj_set_flex_grow(city_lbl, 1);

        // Day offset badge (+1 / -1) — hidden when same day
        lv_obj_t *day_lbl = lv_label_create(row);
        lv_obj_set_style_text_font(day_lbl, &font_normal_26, 0);
        lv_obj_set_style_text_color(day_lbl, lv_color_hex(0x888888), 0);
        lv_obj_set_style_pad_right(day_lbl, 4, 0);
        lv_obj_set_width(day_lbl, 36);
        lv_obj_set_style_text_align(day_lbl, LV_TEXT_ALIGN_RIGHT, 0);
        lv_label_set_text(day_lbl, "");
        s_day_lbl[i] = day_lbl;

        // Time
        lv_obj_t *time_lbl = lv_label_create(row);
        lv_obj_set_style_text_font(time_lbl, &font_normal_26, 0);
        lv_obj_set_style_text_color(time_lbl, lv_color_white(), 0);
        lv_obj_set_style_pad_right(time_lbl, 4, 0);
        lv_obj_set_width(time_lbl, 80);
        lv_obj_set_style_text_align(time_lbl, LV_TEXT_ALIGN_RIGHT, 0);
        lv_label_set_text(time_lbl, "--:--");
        s_time_lbl[i] = time_lbl;
    }

    // Populate immediately, then refresh every second
    update_times();
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    s_timer = lv_timer_create(wc_timer_cb, 1000, NULL);
}
