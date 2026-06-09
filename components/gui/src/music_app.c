// Music app — Artist -> Album -> Track drill-down + Now Playing screen,
// launched from the app picker into the single app tile (ui_app_tile_*),
// exactly like every other launch_* in app_picker.c.
//
// ui_tileview_back() always closes the app tile the moment it's open — there
// is no hook for an app to intercept back-press and pop one navigation level
// instead (see ui_tileview.c). So this whole 4-level browser lives inside one
// screen with its own internal navigation stack (s_level/s_artist_index/
// s_album_index, static so it survives close/reopen — same "persists across
// launches" approach as stopwatch.c's timer state) and its own in-UI "<" back
// affordance that pops one level. The hardware back button still closes the
// app outright from any depth — same as every other single-tile app — and
// that's fine: playback keeps running in music_player's own task regardless
// of whether this screen exists.

#include "music_app.h"
#include "music_player.h"
#include "ui_fonts.h"
#include "esp_log.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

LV_IMAGE_DECLARE(image_ctrl_play);
LV_IMAGE_DECLARE(image_ctrl_pause);
LV_IMAGE_DECLARE(image_ctrl_prev);
LV_IMAGE_DECLARE(image_ctrl_next);
LV_IMAGE_DECLARE(image_ctrl_shuffle);

static const char *TAG = "MUSIC_APP";

typedef enum {
    LVL_ARTISTS,
    LVL_ALBUMS,
    LVL_TRACKS,
    LVL_NOW_PLAYING,
} music_level_t;

// Navigation state — static so returning to the app lands back where the
// user left it (e.g. still on the Now Playing screen of a track that kept
// playing in the background).
static music_level_t s_level        = LVL_ARTISTS;
static uint32_t      s_artist_index = 0;
static uint32_t      s_album_index  = 0;

// Live UI pointers — only valid while the screen is mounted.
static lv_obj_t *s_screen   = NULL;
static lv_obj_t *s_title    = NULL;
static lv_obj_t *s_back_btn = NULL;
static lv_obj_t *s_body     = NULL;

// Now-playing widgets + its refresh timer — only valid while LVL_NOW_PLAYING
// is the displayed level.
static lv_obj_t   *s_np_title  = NULL;
static lv_obj_t   *s_np_artist = NULL;
static lv_obj_t   *s_np_album  = NULL;
static lv_obj_t   *s_np_bar    = NULL;
static lv_obj_t   *s_np_pos    = NULL;
static lv_obj_t   *s_np_dur    = NULL;
static lv_obj_t   *s_np_play   = NULL;   // play/pause glyph label — text swapped on refresh
static lv_timer_t *s_np_timer  = NULL;

static bool s_load_in_flight = false;

static void show_level(music_level_t level);
static void show_no_library(void);

// ---------------------------------------------------------------------------
// Catalog loading — INDEX.JSN parsing is file I/O + JSON parsing, so it runs
// on a one-shot task (mirrors wifi_screens.c's wake_and_scan_task) and reports
// back to the LVGL task via lv_async_call (mirrors do_scan_update). The
// catalog itself caches its result, so this only ever needs to run once per
// boot session unless the first attempt failed (no card yet, etc).
// ---------------------------------------------------------------------------

static void load_complete_async(void *arg)
{
    (void)arg;
    s_load_in_flight = false;
    if (!s_screen || !lv_obj_is_valid(s_screen)) return;
    if (music_player_catalog_ready()) {
        show_level(LVL_ARTISTS);
    } else {
        lv_obj_clean(s_body);
        show_no_library();
    }
}

static void load_catalog_task(void *arg)
{
    (void)arg;
    music_player_load_catalog();
    lv_async_call(load_complete_async, NULL);
    vTaskDelete(NULL);
}

static void start_catalog_load(void)
{
    if (s_load_in_flight) return;
    s_load_in_flight = true;
    xTaskCreate(load_catalog_task, "music_load", 4096, NULL, 5, NULL);
}

// ---------------------------------------------------------------------------
// Shared row/list helpers (browse levels)
// ---------------------------------------------------------------------------

static lv_obj_t *make_list_container(void)
{
    lv_obj_t *list = lv_obj_create(s_body);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_style_pad_left(list, 10, 0);
    lv_obj_set_style_pad_right(list, 10, 0);
    lv_obj_set_style_pad_top(list, 8, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    return list;
}

static void add_centered_message(lv_obj_t *parent, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x808080), 0);
    lv_obj_set_style_text_font(lbl, &font_normal_26, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(lbl);
}

static lv_obj_t *make_row(lv_obj_t *parent, const char *text, lv_event_cb_t cb, uint32_t index)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 56);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x1C1C1C), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, (void *)(uintptr_t)index);

    // Truncate long metadata tags so they fit on one line inside the 56 px row.
    // Now Playing uses its own wider labels and is unaffected.
    enum { MAX_ROW_CHARS = 24 };
    char short_buf[MAX_ROW_CHARS + 4];
    if (strlen(text) > MAX_ROW_CHARS) {
        snprintf(short_buf, sizeof(short_buf), "%.*s...", MAX_ROW_CHARS, text);
        text = short_buf;
    }

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &font_normal_28, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl, lv_pct(92));
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 14, 0);
    lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
    return row;
}

// ---------------------------------------------------------------------------
// Browse levels — Artist -> Album -> Track
// ---------------------------------------------------------------------------

static void artist_row_cb(lv_event_t *e)
{
    s_artist_index = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    s_album_index  = 0;
    show_level(LVL_ALBUMS);
}

static void album_row_cb(lv_event_t *e)
{
    s_album_index = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    show_level(LVL_TRACKS);
}

static void track_row_cb(lv_event_t *e)
{
    uint32_t track_index = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    uint32_t flat = music_catalog_track_flat_index(s_artist_index, s_album_index, track_index);
    if (flat >= music_catalog_total_track_count()) return;
    music_player_play(flat);
    show_level(LVL_NOW_PLAYING);
}

static void build_artist_list(void)
{
    lv_obj_t *list = make_list_container();
    uint32_t n = music_catalog_artist_count();
    if (n == 0) { add_centered_message(list, "No artists found"); return; }
    for (uint32_t i = 0; i < n; i++) {
        make_row(list, music_catalog_artist_name(i), artist_row_cb, i);
    }
}

static void build_album_list(void)
{
    lv_obj_t *list = make_list_container();
    uint32_t n = music_catalog_album_count(s_artist_index);
    if (n == 0) { add_centered_message(list, "No albums found"); return; }
    for (uint32_t i = 0; i < n; i++) {
        make_row(list, music_catalog_album_name(s_artist_index, i), album_row_cb, i);
    }
}

static void build_track_list(void)
{
    lv_obj_t *list = make_list_container();
    uint32_t n = music_catalog_track_count(s_artist_index, s_album_index);
    if (n == 0) { add_centered_message(list, "No tracks found"); return; }
    for (uint32_t i = 0; i < n; i++) {
        make_row(list, music_catalog_track_title(s_artist_index, s_album_index, i), track_row_cb, i);
    }
}

// ---------------------------------------------------------------------------
// Now Playing screen
// ---------------------------------------------------------------------------

static void format_mmss(uint32_t total_s, char *buf, size_t buf_sz)
{
    snprintf(buf, buf_sz, "%u:%02u", (unsigned)(total_s / 60), (unsigned)(total_s % 60));
}

static void refresh_now_playing(void)
{
    if (!s_np_title) return;

    music_now_playing_t np;
    music_player_get_now_playing(&np);

    lv_label_set_text(s_np_title,  np.active ? np.title  : "\xE2\x80\x94"); // em dash placeholder
    lv_label_set_text(s_np_artist, np.active ? np.artist : "");
    lv_label_set_text(s_np_album,  np.active ? np.album  : "");

    int pct = 0;
    if (np.active && np.duration_s > 0) {
        pct = (int)((uint64_t)np.position_s * 100 / np.duration_s);
        if (pct > 100) pct = 100;
    }
    lv_bar_set_value(s_np_bar, pct, LV_ANIM_OFF);

    char buf[8];
    format_mmss(np.position_s, buf, sizeof(buf));
    lv_label_set_text(s_np_pos, buf);
    format_mmss(np.duration_s, buf, sizeof(buf));
    lv_label_set_text(s_np_dur, buf);

    lv_image_set_src(s_np_play, np.playing ? &image_ctrl_pause : &image_ctrl_play);
}

static void np_timer_cb(lv_timer_t *t) { (void)t; refresh_now_playing(); }

static void np_play_pause_cb(lv_event_t *e) { (void)e; music_player_toggle_play_pause(); }
static void np_next_cb(lv_event_t *e)       { (void)e; music_player_next(); }
static void np_prev_cb(lv_event_t *e)       { (void)e; music_player_prev(); }

static void np_shuffle_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    music_player_set_shuffle(lv_obj_has_state(obj, LV_STATE_CHECKED));
}

// Round icon button — used for prev/play-pause/next (plain click) and shuffle
// (checkable toggle, wired separately by the caller).
static lv_obj_t *make_round_btn(lv_obj_t *parent, const lv_image_dsc_t *img,
                                  lv_event_cb_t cb, lv_event_code_t code)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 64, 64);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(btn, 30, 0);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    if (cb) lv_obj_add_event_cb(btn, cb, code, NULL);

    lv_obj_t *icon = lv_image_create(btn);
    lv_image_set_src(icon, img);
    lv_obj_center(icon);
    lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    return btn;
}

static void build_now_playing(void)
{
    lv_obj_t *col = lv_obj_create(s_body);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 6, 0);

    s_np_title = lv_label_create(col);
    lv_obj_set_style_text_font(s_np_title, &font_bold_28, 0);
    lv_obj_set_style_text_color(s_np_title, lv_color_white(), 0);
    lv_label_set_long_mode(s_np_title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_np_title, lv_pct(90));
    lv_obj_set_style_text_align(s_np_title, LV_TEXT_ALIGN_CENTER, 0);

    s_np_artist = lv_label_create(col);
    lv_obj_set_style_text_font(s_np_artist, &font_normal_26, 0);
    lv_obj_set_style_text_color(s_np_artist, lv_color_hex(0xAAAAAA), 0);
    lv_label_set_long_mode(s_np_artist, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_np_artist, lv_pct(90));
    lv_obj_set_style_text_align(s_np_artist, LV_TEXT_ALIGN_CENTER, 0);

    s_np_album = lv_label_create(col);
    lv_obj_set_style_text_font(s_np_album, &font_normal_26, 0);
    lv_obj_set_style_text_color(s_np_album, lv_color_hex(0x808080), 0);
    lv_label_set_long_mode(s_np_album, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_np_album, lv_pct(90));
    lv_obj_set_style_text_align(s_np_album, LV_TEXT_ALIGN_CENTER, 0);

    // Progress bar (read-only — no seek-by-drag, per scope) + position/duration,
    // styled like batt_screen.c's battery bar (lv_bar_create + range 0..100).
    lv_obj_t *bar_box = lv_obj_create(col);
    lv_obj_remove_style_all(bar_box);
    lv_obj_set_width(bar_box, lv_pct(86));
    lv_obj_set_height(bar_box, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(bar_box, 10, 0);
    lv_obj_set_style_pad_row(bar_box, 4, 0);
    lv_obj_clear_flag(bar_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bar_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bar_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_np_bar = lv_bar_create(bar_box);
    lv_obj_set_width(s_np_bar, lv_pct(100));
    lv_obj_set_height(s_np_bar, 14);
    lv_bar_set_range(s_np_bar, 0, 100);
    lv_bar_set_value(s_np_bar, 0, LV_ANIM_OFF);

    lv_obj_t *time_row = lv_obj_create(bar_box);
    lv_obj_remove_style_all(time_row);
    lv_obj_set_width(time_row, lv_pct(100));
    lv_obj_set_height(time_row, LV_SIZE_CONTENT);
    lv_obj_clear_flag(time_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(time_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_np_pos = lv_label_create(time_row);
    lv_obj_set_style_text_font(s_np_pos, &font_normal_26, 0);
    lv_obj_set_style_text_color(s_np_pos, lv_color_hex(0x808080), 0);
    lv_label_set_text(s_np_pos, "0:00");

    s_np_dur = lv_label_create(time_row);
    lv_obj_set_style_text_font(s_np_dur, &font_normal_26, 0);
    lv_obj_set_style_text_color(s_np_dur, lv_color_hex(0x808080), 0);
    lv_label_set_text(s_np_dur, "0:00");

    // Transport row: shuffle | prev | play/pause | next
    lv_obj_t *btn_row = lv_obj_create(col);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_width(btn_row, lv_pct(100));
    lv_obj_set_height(btn_row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(btn_row, 18, 0);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Shuffle is a checkable toggle (LV_EVENT_VALUE_CHANGED), exactly like
    // the Silence/Wi-Fi toggle tiles in settings_screen.c's control grid —
    // its checked-state background color is the on/off indicator.
    lv_obj_t *shuf_btn = make_round_btn(btn_row, &image_ctrl_shuffle, np_shuffle_cb, LV_EVENT_VALUE_CHANGED);
    lv_obj_add_flag(shuf_btn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_bg_color(shuf_btn, lv_color_hex(0x4090FF), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(shuf_btn, 220, LV_PART_MAIN | LV_STATE_CHECKED);
    if (music_player_get_shuffle()) lv_obj_add_state(shuf_btn, LV_STATE_CHECKED);

    make_round_btn(btn_row, &image_ctrl_prev, np_prev_cb, LV_EVENT_CLICKED);

    lv_obj_t *play_btn = make_round_btn(btn_row, &image_ctrl_play, np_play_pause_cb, LV_EVENT_CLICKED);
    s_np_play = lv_obj_get_child(play_btn, 0);

    make_round_btn(btn_row, &image_ctrl_next, np_next_cb, LV_EVENT_CLICKED);

    refresh_now_playing();
    if (s_np_timer) lv_timer_del(s_np_timer);
    s_np_timer = lv_timer_create(np_timer_cb, 500, NULL);
}

// ---------------------------------------------------------------------------
// Loading / no-library states
// ---------------------------------------------------------------------------

static void show_loading(void)
{
    lv_obj_clean(s_body); // safe no-op the first time (body is freshly created); needed on retry
    lv_obj_t *list = make_list_container();
    add_centered_message(list, "Loading library\xE2\x80\xA6"); // "…"
}

static void retry_load_cb(lv_event_t *e)
{
    (void)e;
    show_loading();
    start_catalog_load();
}

static void show_no_library(void)
{
    lv_obj_t *col = lv_obj_create(s_body);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 16, 0);

    lv_obj_t *lbl = lv_label_create(col);
    lv_label_set_text(lbl, "No music library found.\nInsert a card with a\nMUSIC folder + INDEX.JSN.");
    lv_obj_set_style_text_font(lbl, &font_normal_26, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x808080), 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *btn = lv_btn_create(col);
    lv_obj_set_size(btn, 160, 56);
    lv_obj_add_event_cb(btn, retry_load_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(btn_lbl, &font_bold_28, 0);
    lv_label_set_text(btn_lbl, "Retry");
    lv_obj_center(btn_lbl);
}

// ---------------------------------------------------------------------------
// Level switch — header + lv_obj_clean(body) + rebuild, mirrors how
// wifi_screens/settings_menu rebuild on navigation.
// ---------------------------------------------------------------------------

static const char *level_title(void)
{
    switch (s_level) {
    case LVL_ARTISTS:     return "Music";
    case LVL_ALBUMS:      return music_catalog_artist_name(s_artist_index);
    case LVL_TRACKS:      return music_catalog_album_name(s_artist_index, s_album_index);
    case LVL_NOW_PLAYING: return "Now Playing";
    }
    return "Music";
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    switch (s_level) {
    case LVL_ALBUMS:      show_level(LVL_ARTISTS); break;
    case LVL_TRACKS:      show_level(LVL_ALBUMS);  break;
    case LVL_NOW_PLAYING: show_level(LVL_TRACKS);  break;
    case LVL_ARTISTS:     break; // top level — back affordance is hidden here
    }
}

static void show_level(music_level_t level)
{
    if (!s_screen || !lv_obj_is_valid(s_screen)) return;

    // Tear down the now-playing screen's timer/widget pointers when leaving
    // it. Playback itself is untouched — it lives entirely in music_player's
    // own task and keeps running regardless of which level is shown.
    if (s_np_timer) {
        lv_timer_del(s_np_timer);
        s_np_timer = NULL;
        s_np_title = s_np_artist = s_np_album = s_np_bar = s_np_pos = s_np_dur = s_np_play = NULL;
    }

    s_level = level;
    lv_label_set_text(s_title, level_title());
    if (level == LVL_ARTISTS) lv_obj_add_flag(s_back_btn, LV_OBJ_FLAG_HIDDEN);
    else                      lv_obj_clear_flag(s_back_btn, LV_OBJ_FLAG_HIDDEN);

    lv_obj_clean(s_body);
    switch (level) {
    case LVL_ARTISTS:     build_artist_list();  break;
    case LVL_ALBUMS:      build_album_list();   break;
    case LVL_TRACKS:      build_track_list();   break;
    case LVL_NOW_PLAYING: build_now_playing();  break;
    }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

static void music_app_on_delete(lv_event_t *e)
{
    (void)e;
    if (s_np_timer) { lv_timer_del(s_np_timer); s_np_timer = NULL; }
    // Deliberately NOT cancelling load_complete_async here (unlike
    // wifi_screens.c's scan_on_delete, which must cancel do_scan_update to
    // avoid a use-after-free on its heap-allocated, freed-on-delete ctx).
    // load_complete_async only touches static globals that outlive the
    // screen and already self-guards via lv_obj_is_valid(s_screen) — so
    // cancelling would only risk leaving s_load_in_flight stuck `true`
    // (permanently blocking retries) without preventing anything unsafe.
    s_screen = s_title = s_back_btn = s_body = NULL;
    s_np_title = s_np_artist = s_np_album = s_np_bar = s_np_pos = s_np_dur = s_np_play = NULL;
    ESP_LOGI(TAG, "screen deleted (navigation state preserved)");
}

void music_app_create(lv_obj_t *parent)
{
    s_screen = lv_obj_create(parent);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_event_cb(s_screen, music_app_on_delete, LV_EVENT_DELETE, NULL);

    // Header: in-app "<" back (pops one level, hidden at the Artist root) + title
    lv_obj_t *hdr = lv_obj_create(s_screen);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, lv_pct(100), 48);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    // This "<" is the *only* way to pop one browse level — the hardware back
    // gesture closes the whole app outright from any depth (see header
    // comment), so it has to actually read as a tappable button rather than
    // a bare glyph in the corner, or it goes unnoticed (reported: "can't
    // figure out how to back out"). A pill background makes it look pressable;
    // ext_click_area pads its small ~32px glyph out to a real touch target.
    s_back_btn = lv_label_create(hdr);
    lv_label_set_text(s_back_btn, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(s_back_btn, &font_normal_32, 0);
    lv_obj_set_style_text_color(s_back_btn, lv_color_hex(0x4090FF), 0);
    lv_obj_set_style_bg_color(s_back_btn, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(s_back_btn, 30, 0);
    lv_obj_set_style_radius(s_back_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(s_back_btn, 6, 0);
    lv_obj_align(s_back_btn, LV_ALIGN_LEFT_MID, 50, 0);
    lv_obj_add_flag(s_back_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_back_btn, 16);
    lv_obj_add_event_cb(s_back_btn, back_cb, LV_EVENT_CLICKED, NULL);

    s_title = lv_label_create(hdr);
    lv_obj_set_style_text_font(s_title, &font_bold_28, 0);
    lv_obj_set_style_text_color(s_title, lv_color_white(), 0);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_title, lv_pct(70));
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_title, LV_ALIGN_CENTER, 0, 0);

    // Body — cleaned and repopulated by show_level() per navigation level
    s_body = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_body);
    lv_obj_set_width(s_body, lv_pct(100));
    lv_obj_set_flex_grow(s_body, 1);
    lv_obj_clear_flag(s_body, LV_OBJ_FLAG_SCROLLABLE);

    if (music_player_catalog_ready()) {
        show_level(s_level);
    } else {
        // Nothing to resume into yet — show the loading state and kick a
        // load attempt (start_catalog_load() no-ops if one's already running,
        // so reopening mid-load just re-shows "Loading…" without duplicating
        // the background task). load_complete_async() routes to either the
        // Artist list or the no-library/Retry screen once it finishes.
        s_level = LVL_ARTISTS;
        lv_label_set_text(s_title, level_title());
        lv_obj_add_flag(s_back_btn, LV_OBJ_FLAG_HIDDEN);
        show_loading();
        start_catalog_load();
    }
}
