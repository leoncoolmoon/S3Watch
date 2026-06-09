#include "wifi_screens.h"
#include "ui.h"
#include "ip_picker.h"
#include "scroll_keyboard.h"
#include "wifi_manager.h"
#include "ntp_sync.h"
#include "settings.h"
#include "ui_fonts.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_event.h"
#include "bsp/esp32_s3_touch_amoled_2_06.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "WIFI_SCR";

// ── Shared helpers ────────────────────────────────────────────────────────────

static lv_obj_t *make_header(lv_obj_t *parent, const char *title,
                              lv_event_cb_t back_cb, void *user_data)
{
    lv_obj_t *hdr = lv_obj_create(parent);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, lv_pct(100), 48);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = lv_label_create(hdr);
    lv_label_set_text(back, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(back, &font_normal_32, 0);
    lv_obj_set_style_text_color(back, lv_color_hex(0x4090FF), 0);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 50, 0);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *lbl = lv_label_create(hdr);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_font(lbl, &font_bold_28, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    return hdr;
}

static lv_obj_t *make_screen(lv_obj_t *parent)
{
    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    return scr;
}

// ── Password screen ───────────────────────────────────────────────────────────

typedef struct {
    lv_obj_t *screen;
    char      ssid[WIFI_MANAGER_MAX_SSID_LEN];
} pw_ctx_t;

static void pw_done_cb(const char *text, void *user_data)
{
    pw_ctx_t *ctx = (pw_ctx_t*)user_data;
    ESP_LOGI(TAG, "Connecting to '%s'", ctx->ssid);
    // wifi_manager_connect copies the password synchronously, so it's safe to
    // consume `text` (which points into the keyboard's buffer) here.
    wifi_manager_connect(ctx->ssid, text, true);
    // Defer screen teardown — we're inside the keyboard's button-click event,
    // and this screen owns that button. Synchronous delete = use-after-free.
    lv_obj_del_async(ctx->screen);
    free(ctx);
}

static void pw_cancel_cb(void *user_data)
{
    pw_ctx_t *ctx = (pw_ctx_t*)user_data;
    lv_obj_del_async(ctx->screen);
    free(ctx);
}

static void open_password_screen(lv_obj_t *parent, const char *ssid)
{
    pw_ctx_t *ctx = calloc(1, sizeof(pw_ctx_t));
    if (!ctx) return;
    strncpy(ctx->ssid, ssid, WIFI_MANAGER_MAX_SSID_LEN - 1);

    ctx->screen = make_screen(parent);

    // Header with back button that cancels
    lv_obj_t *hdr = lv_obj_create(ctx->screen);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, lv_pct(100), 44);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, ssid);
    lv_obj_set_style_text_font(title, &font_bold_28, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, lv_pct(80));

    // Keyboard fills remainder of screen
    lv_obj_t *kb_area = lv_obj_create(ctx->screen);
    lv_obj_remove_style_all(kb_area);
    lv_obj_set_size(kb_area, lv_pct(100), lv_pct(100) - 44);
    lv_obj_align(kb_area, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(kb_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(kb_area, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(kb_area, LV_OPA_COVER, 0);

    scroll_keyboard_create(kb_area, "", "Password",
                           pw_done_cb, pw_cancel_cb, ctx);
}

// ── WiFi scan screen ──────────────────────────────────────────────────────────

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *list;
    lv_obj_t *status_label;
    esp_event_handler_instance_t scan_handler;
    esp_event_handler_instance_t conn_handler;
} scan_ctx_t;

static void release_task(void *arg)
{
    (void)arg;
    wifi_manager_release();
    vTaskDelete(NULL);
}

static void ap_click_cb(lv_event_t *e)
{
    lv_obj_t    *btn = lv_event_get_target(e);
    scan_ctx_t  *ctx = (scan_ctx_t*)lv_event_get_user_data(e);
    const char  *ssid = lv_label_get_text(lv_obj_get_child(btn, 0));
    open_password_screen(lv_obj_get_parent(ctx->screen), ssid);
}

static const char *rssi_bars(int8_t rssi)
{
    if (rssi >= -55) return "▂▄▆█";
    if (rssi >= -67) return "▂▄▆ ";
    if (rssi >= -78) return "▂▄  ";
    return                  "▂   ";
}

static void populate_list(scan_ctx_t *ctx)
{
    lv_obj_clean(ctx->list);
    wifi_manager_ap_t aps[WIFI_MANAGER_MAX_SCAN_APS];
    int n = wifi_manager_get_scan_results(aps, WIFI_MANAGER_MAX_SCAN_APS);
    if (n == 0) {
        lv_obj_t *lbl = lv_label_create(ctx->list);
        lv_label_set_text(lbl, "No networks found");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x808080), 0);
        lv_obj_set_style_text_font(lbl, &font_normal_26, 0);
        lv_obj_center(lbl);
        return;
    }
    for (int i = 0; i < n; i++) {
        lv_obj_t *row = lv_obj_create(ctx->list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(100), 56);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x1C1C1C), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 10, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, ap_click_cb, LV_EVENT_CLICKED, ctx);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *ssid_lbl = lv_label_create(row);
        lv_label_set_text(ssid_lbl, aps[i].ssid);
        lv_obj_set_style_text_font(ssid_lbl, &font_normal_28, 0);
        lv_obj_set_style_text_color(ssid_lbl, lv_color_white(), 0);
        lv_obj_align(ssid_lbl, LV_ALIGN_LEFT_MID, 12, 0);
        lv_label_set_long_mode(ssid_lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(ssid_lbl, lv_pct(70));

        lv_obj_t *bars_lbl = lv_label_create(row);
        lv_label_set_text(bars_lbl, rssi_bars(aps[i].rssi));
        lv_obj_set_style_text_font(bars_lbl, &font_normal_26, 0);
        lv_obj_set_style_text_color(bars_lbl, lv_color_hex(0x60D060), 0);
        lv_obj_align(bars_lbl, LV_ALIGN_RIGHT_MID, -12, 0);

        if (aps[i].authmode == 0) { // open
            lv_obj_t *open_lbl = lv_label_create(row);
            lv_label_set_text(open_lbl, LV_SYMBOL_WIFI);
            lv_obj_set_style_text_color(open_lbl, lv_color_hex(0x808080), 0);
            lv_obj_align_to(open_lbl, bars_lbl, LV_ALIGN_OUT_LEFT_MID, -4, 0);
        }
    }
    lv_label_set_text(ctx->status_label, "Tap a network to connect");
}

// Callbacks run in the LVGL task via lv_async_call — safe to touch LVGL objects.

static void do_scan_update(void *arg)
{
    scan_ctx_t *ctx = (scan_ctx_t*)arg;
    if (!lv_obj_is_valid(ctx->screen)) return;
    populate_list(ctx);
}

static void do_connected_update(void *arg)
{
    scan_ctx_t *ctx = (scan_ctx_t*)arg;
    if (!lv_obj_is_valid(ctx->screen)) return;
    lv_label_set_text_fmt(ctx->status_label, "Connected: %s",
                          wifi_manager_connected_ssid());
}

static void on_scan_done(void *handler_arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    (void)base; (void)id; (void)data;
    // Running in event loop task — schedule LVGL work on the LVGL task.
    lv_async_call(do_scan_update, handler_arg);
}

static void on_connected(void *handler_arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    (void)base; (void)id; (void)data;
    lv_async_call(do_connected_update, handler_arg);
}

static void wake_and_scan_task(void *arg)
{
    (void)arg;
    wifi_manager_wake();
    wifi_manager_scan();
    vTaskDelete(NULL);
}

// Runs whenever ctx->screen is actually destroyed — whether via scan_back_cb's
// async delete, or because a gesture swept the dynamic settings subtile away
// (ui_tileview's tileview_change_cb deletes it without calling back into us).
// Doing all teardown here, exactly once, regardless of exit path is what makes
// this safe: the alternative (cleanup tied to one button) left the ESP event
// handlers registered — and any lv_async_call they'd already queued pointing
// at ctx — dangling after a gesture-based exit, a races-with-serial-attached
// use-after-free.
static void scan_on_delete(lv_event_t *e)
{
    scan_ctx_t *ctx = (scan_ctx_t*)lv_event_get_user_data(e);
    esp_event_handler_instance_unregister(WIFI_MANAGER_EVENT_BASE,
                                          WIFI_MGR_EVT_SCAN_DONE,
                                          ctx->scan_handler);
    esp_event_handler_instance_unregister(WIFI_MANAGER_EVENT_BASE,
                                          WIFI_MGR_EVT_CONNECTED,
                                          ctx->conn_handler);
    // Cancel any in-flight async updates that captured this ctx — otherwise a
    // scan/connect event that fired moments ago could still run do_scan_update
    // / do_connected_update against memory we're about to free.
    lv_async_call_cancel(do_scan_update, ctx);
    lv_async_call_cancel(do_connected_update, ctx);
    // If user backed out without connecting, shut the radio down off the LVGL task.
    if (!wifi_manager_is_connected()) {
        // 4096, not 2048 — see the matching comment in ntp_sync.c's
        // wifi_release_task: esp_wifi_stop()'s log-chatty teardown chain
        // overflows a 2048-byte stack once SD logging routes every ESP_LOG
        // call through sd_log_vprintf's deeper hook chain.
        xTaskCreate(release_task, "wifi_rel", 4096, NULL, 5, NULL);
    }
    free(ctx);
}

static void scan_back_cb(lv_event_t *e)
{
    scan_ctx_t *ctx = (scan_ctx_t*)lv_event_get_user_data(e);
    // Defer — we're inside a click event on a button that is a descendant of
    // ctx->screen; synchronous lv_obj_del here would be a use-after-free.
    // Teardown happens in scan_on_delete once the object is actually gone.
    lv_obj_del_async(ctx->screen);
}

static void rescan_cb(lv_event_t *e)
{
    scan_ctx_t *ctx = (scan_ctx_t*)lv_event_get_user_data(e);
    if (!settings_get_wifi_enabled()) {
        lv_label_set_text(ctx->status_label, "Wi-Fi is off");
        return;
    }
    lv_label_set_text(ctx->status_label, "Scanning...");
    xTaskCreate(wake_and_scan_task, "wifi_scan", 3072, NULL, 5, NULL);
}

void wifi_scan_screen_open(lv_obj_t *parent)
{
    scan_ctx_t *ctx = calloc(1, sizeof(scan_ctx_t));
    if (!ctx) return;

    ctx->screen = make_screen(parent);
    lv_obj_add_event_cb(ctx->screen, scan_on_delete, LV_EVENT_DELETE, ctx);

    // Header
    lv_obj_t *hdr  = make_header(ctx->screen, "Wi-Fi", scan_back_cb, ctx);
    (void)hdr;

    // Rescan button in header
    lv_obj_t *rescan = lv_label_create(hdr);
    lv_label_set_text(rescan, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_font(rescan, &font_normal_32, 0);
    lv_obj_set_style_text_color(rescan, lv_color_hex(0x4090FF), 0);
    lv_obj_align(rescan, LV_ALIGN_RIGHT_MID, -50, 0);
    lv_obj_add_flag(rescan, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(rescan, rescan_cb, LV_EVENT_CLICKED, ctx);

    // Status bar
    ctx->status_label = lv_label_create(ctx->screen);
    lv_label_set_text(ctx->status_label, "Scanning...");
    lv_obj_set_style_text_font(ctx->status_label, &font_normal_26, 0);
    lv_obj_set_style_text_color(ctx->status_label, lv_color_hex(0x808080), 0);
    lv_obj_align(ctx->status_label, LV_ALIGN_TOP_MID, 0, 56);

    // Scrollable list
    ctx->list = lv_obj_create(ctx->screen);
    lv_obj_remove_style_all(ctx->list);
    lv_obj_set_size(ctx->list, lv_pct(100) - 16, lv_pct(100) - 90);
    lv_obj_align(ctx->list, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_opa(ctx->list, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(ctx->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->list, 6, 0);

    // Register for events
    esp_event_handler_instance_register(WIFI_MANAGER_EVENT_BASE,
        WIFI_MGR_EVT_SCAN_DONE, on_scan_done, ctx, &ctx->scan_handler);
    esp_event_handler_instance_register(WIFI_MANAGER_EVENT_BASE,
        WIFI_MGR_EVT_CONNECTED, on_connected, ctx, &ctx->conn_handler);

    // Show existing cached results immediately (if any from a prior scan)
    {
        wifi_manager_ap_t tmp[1];
        if (wifi_manager_get_scan_results(tmp, 1) > 0) populate_list(ctx);
    }
    if (!settings_get_wifi_enabled()) {
        // Respect the user's WiFi permission — don't wake the radio just
        // because they opened the scan screen; they must enable WiFi first.
        lv_label_set_text(ctx->status_label, "Wi-Fi is off");
    } else {
        // Wake radio and scan on a separate task so the LVGL task isn't blocked
        // by esp_wifi_start() which can take 100-200 ms.
        xTaskCreate(wake_and_scan_task, "wifi_scan", 3072, NULL, 5, NULL);
    }
}

// ── NTP settings screen ───────────────────────────────────────────────────────
//
// Mirrors setting_signalk_screen.c: a single "Server" row that opens a
// chooser (Enter IP / Enter Hostname), reusing ip_picker / scroll_keyboard
// for entry. No port row — NTP always uses the standard port.

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *server_value;   // right-hand label on Server row
} ntp_ctx_t;

static void ntp_on_delete(lv_event_t *e) {
    ntp_ctx_t *ctx = (ntp_ctx_t *)lv_event_get_user_data(e);
    if (ctx) free(ctx);
}

static void ntp_screen_events(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_GESTURE &&
        lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_active());
        ui_dynamic_subtile_close();
    }
}

static void ntp_refresh_label(ntp_ctx_t *ctx) {
    const char *s = ntp_sync_get_server();
    lv_label_set_text(ctx->server_value, (s && s[0]) ? s : "Not set");
}

// ── IP picker integration ────────────────────────────────────────────────

static void ntp_ip_done(const char *ip_str, void *user) {
    ntp_ctx_t *ctx = (ntp_ctx_t *)user;
    ESP_LOGI(TAG, "NTP server IP set: %s", ip_str);
    ntp_sync_set_server(ip_str);
    ntp_refresh_label(ctx);
}

static void ntp_ip_cancel(void *user) { (void)user; }

// ── Hostname keyboard integration ────────────────────────────────────────

typedef struct {
    lv_obj_t *kb_screen;
    ntp_ctx_t *parent_ctx;
} ntp_host_kb_ctx_t;

static void ntp_host_kb_done(const char *text, void *user) {
    ntp_host_kb_ctx_t *kb = (ntp_host_kb_ctx_t *)user;
    if (text && text[0]) {
        ESP_LOGI(TAG, "NTP server hostname set: %s", text);
        ntp_sync_set_server(text);
        ntp_refresh_label(kb->parent_ctx);
    }
    lv_obj_del_async(kb->kb_screen);
    free(kb);
}

static void ntp_host_kb_cancel(void *user) {
    ntp_host_kb_ctx_t *kb = (ntp_host_kb_ctx_t *)user;
    lv_obj_del_async(kb->kb_screen);
    free(kb);
}

// ── Chooser overlay (Enter IP / Enter Hostname) ──────────────────────────

typedef struct {
    lv_obj_t *overlay;
    ntp_ctx_t *parent_ctx;
} ntp_chooser_ctx_t;

static void ntp_overlay_close(ntp_chooser_ctx_t *c) {
    lv_obj_del_async(c->overlay);
    free(c);
}

static void ntp_chooser_pick_ip(lv_event_t *e) {
    ntp_chooser_ctx_t *c = (ntp_chooser_ctx_t *)lv_event_get_user_data(e);
    ntp_ctx_t *ctx = c->parent_ctx;
    lv_obj_t *parent = ctx->screen;
    ntp_overlay_close(c);
    // ip_picker creates a full-screen child of parent and self-deletes on
    // Set or swipe-right.
    ip_picker_create(parent, ntp_sync_get_server(), ntp_ip_done, ntp_ip_cancel, ctx);
}

static void ntp_chooser_pick_hostname(lv_event_t *e) {
    ntp_chooser_ctx_t *c = (ntp_chooser_ctx_t *)lv_event_get_user_data(e);
    ntp_ctx_t *ctx = c->parent_ctx;
    lv_obj_t *parent = ctx->screen;
    ntp_overlay_close(c);

    ntp_host_kb_ctx_t *kb = (ntp_host_kb_ctx_t *)calloc(1, sizeof(ntp_host_kb_ctx_t));
    if (!kb) return;
    kb->parent_ctx = ctx;
    kb->kb_screen = lv_obj_create(parent);
    lv_obj_remove_style_all(kb->kb_screen);
    lv_obj_set_size(kb->kb_screen, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(kb->kb_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(kb->kb_screen, LV_OPA_COVER, 0);
    scroll_keyboard_create(kb->kb_screen, ntp_sync_get_server(),
                           "NTP Server", ntp_host_kb_done, ntp_host_kb_cancel, kb);
}

static void ntp_chooser_cancel(lv_event_t *e) {
    ntp_chooser_ctx_t *c = (ntp_chooser_ctx_t *)lv_event_get_user_data(e);
    ntp_overlay_close(c);
}

static lv_obj_t *ntp_chooser_button(lv_obj_t *parent, const char *txt,
                                     lv_event_cb_t cb, void *user) {
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, 220, 60);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &font_bold_28, 0);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    return b;
}

static void ntp_open_chooser(ntp_ctx_t *ctx) {
    ntp_chooser_ctx_t *c = (ntp_chooser_ctx_t *)calloc(1, sizeof(ntp_chooser_ctx_t));
    if (!c) return;
    c->parent_ctx = ctx;
    c->overlay = lv_obj_create(ctx->screen);
    lv_obj_remove_style_all(c->overlay);
    lv_obj_set_size(c->overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(c->overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(c->overlay, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(c->overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c->overlay, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(c->overlay, 16, 0);

    lv_obj_t *title = lv_label_create(c->overlay);
    lv_obj_set_style_text_font(title, &font_bold_32, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_label_set_text(title, "Server");

    ntp_chooser_button(c->overlay, "Enter IP",       ntp_chooser_pick_ip,       c);
    ntp_chooser_button(c->overlay, "Enter Hostname", ntp_chooser_pick_hostname, c);
    ntp_chooser_button(c->overlay, "Cancel",         ntp_chooser_cancel,        c);
}

// ── Row tap handler ───────────────────────────────────────────────────────

static void ntp_on_server_row(lv_event_t *e) {
    ntp_ctx_t *ctx = (ntp_ctx_t *)lv_event_get_user_data(e);
    ntp_open_chooser(ctx);
}

// Row factory — copy of setting_signalk_screen's make_row, kept local so this
// screen owns its layout choices.
static lv_obj_t *ntp_make_row(lv_obj_t *parent, const char *icon,
                               const char *label_txt, lv_event_cb_t cb,
                               void *user) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, 56);
    lv_obj_set_style_bg_opa(row, LV_OPA_10, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_pad_hor(row, 12, 0);
    lv_obj_set_style_pad_ver(row, 6, 0);
    lv_obj_set_style_margin_bottom(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    if (cb) lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, user);

    if (icon) {
        lv_obj_t *isym = lv_label_create(row);
        lv_obj_set_style_text_font(isym, &font_normal_28, 0);
        lv_label_set_text(isym, icon);
    }

    lv_obj_t *l = lv_label_create(row);
    lv_obj_set_style_text_font(l, &font_normal_28, 0);
    lv_obj_set_style_pad_left(l, 10, 0);
    lv_label_set_text(l, label_txt);
    lv_obj_set_flex_grow(l, 1);

    lv_obj_t *v = lv_label_create(row);
    lv_obj_set_style_text_font(v, &font_bold_28, 0);
    lv_label_set_text(v, "");
    return v;
}

void ntp_settings_screen_open(lv_obj_t *parent)
{
    ntp_ctx_t *ctx = (ntp_ctx_t *)calloc(1, sizeof(ntp_ctx_t));
    if (!ctx) return;

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
    lv_obj_add_event_cb(ctx->screen, ntp_screen_events, LV_EVENT_GESTURE, ctx);
    lv_obj_add_event_cb(ctx->screen, ntp_on_delete,      LV_EVENT_DELETE,  ctx);

    lv_obj_t *title = lv_label_create(ctx->screen);
    lv_obj_set_style_text_font(title, &font_bold_32, 0);
    lv_label_set_text(title, "NTP Server");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *content = lv_obj_create(ctx->screen);
    lv_obj_remove_style_all(content);
    lv_obj_add_flag(content, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_size(content, lv_pct(100), lv_pct(70));
    lv_obj_align(content, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_style_pad_left(content, 16, 0);
    lv_obj_set_style_pad_right(content, 16, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    // START so the first row is reachable by scroll (same gotcha as the main
    // settings menu).
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

    ctx->server_value = ntp_make_row(content, LV_SYMBOL_WIFI, "Server", ntp_on_server_row, ctx);

    ntp_refresh_label(ctx);
}
