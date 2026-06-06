// SignalK WebSocket client.
//
// Lifecycle:
//   start()  : wifi_manager_wake() -> WIFI_MGR_EVT_CONNECTED ->
//              esp_wifi_set_ps(WIFI_PS_MAX_MODEM) -> ws_start ->
//              CONNECTED -> send subscribe -> populate value cache.
//   stop()   : ws_close + destroy -> wifi_manager_release() (esp_wifi_stop).
//
// The cache is read by the dashboard's 1 Hz task_coord subscriber via
// signalk_client_get(). Updates happen on the websocket task; coarse
// critical-section guards per-field consistency. The values are tiny POD,
// so this is much cheaper than a mutex.

#include "signalk_client.h"
#include "signalk_alert_parse.h"
#include "settings.h"
#include "wifi_manager.h"
#include "display_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_websocket_client.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "SIGNALK";

typedef struct {
    const char     *path;
    signalk_path_t  id;
} path_map_t;

static const path_map_t s_paths[] = {
    { "navigation.speedOverGround",        SIGNALK_PATH_SOG },
    { "navigation.headingMagnetic",        SIGNALK_PATH_HEADING_MAG },
    { "environment.depth.belowTransducer", SIGNALK_PATH_DEPTH_BELOW_TRANSDUCER },
    { "environment.wind.angleApparent",    SIGNALK_PATH_WIND_ANGLE_APPARENT },
    { "environment.wind.speedApparent",    SIGNALK_PATH_WIND_SPEED_APPARENT },
};

static signalk_value_t      s_values[SIGNALK_PATH_COUNT];
static signalk_alert_t      s_alerts[SIGNALK_ALERT_MAX_ACTIVE];
static signalk_state_t      s_state = SIGNALK_STATE_IDLE;
static esp_websocket_client_handle_t s_ws = NULL;
static portMUX_TYPE         s_mux = portMUX_INITIALIZER_UNLOCKED;
// Serialises lifecycle transitions across the three tasks that touch s_ws:
// the LVGL/tile-change caller (start/stop), the esp_event task (wifi_evt
// reacting to WIFI_MGR_EVT_CONNECTED → open_ws), and any later display-off
// caller. Held only across init/destroy — not inside WS event callbacks.
static SemaphoreHandle_t    s_lifecycle_mux = NULL;
// True between start() and stop() — used by the WiFi event handler to
// distinguish "the dashboard wants WiFi" from "something else brought it up".
static bool                 s_want_wifi = false;

static void set_state(signalk_state_t st) {
    portENTER_CRITICAL(&s_mux);
    s_state = st;
    portEXIT_CRITICAL(&s_mux);
}

signalk_state_t signalk_client_state(void) {
    portENTER_CRITICAL(&s_mux);
    signalk_state_t st = s_state;
    portEXIT_CRITICAL(&s_mux);
    return st;
}

bool signalk_client_get(signalk_path_t p, signalk_value_t *out) {
    if (p >= SIGNALK_PATH_COUNT || !out) return false;
    portENTER_CRITICAL(&s_mux);
    *out = s_values[p];
    portEXIT_CRITICAL(&s_mux);
    return out->valid;
}

static signalk_path_t lookup_path(const char *p) {
    if (!p) return SIGNALK_PATH_COUNT;
    for (size_t i = 0; i < sizeof(s_paths)/sizeof(s_paths[0]); i++) {
        if (strcmp(p, s_paths[i].path) == 0) return s_paths[i].id;
    }
    return SIGNALK_PATH_COUNT;
}

static void cache_update(signalk_path_t id, double v) {
    if (id >= SIGNALK_PATH_COUNT) return;
    int64_t now_ms = esp_timer_get_time() / 1000;
    portENTER_CRITICAL(&s_mux);
    s_values[id].valid      = true;
    s_values[id].value      = v;
    s_values[id].updated_ms = now_ms;
    portEXIT_CRITICAL(&s_mux);
}

// Find slot for path; returns -1 if not found.
// Caller must hold s_mux.
static int find_alert_slot(const char *path) {
    for (int i = 0; i < SIGNALK_ALERT_MAX_ACTIVE; i++) {
        if (s_alerts[i].valid && strcmp(s_alerts[i].path, path) == 0) return i;
    }
    return -1;
}

// Find a free slot, or if all full evict the lowest-severity entry.
// Returns slot index. Caller must hold s_mux.
static int claim_alert_slot(signalk_alert_state_t new_state) {
    for (int i = 0; i < SIGNALK_ALERT_MAX_ACTIVE; i++) {
        if (!s_alerts[i].valid) return i;
    }
    // Cache full — evict the lowest-severity entry if the new alert is at
    // least as severe; otherwise drop the new one.
    int min_idx = 0;
    for (int i = 1; i < SIGNALK_ALERT_MAX_ACTIVE; i++) {
        if (s_alerts[i].state < s_alerts[min_idx].state) min_idx = i;
    }
    if (new_state >= s_alerts[min_idx].state) return min_idx;
    return -1;
}

static void clear_alert(const char *path) {
    portENTER_CRITICAL(&s_mux);
    int idx = find_alert_slot(path);
    if (idx >= 0) s_alerts[idx].valid = false;
    portEXIT_CRITICAL(&s_mux);
}

static void update_alert_from_object(const char *path, cJSON *obj) {
    cJSON *js = cJSON_GetObjectItem(obj, "state");
    cJSON *jm = cJSON_GetObjectItem(obj, "message");
    signalk_alert_state_t st = signalk_alert_parse_state(cJSON_IsString(js) ? js->valuestring : NULL);
    if (st == SIGNALK_ALERT_NORMAL) {
        clear_alert(path);
        return;
    }
    const char *msg = (cJSON_IsString(jm) && jm->valuestring) ? jm->valuestring : "";
    int64_t now_ms = esp_timer_get_time() / 1000;

    portENTER_CRITICAL(&s_mux);
    int idx = find_alert_slot(path);
    if (idx < 0) idx = claim_alert_slot(st);
    if (idx >= 0) {
        s_alerts[idx].valid      = true;
        s_alerts[idx].state      = st;
        s_alerts[idx].updated_ms = now_ms;
        strncpy(s_alerts[idx].path,    path, SIGNALK_ALERT_PATH_LEN - 1);
        s_alerts[idx].path[SIGNALK_ALERT_PATH_LEN - 1] = '\0';
        strncpy(s_alerts[idx].message, msg,  SIGNALK_ALERT_MSG_LEN - 1);
        s_alerts[idx].message[SIGNALK_ALERT_MSG_LEN - 1] = '\0';
    }
    portEXIT_CRITICAL(&s_mux);
}

static void parse_delta(const char *json, int len) {
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return;
    cJSON *updates = cJSON_GetObjectItem(root, "updates");
    if (!cJSON_IsArray(updates)) { cJSON_Delete(root); return; }
    cJSON *u;
    cJSON_ArrayForEach(u, updates) {
        cJSON *values = cJSON_GetObjectItem(u, "values");
        if (!cJSON_IsArray(values)) continue;
        cJSON *kv;
        cJSON_ArrayForEach(kv, values) {
            cJSON *jp = cJSON_GetObjectItem(kv, "path");
            cJSON *jv = cJSON_GetObjectItem(kv, "value");
            if (!cJSON_IsString(jp)) continue;
            const char *path = jp->valuestring;

            // Notifications: object value (active) or null (cleared).
            if (strncmp(path, "notifications.", 14) == 0) {
                if (cJSON_IsNull(jv))        clear_alert(path);
                else if (cJSON_IsObject(jv)) update_alert_from_object(path, jv);
                continue;
            }

            // Dashboard scalars.
            signalk_path_t id = lookup_path(path);
            if (id == SIGNALK_PATH_COUNT) continue;
            if (cJSON_IsNumber(jv)) cache_update(id, jv->valuedouble);
        }
    }
    cJSON_Delete(root);
}

int signalk_alerts_count(void) {
    int n = 0;
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < SIGNALK_ALERT_MAX_ACTIVE; i++) {
        if (s_alerts[i].valid && s_alerts[i].state > SIGNALK_ALERT_NORMAL) n++;
    }
    portEXIT_CRITICAL(&s_mux);
    return n;
}

bool signalk_alerts_get(int index, signalk_alert_t *out) {
    if (!out || index < 0) return false;
    // Build a sorted view: copy active entries out under the lock, then
    // sort outside it. SIGNALK_ALERT_MAX_ACTIVE is small (16) so insertion
    // sort is cheap and avoids allocation.
    signalk_alert_t snap[SIGNALK_ALERT_MAX_ACTIVE];
    int n = 0;
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < SIGNALK_ALERT_MAX_ACTIVE; i++) {
        if (s_alerts[i].valid && s_alerts[i].state > SIGNALK_ALERT_NORMAL) {
            snap[n++] = s_alerts[i];
        }
    }
    portEXIT_CRITICAL(&s_mux);
    if (index >= n) return false;
    for (int i = 1; i < n; i++) {
        signalk_alert_t key = snap[i];
        int j = i - 1;
        while (j >= 0 && (snap[j].state < key.state ||
                          (snap[j].state == key.state && snap[j].updated_ms < key.updated_ms))) {
            snap[j + 1] = snap[j];
            j--;
        }
        snap[j + 1] = key;
    }
    *out = snap[index];
    return true;
}

void signalk_alerts_clear(void) {
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < SIGNALK_ALERT_MAX_ACTIVE; i++) s_alerts[i].valid = false;
    portEXIT_CRITICAL(&s_mux);
}

void signalk_test_inject_delta(const char *json, int len) {
    if (!json || len <= 0) return;
    parse_delta(json, len);
}

void signalk_test_clear_values(void) {
    portENTER_CRITICAL(&s_mux);
    memset(s_values, 0, sizeof(s_values));
    portEXIT_CRITICAL(&s_mux);
}

// Static subscribe message; built once.
static const char SUBSCRIBE_JSON[] =
    "{"
    "\"context\":\"vessels.self\","
    "\"subscribe\":["
    "{\"path\":\"navigation.speedOverGround\",\"period\":1000,\"format\":\"delta\",\"policy\":\"instant\",\"minPeriod\":200},"
    "{\"path\":\"navigation.headingMagnetic\",\"period\":1000,\"format\":\"delta\",\"policy\":\"instant\",\"minPeriod\":200},"
    "{\"path\":\"environment.depth.belowTransducer\",\"period\":1000,\"format\":\"delta\",\"policy\":\"instant\",\"minPeriod\":500},"
    "{\"path\":\"environment.wind.angleApparent\",\"period\":500,\"format\":\"delta\",\"policy\":\"instant\",\"minPeriod\":100},"
    "{\"path\":\"environment.wind.speedApparent\",\"period\":500,\"format\":\"delta\",\"policy\":\"instant\",\"minPeriod\":100},"
    "{\"path\":\"notifications.*\",\"period\":1000,\"format\":\"delta\",\"policy\":\"instant\",\"minPeriod\":200}"
    "]"
    "}";

static void ws_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)base;
    esp_websocket_event_data_t *d = (esp_websocket_event_data_t *)data;
    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WS connected; sending subscribe");
        set_state(SIGNALK_STATE_CONNECTED);
        if (s_ws) {
            int sent = esp_websocket_client_send_text(s_ws, SUBSCRIBE_JSON,
                                                      sizeof(SUBSCRIBE_JSON) - 1,
                                                      portMAX_DELAY);
            // -1 / short write = server closed between handshake and send,
            // or the buffer is full. No subscribe means no deltas — surface
            // it as an error so the UI doesn't sit at "Connected" silent.
            if (sent != (int)(sizeof(SUBSCRIBE_JSON) - 1)) {
                ESP_LOGE(TAG, "subscribe send failed (sent=%d)", sent);
                set_state(SIGNALK_STATE_ERROR);
            }
        }
        break;
    case WEBSOCKET_EVENT_DATA:
        if (d && d->op_code == 0x1 /* text */ && d->data_len > 0 && d->data_ptr) {
            parse_delta(d->data_ptr, d->data_len);
        }
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "WS disconnected (err_handle=%d)",
                 d ? (int)d->error_handle.error_type : -1);
        // Don't latch ERROR on a single disconnect — esp_websocket_client
        // auto-reconnects after reconnect_timeout_ms, which fires another
        // CONNECTED event. Show "Connecting…" in the meantime.
        if (signalk_client_state() == SIGNALK_STATE_CONNECTED) {
            set_state(SIGNALK_STATE_CONNECTING);
        }
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WS error (err_handle=%d, esp_tls_err=0x%x)",
                 d ? (int)d->error_handle.error_type : -1,
                 d ? (unsigned)d->error_handle.esp_tls_last_esp_err : 0u);
        if (signalk_client_state() == SIGNALK_STATE_CONNECTED) {
            set_state(SIGNALK_STATE_CONNECTING);
        }
        break;
    default:
        break;
    }
}

static void open_ws(void) {
    // Caller must hold s_lifecycle_mux.
    if (s_ws) return;  // already open
    const char *host = settings_get_signalk_host();
    uint16_t    port = settings_get_signalk_port();
    if (!host || !host[0]) {
        set_state(SIGNALK_STATE_NO_CONFIG);
        return;
    }
    char uri[160];
    snprintf(uri, sizeof(uri), "ws://%s:%u/signalk/v1/stream?subscribe=none",
             host, (unsigned)port);
    ESP_LOGI(TAG, "Opening WS: %s", uri);

    esp_websocket_client_config_t cfg = {0};
    cfg.uri               = uri;
    cfg.reconnect_timeout_ms = 5000;
    cfg.network_timeout_ms   = 5000;

    s_ws = esp_websocket_client_init(&cfg);
    if (!s_ws) {
        ESP_LOGE(TAG, "ws init failed");
        set_state(SIGNALK_STATE_ERROR);
        return;
    }
    if (esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY,
                                      ws_event, NULL) != ESP_OK) {
        // Without event delivery we'd never observe CONNECTED/DATA and
        // sit at CONNECTING forever — tear down here and surface ERROR.
        ESP_LOGE(TAG, "ws register_events failed");
        esp_websocket_client_destroy(s_ws);
        s_ws = NULL;
        set_state(SIGNALK_STATE_ERROR);
        return;
    }
    set_state(SIGNALK_STATE_CONNECTING);
    if (esp_websocket_client_start(s_ws) != ESP_OK) {
        ESP_LOGE(TAG, "ws start failed");
        esp_websocket_client_destroy(s_ws);
        s_ws = NULL;
        set_state(SIGNALK_STATE_ERROR);
    }
}

static void wifi_evt(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)base; (void)data;
    if (!s_want_wifi) return;
    if (id == WIFI_MGR_EVT_CONNECTED) {
        ESP_LOGI(TAG, "WiFi up — setting PS_MIN_MODEM + opening WS");
        // MIN (listen every beacon, ~100 ms typical) instead of MAX
        // (DTIM-interval, 300 ms+). MAX is more efficient on paper but
        // breaks long-lived WebSocket / TCP keepalives on many APs:
        // pings time out, the AP drops the association, the WS errors out
        // moments after first data arrives.
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        if (s_lifecycle_mux && xSemaphoreTake(s_lifecycle_mux, portMAX_DELAY) == pdTRUE) {
            open_ws();
            xSemaphoreGive(s_lifecycle_mux);
        }
    } else if (id == WIFI_MGR_EVT_DISCONNECTED) {
        // Transient: wifi_manager attempts to reconnect on its own. Move
        // back to WIFI_UP so the UI shows "Connecting Wi-Fi…" instead of
        // latching ERROR; the next CONNECTED event resumes the WS path.
        if (signalk_client_state() == SIGNALK_STATE_CONNECTED ||
            signalk_client_state() == SIGNALK_STATE_CONNECTING) {
            set_state(SIGNALK_STATE_WIFI_UP);
        }
    }
}

static void on_display_pre_off(void) {
    // Called inside display_manager turn-off path. Mirrors what the tile-leave
    // hook does so that timing out on the dashboard fully releases the radio.
    signalk_client_stop();
}

void signalk_client_init(void) {
    memset(s_values, 0, sizeof(s_values));
    memset(s_alerts, 0, sizeof(s_alerts));
    if (!s_lifecycle_mux) s_lifecycle_mux = xSemaphoreCreateMutex();
    esp_event_handler_register(WIFI_MANAGER_EVENT_BASE, ESP_EVENT_ANY_ID,
                                wifi_evt, NULL);
    display_manager_set_pre_off_cb(on_display_pre_off);
    set_state(SIGNALK_STATE_IDLE);
}

void signalk_client_start(void) {
    if (signalk_client_state() == SIGNALK_STATE_CONNECTED ||
        signalk_client_state() == SIGNALK_STATE_CONNECTING ||
        signalk_client_state() == SIGNALK_STATE_WIFI_UP) {
        return;
    }
    const char *host = settings_get_signalk_host();
    if (!host || !host[0]) {
        set_state(SIGNALK_STATE_NO_CONFIG);
        return;
    }
    ESP_LOGI(TAG, "start: waking WiFi for SignalK");
    s_want_wifi = true;
    set_state(SIGNALK_STATE_WIFI_UP);
    // Bring the radio up (no-op if already up). The actual connect happens
    // via wifi_manager_auto_connect which is driven from the boot path; we
    // call it again here to ensure the saved-network attempt fires after
    // a release.
    if (wifi_manager_wake() != ESP_OK) {
        set_state(SIGNALK_STATE_ERROR);
        return;
    }
    if (wifi_manager_is_connected()) {
        // Already connected — the wifi event won't fire, jump directly.
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        if (s_lifecycle_mux && xSemaphoreTake(s_lifecycle_mux, portMAX_DELAY) == pdTRUE) {
            open_ws();
            xSemaphoreGive(s_lifecycle_mux);
        }
    } else {
        wifi_manager_auto_connect();
    }
}

void signalk_client_stop(void) {
    // Disarm the wifi-event path before grabbing the lock so a concurrent
    // wifi_evt → open_ws sees s_want_wifi == false and bails before
    // contending here.
    s_want_wifi = false;
    // Use a bounded timeout so callers on the task-coordinator task (via the
    // display pre-off callback) don't block indefinitely if open_ws() is mid-
    // connect (network_timeout_ms = 5 s).  If we can't take the lock in time
    // the WS will be torn down on the next lifecycle call; at worst a single
    // stale WS frame is received.
    bool locked = s_lifecycle_mux &&
                  (xSemaphoreTake(s_lifecycle_mux, pdMS_TO_TICKS(300)) == pdTRUE);
    if (signalk_client_state() != SIGNALK_STATE_IDLE || s_ws) {
        ESP_LOGI(TAG, "stop: closing WS + releasing WiFi");
        if (s_ws) {
            esp_websocket_client_close(s_ws, pdMS_TO_TICKS(200));
            esp_websocket_client_destroy(s_ws);
            s_ws = NULL;
        }
        wifi_manager_release();   // esp_wifi_stop — radio fully off
        signalk_alerts_clear();   // session-local cache; next start re-populates
        set_state(SIGNALK_STATE_IDLE);
    }
    if (locked) xSemaphoreGive(s_lifecycle_mux);
}
