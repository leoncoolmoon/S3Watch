// SignalK dashboard tile. Lives above the watchface at (0,0); user reaches
// it by swiping up.
//
// Pure read: a 1 Hz task_coordinator subscriber pulls latest values from
// signalk_client_get() and renders them with American nautical units.
// Stale values (>5 s without a delta) render as "—". The status line at
// the bottom mirrors signalk_client_state().

#include "signalk_dashboard.h"
#include "signalk_dashboard_fmt.h"
#include "ui_fonts.h"
#include "signalk_client.h"
#include "task_coordinator.h"
#include "bsp/esp-bsp.h"
#include "esp_timer.h"
#include "lvgl.h"
#include <stdio.h>

#define STALE_AFTER_MS  5000

typedef struct {
    lv_obj_t *hdg;     // heading value
    lv_obj_t *depth;
    lv_obj_t *sog;
    lv_obj_t *wind;    // combined "angle / speed"
    lv_obj_t *status;
} dash_t;

static dash_t s_d;
static bool   s_subscribed = false;

static bool value_fresh(const signalk_value_t *v) {
    if (!v->valid) return false;
    int64_t now_ms = esp_timer_get_time() / 1000;
    return (now_ms - v->updated_ms) <= STALE_AFTER_MS;
}

static const char *state_text(signalk_state_t st) {
    switch (st) {
    case SIGNALK_STATE_IDLE:       return "Idle";
    case SIGNALK_STATE_NO_CONFIG:  return "Not configured";
    case SIGNALK_STATE_WIFI_UP:    return "Connecting Wi-Fi…";
    case SIGNALK_STATE_CONNECTING: return "Connecting…";
    case SIGNALK_STATE_CONNECTED:  return "Connected";
    case SIGNALK_STATE_ERROR:      return "Error";
    }
    return "?";
}

// Fired by LVGL (inside the LVGL lock) when the tile that owns the dashboard
// is being deleted. Zeroing s_d here is race-free: tick_cb also holds the
// LVGL lock before touching s_d, so the two operations are serialised.
static void dash_on_delete(lv_event_t *e)
{
    (void)e;
    s_d.hdg = s_d.depth = s_d.sog = s_d.wind = s_d.status = NULL;
}

// 1 Hz refresh, only fires while display is on (task_coord off-period = 0).
// Runs on the task_coord task — must hold the LVGL lock before touching any
// widget; otherwise we race the LVGL flush task and starve the IDLE watchdog.
static void tick_cb(void *user) {
    (void)user;
    signalk_value_t v, ang, spd;
    char hdg[16], depth[16], sog[16], wind[32];

    signalk_client_get(SIGNALK_PATH_HEADING_MAG, &v);
    fmt_heading_to(hdg, sizeof(hdg), &v, value_fresh(&v));
    signalk_client_get(SIGNALK_PATH_DEPTH_BELOW_TRANSDUCER, &v);
    fmt_depth_ft_to(depth, sizeof(depth), &v, value_fresh(&v));
    signalk_client_get(SIGNALK_PATH_SOG, &v);
    fmt_sog_kt_to(sog, sizeof(sog), &v, value_fresh(&v));
    signalk_client_get(SIGNALK_PATH_WIND_ANGLE_APPARENT, &ang);
    signalk_client_get(SIGNALK_PATH_WIND_SPEED_APPARENT, &spd);
    fmt_wind_to(wind, sizeof(wind), &ang, value_fresh(&ang), &spd, value_fresh(&spd));
    const char *status = state_text(signalk_client_state());

    if (!bsp_display_lock(50)) return;  // skip this tick on contention
    // s_d is zeroed by dash_on_delete (under the same lock) when the tile is
    // destroyed — check here to avoid touching freed widget memory.
    if (!s_d.hdg) { bsp_display_unlock(); return; }
    lv_label_set_text(s_d.hdg,    hdg);
    lv_label_set_text(s_d.depth,  depth);
    lv_label_set_text(s_d.sog,    sog);
    lv_label_set_text(s_d.wind,   wind);
    lv_label_set_text(s_d.status, status);
    bsp_display_unlock();
}

// One value cell: small grey caption above, big white number below.
static void make_cell(lv_obj_t *parent, const char *caption,
                      lv_obj_t **value_out, int col, int row) {
    lv_obj_t *cell = lv_obj_create(parent);
    lv_obj_remove_style_all(cell);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_STRETCH, col, 1,
                         LV_GRID_ALIGN_STRETCH, row, 1);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *cap = lv_label_create(cell);
    lv_obj_set_style_text_font(cap, &font_normal_26, 0);
    lv_obj_set_style_text_color(cap, lv_color_hex(0x9099A8), 0);
    lv_label_set_text(cap, caption);

    lv_obj_t *val = lv_label_create(cell);
    lv_obj_set_style_text_font(val, &font_bold_32, 0);
    lv_obj_set_style_text_color(val, lv_color_white(), 0);
    lv_label_set_text(val, "—");
    *value_out = val;
}

void signalk_dashboard_create(lv_obj_t *parent) {
    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    lv_obj_t *grid = lv_obj_create(parent);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, lv_pct(100), lv_pct(85));
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 0);
    static const int32_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);

    make_cell(grid, "HDG",   &s_d.hdg,   0, 0);
    make_cell(grid, "DEPTH", &s_d.depth, 1, 0);
    make_cell(grid, "SOG",   &s_d.sog,   0, 1);
    make_cell(grid, "WIND",  &s_d.wind,  1, 1);

    s_d.status = lv_label_create(parent);
    lv_obj_set_style_text_font(s_d.status, &font_normal_26, 0);
    lv_obj_set_style_text_color(s_d.status, lv_color_hex(0x9099A8), 0);
    lv_obj_align(s_d.status, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_label_set_text(s_d.status, "—");

    // Register delete handler so we can null s_d before the widgets are freed.
    lv_obj_add_event_cb(parent, dash_on_delete, LV_EVENT_DELETE, NULL);

    // Subscribe only once — task_coord has no unsubscribe, so calling this on
    // every re-open would stack up duplicate callbacks.
    if (!s_subscribed) {
        task_coord_subscribe("signalk_dash", tick_cb, NULL,
                             /*on*/  1000,   // 1 Hz while display on
                             /*off*/    0);  // skipped while off
        s_subscribed = true;
    }
}
