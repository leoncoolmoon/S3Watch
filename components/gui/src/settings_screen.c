#include "settings_screen.h"
#include "settings.h"
#include "ui_fonts.h"
#include "batt_screen.h"
#include "brightness_screen.h"
#include "setting_flashlight_screen.h"
#include "esp_log.h"
#include "settings_menu_screen.h"
#include "rtc_lib.h"
#include "wifi_manager.h"
#include "music_player.h"

#include "ui.h"
#include "watchface.h"
#include "bsp/esp32_s3_touch_amoled_2_06.h"

static const char* TAG = "SETTINGS SCREEN";

LV_IMAGE_DECLARE(image_mute_icon);
LV_IMAGE_DECLARE(image_flashlight_icon);
LV_IMAGE_DECLARE(image_brightness_icon);
LV_IMAGE_DECLARE(image_battery_icon);
LV_IMAGE_DECLARE(image_silence_icon);
LV_IMAGE_DECLARE(image_settings_icon);

static void click_event_cb(lv_event_t* e);
static void toggle_event_cb(lv_event_t* e);
static void time_timer_cb(lv_timer_t* timer);
static void update_time_label(void);
static void update_now_playing_label(void);
static void control_screen_on_delete(lv_event_t* e);

static lv_obj_t* control_screen;
static lv_obj_t* time_label;
static lv_obj_t* now_playing_label;
static lv_timer_t* time_timer;

// NULL icon = use LVGL symbol text instead of an image
static const lv_image_dsc_t* control_icons[] = {
    &image_brightness_icon,   // CTRL_BRIGHTNESS
    &image_silence_icon,      // CTRL_SILENCE
    &image_flashlight_icon,   // CTRL_FLASHLIGHT
    &image_battery_icon,      // CTRL_BATTERY
    NULL,                     // CTRL_WIFI (uses LV_SYMBOL_WIFI)
    &image_settings_icon,     // CTRL_SETTINGS
};

static const char* control_labels[] = {
    "Brightness",
    "Silence",
    "Flashlight",
    "Battery",
    "Wi-Fi",
    "Settings"
};

enum control_id {
    CTRL_BRIGHTNESS = 0,
    CTRL_SILENCE,
    CTRL_FLASHLIGHT,
    CTRL_BATTERY,
    CTRL_WIFI,
    CTRL_SETTINGS,
    CTRL_COUNT
};

static void update_time_label(void)
{
    if (!time_label) return;
    int hour = rtc_get_hour();
    int min  = rtc_get_minute();
    if (settings_get_time_24h()) {
        lv_label_set_text_fmt(time_label, "%02d:%02d", hour, min);
    } else {
        int h12 = hour % 12;
        if (h12 == 0) h12 = 12;
        lv_label_set_text_fmt(time_label, "%02d:%02d %s", h12, min, hour >= 12 ? "PM" : "AM");
    }
}

// Always-reachable "now playing" feedback for the music app's confirmed
// background-playback design — the engine runs on its own task regardless of
// which screen is showing, so this is the one place every user passes through
// (the controls tile, reached straight off the watchface) that can surface it
// without requiring the Music app to be open. Hidden whenever nothing has
// ever been loaded; piggybacks on the existing 1 s clock timer/lock below
// rather than adding a second timer for what is purely cosmetic, slow-changing
// state.
static void update_now_playing_label(void)
{
    if (!now_playing_label) return;
    music_now_playing_t np;
    music_player_get_now_playing(&np);
    if (!np.active) {
        lv_obj_add_flag(now_playing_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(now_playing_label, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_fmt(now_playing_label, "%s %s \xE2\x80\x94 %s",
                          np.playing ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE,
                          np.title, np.artist);
}

static void time_timer_cb(lv_timer_t* timer)
{
    (void)timer;
    bool locked = bsp_display_lock(0);
    update_time_label();
    update_now_playing_label();
    if (locked) bsp_display_unlock();
}

static void control_screen_on_delete(lv_event_t* e)
{
    (void)e;
    if (time_timer) { lv_timer_del(time_timer); time_timer = NULL; }
    time_label = NULL;
    now_playing_label = NULL;
    control_screen = NULL;
}

void control_screen_create(lv_obj_t* parent)
{
    static lv_style_t cmain_style;
    lv_style_init(&cmain_style);
    lv_style_set_text_color(&cmain_style, lv_color_white());
    lv_style_set_bg_color(&cmain_style, lv_color_hex(0x000000));
    lv_style_set_bg_opa(&cmain_style, LV_OPA_100);

    control_screen = lv_obj_create(parent);
    lv_obj_remove_style_all(control_screen);
    lv_obj_add_style(control_screen, &cmain_style, 0);
    lv_obj_set_size(control_screen, lv_pct(100), lv_pct(100));
    lv_obj_center(control_screen);
    lv_obj_clear_flag(control_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(control_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(control_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(control_screen, 10, 0);
    lv_obj_add_event_cb(control_screen, control_screen_on_delete, LV_EVENT_DELETE, NULL);

    // Header: clock
    lv_obj_t* header = lv_obj_create(control_screen);
    lv_obj_remove_style_all(header);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_top(header, 10, 0);
    lv_obj_set_style_pad_bottom(header, 4, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    time_label = lv_label_create(header);
    lv_obj_set_style_text_font(time_label, &font_bold_32, 0);
    lv_obj_set_style_text_color(time_label, lv_color_white(), 0);
    lv_label_set_text(time_label, "--:--");

    // "Now playing" mini-indicator — see update_now_playing_label(). Hidden
    // until a track has ever loaded.
    now_playing_label = lv_label_create(control_screen);
    lv_obj_set_style_text_font(now_playing_label, &font_normal_26, 0);
    lv_obj_set_style_text_color(now_playing_label, lv_color_hex(0xAAAAAA), 0);
    lv_label_set_long_mode(now_playing_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(now_playing_label, lv_pct(86));
    lv_obj_set_style_text_align(now_playing_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_bottom(now_playing_label, 4, 0);
    lv_obj_remove_flag(now_playing_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(now_playing_label, LV_OBJ_FLAG_HIDDEN);

    // Button grid: 2 columns, up to 4 rows (8 buttons)
    lv_obj_t* grid = lv_obj_create(control_screen);
    lv_obj_remove_style_all(grid);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(grid, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(grid, 4, 0);
    lv_obj_set_style_pad_left(grid, 12, 0);
    lv_obj_set_style_pad_right(grid, 12, 0);
    lv_obj_set_style_pad_row(grid, 12, 0);
    lv_obj_set_style_pad_column(grid, 12, 0);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (int i = 0; i < CTRL_COUNT; i++) {
        lv_obj_t* item = lv_obj_create(grid);
        lv_obj_remove_style_all(item);
        lv_obj_set_width(item, lv_pct(46));
        lv_obj_set_height(item, 90);
        lv_obj_set_style_bg_color(item, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_bg_opa(item, 38, 0);
        lv_obj_set_style_radius(item, 16, 0);
        lv_obj_set_style_pad_all(item, 8, 0);
        lv_obj_set_style_text_align(item, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        bool is_toggle = (i == CTRL_SILENCE || i == CTRL_WIFI);
        if (is_toggle) {
            lv_obj_add_flag(item, LV_OBJ_FLAG_CHECKABLE);
            lv_obj_set_style_bg_color(item, lv_color_hex(0x438bff), LV_PART_MAIN | LV_STATE_CHECKED);
            lv_obj_set_style_bg_opa(item, 255, LV_PART_MAIN | LV_STATE_CHECKED);
            lv_obj_add_event_cb(item, toggle_event_cb, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)i);

            if (i == CTRL_SILENCE && !settings_get_sound())
                lv_obj_add_state(item, LV_STATE_CHECKED);
            if (i == CTRL_WIFI && settings_get_wifi_enabled())
                lv_obj_add_state(item, LV_STATE_CHECKED);
        } else {
            lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(item, click_event_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
        }

        // Icon: image or LVGL symbol
        if (control_icons[i]) {
            lv_obj_t* img = lv_image_create(item);
            lv_image_set_src(img, control_icons[i]);
            lv_obj_set_align(img, LV_ALIGN_TOP_MID);
            lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
        } else {
            lv_obj_t* sym = lv_label_create(item);
            lv_label_set_text(sym, LV_SYMBOL_WIFI);
            lv_obj_set_style_text_font(sym, &font_bold_42, 0);
            lv_obj_set_style_text_color(sym, lv_color_hex(0xD0D0D0), 0);
            lv_obj_set_align(sym, LV_ALIGN_TOP_MID);
            lv_obj_remove_flag(sym, LV_OBJ_FLAG_CLICKABLE);
        }

        lv_obj_t* lbl = lv_label_create(item);
        lv_label_set_text(lbl, control_labels[i]);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xD0D0D0), 0);
        lv_obj_set_style_text_font(lbl, &font_normal_26, 0);
        lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
    }

    update_time_label();
    update_now_playing_label();
    if (!time_timer) {
        time_timer = lv_timer_create(time_timer_cb, 1000, NULL);
        lv_timer_ready(time_timer);
    }
}


static void click_event_cb(lv_event_t* e)
{
    int ctrl = (int)(uintptr_t)lv_event_get_user_data(e);
    lv_obj_t* t;
    switch (ctrl) {
    case CTRL_BRIGHTNESS:
        t = ui_dynamic_tile_acquire();
        if (t) { lv_smartwatch_brightness_create(t); ui_dynamic_tile_show(); }
        break;
    case CTRL_BATTERY:
        t = ui_dynamic_tile_acquire();
        if (t) { lv_smartwatch_batt_create(t); ui_dynamic_tile_show(); }
        break;
    case CTRL_FLASHLIGHT:
        t = ui_dynamic_tile_acquire();
        if (t) { setting_flashlight_screen_create(t); ui_dynamic_tile_show(); }
        break;
    case CTRL_SETTINGS:
        t = ui_dynamic_tile_acquire();
        if (t) { settings_menu_screen_create(t); ui_dynamic_tile_show(); }
        break;
    default:
        break;
    }
}

static void toggle_event_cb(lv_event_t* e)
{
    lv_obj_t* obj = lv_event_get_target(e);
    int ctrl = (int)(uintptr_t)lv_event_get_user_data(e);
    bool checked = lv_obj_has_state(obj, LV_STATE_CHECKED);

    switch (ctrl) {
    case CTRL_SILENCE:
        settings_set_sound(!checked);
        break;
    case CTRL_WIFI:
        settings_set_wifi_enabled(checked);
        if (checked) {
            ESP_LOGI(TAG, "WiFi permitted — connecting for NTP sync");
            wifi_manager_wake();
            wifi_manager_auto_connect();
            // WiFi releases itself in on_sntp_sync() after sync completes
        } else {
            ESP_LOGI(TAG, "WiFi permission revoked — releasing radio");
            wifi_manager_release();
        }
        break;
    default:
        break;
    }
}
