#include "wifi_screens.h"
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
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 10, 0);
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

static void scan_close(scan_ctx_t *ctx)
{
    esp_event_handler_instance_unregister(WIFI_MANAGER_EVENT_BASE,
                                          WIFI_MGR_EVT_SCAN_DONE,
                                          ctx->scan_handler);
    esp_event_handler_instance_unregister(WIFI_MANAGER_EVENT_BASE,
                                          WIFI_MGR_EVT_CONNECTED,
                                          ctx->conn_handler);
    // If user backed out without connecting, shut the radio down off the LVGL task.
    if (!wifi_manager_is_connected()) {
        xTaskCreate(release_task, "wifi_rel", 2048, NULL, 5, NULL);
    }
    lv_obj_del(ctx->screen);
    free(ctx);
}

static void scan_back_cb(lv_event_t *e)
{
    scan_ctx_t *ctx = (scan_ctx_t*)lv_event_get_user_data(e);
    scan_close(ctx);
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

static void rescan_cb(lv_event_t *e)
{
    scan_ctx_t *ctx = (scan_ctx_t*)lv_event_get_user_data(e);
    lv_label_set_text(ctx->status_label, "Scanning...");
    xTaskCreate(wake_and_scan_task, "wifi_scan", 3072, NULL, 5, NULL);
}

void wifi_scan_screen_open(lv_obj_t *parent)
{
    scan_ctx_t *ctx = calloc(1, sizeof(scan_ctx_t));
    if (!ctx) return;

    ctx->screen = make_screen(parent);

    // Header
    lv_obj_t *hdr  = make_header(ctx->screen, "Wi-Fi", scan_back_cb, ctx);
    (void)hdr;

    // Rescan button in header
    lv_obj_t *rescan = lv_label_create(hdr);
    lv_label_set_text(rescan, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_font(rescan, &font_normal_32, 0);
    lv_obj_set_style_text_color(rescan, lv_color_hex(0x4090FF), 0);
    lv_obj_align(rescan, LV_ALIGN_RIGHT_MID, -10, 0);
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
    // Wake radio and scan on a separate task so the LVGL task isn't blocked
    // by esp_wifi_start() which can take 100-200 ms.
    xTaskCreate(wake_and_scan_task, "wifi_scan", 3072, NULL, 5, NULL);
}

// ── NTP settings screen ───────────────────────────────────────────────────────

typedef struct { lv_obj_t *screen; } ntp_ctx_t;

static void ntp_back_cb(lv_event_t *e)
{
    ntp_ctx_t *ctx = (ntp_ctx_t*)lv_event_get_user_data(e);
    lv_obj_del(ctx->screen);
    free(ctx);
}

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *cur_label;
} ntp_kb_ctx_t;

static void ntp_kb_done(const char *text, void *user_data)
{
    ntp_kb_ctx_t *ctx = (ntp_kb_ctx_t*)user_data;
    if (text && text[0]) {
        ntp_sync_set_server(text);
        lv_label_set_text_fmt(ctx->cur_label, "Current: %s", text);
    }
    // Defer teardown — see pw_done_cb. Screen owns the keyboard button whose
    // click event we are currently inside.
    lv_obj_del_async(ctx->screen);
    free(ctx);
}

static void ntp_kb_cancel(void *user_data)
{
    ntp_kb_ctx_t *ctx = (ntp_kb_ctx_t*)user_data;
    lv_obj_del_async(ctx->screen);
    free(ctx);
}

static void open_custom_ntp_kb(lv_obj_t *parent, lv_obj_t *cur_label)
{
    ntp_kb_ctx_t *ctx = calloc(1, sizeof(ntp_kb_ctx_t));
    if (!ctx) return;
    ctx->cur_label = cur_label;
    ctx->screen    = make_screen(parent);
    scroll_keyboard_create(ctx->screen, settings_get_ntp_server(),
                           "NTP Server", ntp_kb_done, ntp_kb_cancel, ctx);
}

static void preset_click_cb(lv_event_t *e)
{
    const char *server = (const char*)lv_event_get_user_data(e);
    ntp_sync_set_server(server);
    // Update the current label — find it via the list's parent chain
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *list = lv_obj_get_parent(btn);
    lv_obj_t *scr  = lv_obj_get_parent(list);
    // cur_label is the second child of scr (after header)
    lv_obj_t *cur = lv_obj_get_child(scr, 1);
    if (cur) lv_label_set_text_fmt(cur, "Current: %s", server);
}

typedef struct { lv_obj_t *parent; lv_obj_t *cur_label; } custom_ntp_ctx_t;

static void custom_ntp_cb(lv_event_t *e)
{
    custom_ntp_ctx_t *c = (custom_ntp_ctx_t*)lv_event_get_user_data(e);
    open_custom_ntp_kb(c->parent, c->cur_label);
    free(c);
}

void ntp_settings_screen_open(lv_obj_t *parent)
{
    ntp_ctx_t *ctx = calloc(1, sizeof(ntp_ctx_t));
    if (!ctx) return;

    ctx->screen = make_screen(parent);

    make_header(ctx->screen, "NTP Server", ntp_back_cb, ctx);

    lv_obj_t *cur_label = lv_label_create(ctx->screen);
    lv_label_set_text_fmt(cur_label, "Current: %s", settings_get_ntp_server());
    lv_obj_set_style_text_font(cur_label, &font_normal_26, 0);
    lv_obj_set_style_text_color(cur_label, lv_color_hex(0x60D060), 0);
    lv_obj_align(cur_label, LV_ALIGN_TOP_MID, 0, 58);

    // Presets list
    static const char *presets[] = {
        "pool.ntp.org",
        "time.cloudflare.com",
        "time.google.com",
        "time.apple.com",
        NULL
    };

    lv_obj_t *list = lv_obj_create(ctx->screen);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, lv_pct(100) - 16, lv_pct(100) - 130);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 6, 0);

    for (int i = 0; presets[i]; i++) {
        lv_obj_t *row = lv_obj_create(list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(100), 52);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x1C1C1C), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 10, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, preset_click_cb, LV_EVENT_CLICKED,
                            (void*)presets[i]);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, presets[i]);
        lv_obj_set_style_text_font(lbl, &font_normal_28, 0);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 12, 0);
    }

    // Custom entry row
    lv_obj_t *custom = lv_obj_create(list);
    lv_obj_remove_style_all(custom);
    lv_obj_set_size(custom, lv_pct(100), 52);
    lv_obj_set_style_bg_color(custom, lv_color_hex(0x1A2A3A), 0);
    lv_obj_set_style_bg_opa(custom, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(custom, 10, 0);
    lv_obj_add_flag(custom, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(custom, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *clbl = lv_label_create(custom);
    lv_label_set_text(clbl, "Custom...");
    lv_obj_set_style_text_font(clbl, &font_normal_28, 0);
    lv_obj_set_style_text_color(clbl, lv_color_hex(0x4090FF), 0);
    lv_obj_align(clbl, LV_ALIGN_LEFT_MID, 12, 0);

    custom_ntp_ctx_t *cc = malloc(sizeof(custom_ntp_ctx_t));
    if (cc) {
        cc->parent    = parent;
        cc->cur_label = cur_label;
        lv_obj_add_event_cb(custom, custom_ntp_cb, LV_EVENT_CLICKED, cc);
    }
}
