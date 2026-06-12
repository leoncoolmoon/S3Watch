// App picker — swipe-up destination (0,2).
// Displays a grid of launchable apps. Tapping an icon acquires the row-2
// dynamic tile at (1,2), populates it with the app's screen, and navigates
// there. Swipe left or back button from (1,2) returns here.

#include "app_picker.h"
#include "ui.h"
#include "ui_fonts.h"
#include "settings.h"
#include "signalk_dashboard.h"
#include "world_clock.h"
#include "stopwatch.h"
#include "alarm_clock.h"
#include "calendar_app.h"
#include "music_app.h"
#include "calculator_app.h"
#include "step_app.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "app_picker";

static lv_obj_t *s_bg_img = NULL;  // background image object, lives for tile lifetime

// ---------------------------------------------------------------------------
// App registry
// ---------------------------------------------------------------------------

typedef struct {
    const char           *name;
    const lv_image_dsc_t *icon;
    void (*launch)(lv_obj_t *tile);
} app_entry_t;

LV_IMAGE_DECLARE(image_signalk_icon);
LV_IMAGE_DECLARE(image_worldclock_icon);
LV_IMAGE_DECLARE(image_stopwatch_icon);
LV_IMAGE_DECLARE(image_alarmclock_icon);
LV_IMAGE_DECLARE(image_calendar_icon);
LV_IMAGE_DECLARE(image_music_icon);
LV_IMAGE_DECLARE(image_calculator_icon);
LV_IMAGE_DECLARE(image_steps_icon);

static void launch_signalk(lv_obj_t *tile)      { signalk_dashboard_create(tile); ui_app_tile_mark_signalk(); }
static void launch_world_clock(lv_obj_t *tile)  { world_clock_create(tile); }
static void launch_stopwatch(lv_obj_t *tile)    { stopwatch_create(tile); }
static void launch_alarm(lv_obj_t *tile)        { alarm_clock_create(tile); }
static void launch_calendar(lv_obj_t *tile)     { calendar_app_create(tile); }
static void launch_music(lv_obj_t *tile)        { music_app_create(tile); }
static void launch_calculator(lv_obj_t *tile)   { calculator_app_create(tile); }
static void launch_steps(lv_obj_t *tile)        { step_app_create(tile); }

static const app_entry_t s_apps[] = {
    { "SignalK",     &image_signalk_icon,    launch_signalk     },
    { "World Clock", &image_worldclock_icon, launch_world_clock },
    { "Stopwatch",   &image_stopwatch_icon,  launch_stopwatch   },
    { "Alarm",       &image_alarmclock_icon, launch_alarm       },
    { "Calendar",    &image_calendar_icon,   launch_calendar    },
    { "Music",       &image_music_icon,      launch_music       },
    { "Calculator",  &image_calculator_icon, launch_calculator  },
    { "Steps",       &image_steps_icon,      launch_steps       },
};
static const int s_app_count = sizeof(s_apps) / sizeof(s_apps[0]);

// ---------------------------------------------------------------------------
// Click handler
// ---------------------------------------------------------------------------

static void app_click_cb(lv_event_t *e)
{
    const app_entry_t *app = lv_event_get_user_data(e);
    if (!app || !app->launch) return;

    lv_indev_wait_release(lv_indev_active());

    lv_obj_t *t = ui_app_tile_acquire();
    if (!t) {
        ESP_LOGE(TAG, "failed to acquire app tile");
        return;
    }
    app->launch(t);
    ui_app_tile_show();
}

// Programmatically open the Alarm app (no touch input to debounce). Mirrors
// app_click_cb. Called on the LVGL thread by the display-wake hook when an
// alarm is ringing.
void ui_open_alarm_app(void)
{
    lv_obj_t *t = ui_app_tile_acquire();
    if (!t) {
        ESP_LOGE(TAG, "failed to acquire app tile for alarm");
        return;
    }
    alarm_clock_create(t);
    ui_app_tile_show();
}

// Open the Music app straight to Now Playing. Called on the LVGL thread by the
// display-wake hook when a track is actively playing, for one-tap pause/stop.
void ui_open_music_app(void)
{
    lv_obj_t *t = ui_app_tile_acquire();
    if (!t) {
        ESP_LOGE(TAG, "failed to acquire app tile for music");
        return;
    }
    music_app_create(t);
    music_app_goto_now_playing();
    ui_app_tile_show();
}

// ---------------------------------------------------------------------------
// Screen construction
// ---------------------------------------------------------------------------

static void apply_bg_src(lv_obj_t *img)
{
    LV_IMAGE_DECLARE(background_wf);
    LV_IMAGE_DECLARE(background_wf_2);
    LV_IMAGE_DECLARE(background_wf_3);
    int bg = settings_get_watchface_bg();
    if (bg == 1)      lv_image_set_src(img, &background_wf);
    else if (bg == 2) lv_image_set_src(img, &background_wf_3);
    else              lv_image_set_src(img, &background_wf_2);
}

static void add_watchface_bg(lv_obj_t *parent)
{
    s_bg_img = lv_image_create(parent);
    apply_bg_src(s_bg_img);
    lv_obj_set_align(s_bg_img, LV_ALIGN_CENTER);
}

void app_picker_refresh_bg(void)
{
    if (s_bg_img) apply_bg_src(s_bg_img);
}

void app_picker_create(lv_obj_t *parent)
{
    // Background image drawn first so it appears behind the content overlay
    add_watchface_bg(parent);

    static lv_style_t s_bg_style;
    lv_style_init(&s_bg_style);
    lv_style_set_bg_opa(&s_bg_style, LV_OPA_TRANSP);
    lv_style_set_text_color(&s_bg_style, lv_color_white());

    lv_obj_t *screen = lv_obj_create(parent);
    lv_obj_remove_style_all(screen);
    lv_obj_add_style(screen, &s_bg_style, 0);
    lv_obj_set_size(screen, lv_pct(100), lv_pct(100));
    lv_obj_center(screen);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(screen, 0, 0);

    // Title bar
    lv_obj_t *title = lv_label_create(screen);
    lv_obj_set_style_text_font(title, &font_bold_32, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_pad_top(title, 16, 0);
    lv_obj_set_style_pad_bottom(title, 14, 0);
    lv_label_set_text(title, "Apps");
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    // Icon grid — scrollable so the list can grow past two rows without
    // clipping (five apps wrap to a 2-2-1 layout taller than the tile).
    lv_obj_t *grid = lv_obj_create(screen);
    lv_obj_remove_style_all(grid);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_left(grid, 12, 0);
    lv_obj_set_style_pad_right(grid, 12, 0);
    lv_obj_set_style_pad_row(grid, 12, 0);
    lv_obj_set_style_pad_column(grid, 12, 0);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

    for (int i = 0; i < s_app_count; i++) {
        const app_entry_t *app = &s_apps[i];

        lv_obj_t *item = lv_obj_create(grid);
        lv_obj_remove_style_all(item);
        lv_obj_set_width(item, lv_pct(46));
        lv_obj_set_height(item, 150);
        lv_obj_set_style_bg_color(item, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_bg_opa(item, 25, 0);
        lv_obj_set_style_radius(item, 20, 0);
        lv_obj_set_style_pad_all(item, 10, 0);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(item, app_click_cb, LV_EVENT_CLICKED, (void *)app);

        if (app->icon) {
            lv_obj_t *img = lv_image_create(item);
            lv_image_set_src(img, app->icon);
            lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
        }

        lv_obj_t *lbl = lv_label_create(item);
        lv_obj_set_style_text_font(lbl, &font_normal_26, 0);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_pad_top(lbl, 6, 0);
        lv_label_set_text(lbl, app->name);
        lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
    }
}
