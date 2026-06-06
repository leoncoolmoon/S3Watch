// SignalK settings sub-screen: two rows (Server, Port). Server tap shows a
// small chooser (Enter IP / Enter Hostname) so users can use the IP picker
// for the common case and fall back to the QWERTY keyboard for hostnames.
// Port tap opens the numpad. Each editor creates a full-bleed child screen
// of this sub-screen; deleting it reveals the row list again.

#include "setting_signalk_screen.h"
#include "ui.h"
#include "ui_fonts.h"
#include "settings.h"
#include "ip_picker.h"
#include "numpad.h"
#include "scroll_keyboard.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "SignalK";

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *server_value;   // right-hand label on Server row
    lv_obj_t *port_value;     // right-hand label on Port row
} sk_ctx_t;

static void on_delete(lv_event_t *e) {
    sk_ctx_t *ctx = (sk_ctx_t *)lv_event_get_user_data(e);
    if (ctx) free(ctx);
}

static void screen_events(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_GESTURE &&
        lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_active());
        ui_dynamic_subtile_close();
    }
}

static void refresh_labels(sk_ctx_t *ctx) {
    const char *h = settings_get_signalk_host();
    lv_label_set_text(ctx->server_value, (h && h[0]) ? h : "Not set");
    char pbuf[8];
    snprintf(pbuf, sizeof(pbuf), "%u", (unsigned)settings_get_signalk_port());
    lv_label_set_text(ctx->port_value, pbuf);
}

// ── IP picker integration ────────────────────────────────────────────────

static void ip_done(const char *ip_str, void *user) {
    sk_ctx_t *ctx = (sk_ctx_t *)user;
    ESP_LOGI(TAG, "Server IP set: %s", ip_str);
    settings_set_signalk_host(ip_str);
    refresh_labels(ctx);
}

static void ip_cancel(void *user) { (void)user; }

// ── Hostname keyboard integration ────────────────────────────────────────

typedef struct {
    lv_obj_t *kb_screen;
    sk_ctx_t *parent_ctx;
} host_kb_ctx_t;

static void host_kb_done(const char *text, void *user) {
    host_kb_ctx_t *kb = (host_kb_ctx_t *)user;
    if (text) {
        ESP_LOGI(TAG, "Server hostname set: %s", text);
        settings_set_signalk_host(text);
        refresh_labels(kb->parent_ctx);
    }
    lv_obj_del_async(kb->kb_screen);
    free(kb);
}

static void host_kb_cancel(void *user) {
    host_kb_ctx_t *kb = (host_kb_ctx_t *)user;
    lv_obj_del_async(kb->kb_screen);
    free(kb);
}

// ── Chooser overlay (Enter IP / Enter Hostname) ──────────────────────────

typedef struct {
    lv_obj_t *overlay;
    sk_ctx_t *parent_ctx;
} chooser_ctx_t;

static void overlay_close(chooser_ctx_t *c) {
    lv_obj_del_async(c->overlay);
    free(c);
}

static void chooser_pick_ip(lv_event_t *e) {
    chooser_ctx_t *c = (chooser_ctx_t *)lv_event_get_user_data(e);
    sk_ctx_t *ctx = c->parent_ctx;
    lv_obj_t *parent = ctx->screen;
    overlay_close(c);
    // ip_picker creates a full-screen child of parent and self-deletes on
    // Set or swipe-right.
    ip_picker_create(parent, settings_get_signalk_host(),
                     ip_done, ip_cancel, ctx);
}

static void chooser_pick_hostname(lv_event_t *e) {
    chooser_ctx_t *c = (chooser_ctx_t *)lv_event_get_user_data(e);
    sk_ctx_t *ctx = c->parent_ctx;
    lv_obj_t *parent = ctx->screen;
    overlay_close(c);

    host_kb_ctx_t *kb = (host_kb_ctx_t *)calloc(1, sizeof(host_kb_ctx_t));
    if (!kb) return;
    kb->parent_ctx = ctx;
    kb->kb_screen = lv_obj_create(parent);
    lv_obj_remove_style_all(kb->kb_screen);
    lv_obj_set_size(kb->kb_screen, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(kb->kb_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(kb->kb_screen, LV_OPA_COVER, 0);
    scroll_keyboard_create(kb->kb_screen, settings_get_signalk_host(),
                           "Hostname", host_kb_done, host_kb_cancel, kb);
}

static void chooser_cancel(lv_event_t *e) {
    chooser_ctx_t *c = (chooser_ctx_t *)lv_event_get_user_data(e);
    overlay_close(c);
}

static lv_obj_t *chooser_button(lv_obj_t *parent, const char *txt,
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

static void open_chooser(sk_ctx_t *ctx) {
    chooser_ctx_t *c = (chooser_ctx_t *)calloc(1, sizeof(chooser_ctx_t));
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

    chooser_button(c->overlay, "Enter IP",       chooser_pick_ip,       c);
    chooser_button(c->overlay, "Enter Hostname", chooser_pick_hostname, c);
    chooser_button(c->overlay, "Cancel",         chooser_cancel,        c);
}

// ── Numpad integration (Port) ────────────────────────────────────────────

static void port_done(uint32_t value, void *user) {
    sk_ctx_t *ctx = (sk_ctx_t *)user;
    ESP_LOGI(TAG, "Port set: %u", (unsigned)value);
    settings_set_signalk_port((uint16_t)value);
    refresh_labels(ctx);
}

static void port_cancel(void *user) { (void)user; }

static void open_port(sk_ctx_t *ctx) {
    numpad_create(ctx->screen, "Port",
                  (uint32_t)settings_get_signalk_port(),
                  1, 65535, port_done, port_cancel, ctx);
}

// ── Row tap handlers ─────────────────────────────────────────────────────

static void on_server_row(lv_event_t *e) {
    sk_ctx_t *ctx = (sk_ctx_t *)lv_event_get_user_data(e);
    open_chooser(ctx);
}

static void on_port_row(lv_event_t *e) {
    sk_ctx_t *ctx = (sk_ctx_t *)lv_event_get_user_data(e);
    open_port(ctx);
}

// Row factory (copy of the settings-menu helper, kept local so this screen
// owns its layout choices).
static lv_obj_t *make_row(lv_obj_t *parent, const char *icon,
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

void setting_signalk_screen_create(lv_obj_t *parent) {
    sk_ctx_t *ctx = (sk_ctx_t *)calloc(1, sizeof(sk_ctx_t));
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
    lv_obj_add_event_cb(ctx->screen, screen_events, LV_EVENT_GESTURE, ctx);
    lv_obj_add_event_cb(ctx->screen, on_delete,      LV_EVENT_DELETE,  ctx);

    lv_obj_t *title = lv_label_create(ctx->screen);
    lv_obj_set_style_text_font(title, &font_bold_32, 0);
    lv_label_set_text(title, "SignalK");
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

    ctx->server_value = make_row(content, LV_SYMBOL_WIFI,    "Server", on_server_row, ctx);
    ctx->port_value   = make_row(content, LV_SYMBOL_UPLOAD,  "Port",   on_port_row,   ctx);

    refresh_labels(ctx);
}
