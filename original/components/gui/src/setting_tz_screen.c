// Timezone preset picker. The list maps a friendly display name to a POSIX
// TZ string. Add more entries by appending to s_presets below — no other
// code needs to change.
//
// POSIX format reference:
//   STD[offset[DST[offset][,start[/time],end[/time]]]]
// Offset is the time you ADD to local to get UTC (i.e. negative of UTC offset).
// US Eastern is UTC-5 standard → "EST5EDT,M3.2.0/2,M11.1.0/2".

#include "setting_tz_screen.h"
#include "ui.h"
#include "ui_fonts.h"
#include "settings.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "TimeZone";

typedef struct {
    const char* name;       // display name shown in the picker
    const char* posix_tz;   // value stored / applied via setenv
} tz_preset_t;

static const tz_preset_t s_presets[] = {
    { "UTC",          "UTC0" },
    { "US Eastern",   "EST5EDT,M3.2.0/2,M11.1.0/2" },
    { "US Central",   "CST6CDT,M3.2.0/2,M11.1.0/2" },
    { "US Mountain",  "MST7MDT,M3.2.0/2,M11.1.0/2" },
    { "US Pacific",   "PST8PDT,M3.2.0/2,M11.1.0/2" },
    { "US Alaska",    "AKST9AKDT,M3.2.0/2,M11.1.0/2" },
    { "US Hawaii",    "HST10" },
    { "UK / Ireland", "GMT0BST,M3.5.0/1,M10.5.0/2" },
    { "Europe (CET)", "CET-1CEST,M3.5.0/2,M10.5.0/3" },
    { "Europe (EET)", "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Japan / Korea","JST-9" },
    { "China",        "CST-8" },
    { "India",        "IST-5:30" },
    { "Australia (E)","AEST-10AEDT,M10.1.0/2,M4.1.0/3" },
    { "New Zealand",  "NZST-12NZDT,M9.5.0/2,M4.1.0/3" },
};
static const int s_preset_count = sizeof(s_presets) / sizeof(s_presets[0]);

static lv_obj_t* s_screen;
static lv_obj_t* s_content;

static void on_delete(lv_event_t* e) {
    (void)e;
    s_screen = NULL;
    s_content = NULL;
}

static int current_index(void) {
    const char* cur = settings_get_tz();
    if (!cur) return 0;
    for (int i = 0; i < s_preset_count; i++) {
        if (strcmp(cur, s_presets[i].posix_tz) == 0) return i;
    }
    return 0;  // unknown / custom — default to UTC selection visually
}

static void refresh_checked(void) {
    if (!s_content) return;
    int cur = current_index();
    lv_obj_t* selected = NULL;
    for (uint32_t i = 0; i < lv_obj_get_child_count(s_content); ++i) {
        lv_obj_t* row = lv_obj_get_child(s_content, i);
        int idx = (int)(intptr_t)lv_obj_get_user_data(row);
        if (idx == cur) {
            lv_obj_add_state(row, LV_STATE_CHECKED);
            selected = row;
        } else {
            lv_obj_remove_state(row, LV_STATE_CHECKED);
        }
    }
    if (selected) lv_obj_scroll_to_view(selected, LV_ANIM_ON);
}

static void screen_events(lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_GESTURE) {
        if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_RIGHT) {
            lv_indev_wait_release(lv_indev_active());
            ui_dynamic_subtile_close();
            s_screen = NULL;
        }
    } else if (lv_event_get_code(e) == LV_EVENT_SCREEN_LOADED) {
        refresh_checked();
    }
}

static void choose(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_preset_count) return;
    ESP_LOGI(TAG, "TZ selected: %s (%s)", s_presets[idx].name, s_presets[idx].posix_tz);
    settings_set_tz(s_presets[idx].posix_tz);
    refresh_checked();
}

static lv_obj_t* make_opt(lv_obj_t* parent, const char* txt, int idx) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, 60);
    lv_obj_set_style_bg_opa(row, LV_OPA_10, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_pad_all(row, 12, 0);
    lv_obj_set_style_margin_bottom(row, 10, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, choose, LV_EVENT_CLICKED, (void*)(intptr_t)idx);
    lv_obj_set_user_data(row, (void*)(intptr_t)idx);
    lv_obj_t* l = lv_label_create(row);
    lv_obj_set_style_text_font(l, &font_bold_32, 0);
    lv_label_set_text(l, txt);
    return row;
}

void setting_tz_screen_create(lv_obj_t* parent) {
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_text_color(&style, lv_color_white());
    lv_style_set_bg_color(&style, lv_color_black());
    lv_style_set_bg_opa(&style, LV_OPA_COVER);

    s_screen = lv_obj_create(parent);
    lv_obj_remove_style_all(s_screen);
    lv_obj_add_style(s_screen, &style, 0);
    lv_obj_set_size(s_screen, lv_pct(100), lv_pct(100));
    lv_obj_add_event_cb(s_screen, screen_events, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(s_screen, screen_events, LV_EVENT_SCREEN_LOADED, NULL);
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_screen, on_delete, LV_EVENT_DELETE, NULL);

    lv_obj_t* hdr = lv_obj_create(s_screen);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_t* title = lv_label_create(hdr);
    lv_obj_set_style_text_font(title, &font_bold_32, 0);
    lv_label_set_text(title, "Time Zone");

    s_content = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_content);
    lv_obj_set_size(s_content, lv_pct(100), lv_pct(80));
    lv_obj_add_flag(s_content, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_pad_top(s_content, 80, 0);
    lv_obj_set_style_pad_bottom(s_content, 10, 0);
    lv_obj_set_style_pad_left(s_content, 12, 0);
    lv_obj_set_style_pad_right(s_content, 12, 0);
    lv_obj_set_flex_flow(s_content, LV_FLEX_FLOW_COLUMN);
    // START on the main axis so the first row is reachable by scroll
    // (same gotcha that caught us in settings_menu_screen).
    lv_obj_set_flex_align(s_content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(s_content, LV_DIR_VER);

    for (int i = 0; i < s_preset_count; i++) {
        lv_obj_t* row = make_opt(s_content, s_presets[i].name, i);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x438bff), LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(row, 255, LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(row, lv_color_white(), LV_PART_MAIN | LV_STATE_CHECKED);
    }

    refresh_checked();
}
