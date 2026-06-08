#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "WIFI_MGR";
#define NVS_NAMESPACE   "wifi_mgr"
#define NVS_KEY_NETS    "networks"

ESP_EVENT_DEFINE_BASE(WIFI_MANAGER_EVENT_BASE);

// Serializes esp_wifi_*() control calls (start/stop/scan_start/connect/
// disconnect). The driver isn't safe against concurrent control calls from
// independent tasks — and this app genuinely has them: ntp_sync spins up a
// one-shot task to release() the radio after sync completes, the Wi-Fi scan
// screen spins up its own task to wake()+scan(), and either can fire while
// the other is mid-flight (e.g. opening Wi-Fi settings just after enabling
// Wi-Fi races release-after-NTP-sync against wake+scan — esp_wifi_stop()
// landing mid-scan corrupts driver state and crashes). Recursive because
// wifi_manager_auto_connect() calls wifi_manager_connect() while "holding" it.
static SemaphoreHandle_t  s_lock         = NULL;
static esp_netif_t       *s_netif        = NULL;
static bool               s_initialized  = false;
static bool               s_radio_running = false;
// Number of outstanding wifi_manager_hold() calls — see wifi_manager_hold()
// for why release() must defer while this is nonzero.
static int                s_hold_count   = 0;
static bool               s_connected    = false;
static char               s_ssid[WIFI_MANAGER_MAX_SSID_LEN];
static int8_t             s_rssi         = 0;
static wifi_manager_ap_t  s_scan_results[WIFI_MANAGER_MAX_SCAN_APS];
static int                s_scan_count   = 0;

// ── NVS helpers ──────────────────────────────────────────────────────────────

static cJSON *load_networks_json(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return cJSON_CreateArray();
    size_t len = 0;
    if (nvs_get_str(h, NVS_KEY_NETS, NULL, &len) != ESP_OK || len == 0) {
        nvs_close(h);
        return cJSON_CreateArray();
    }
    char *buf = malloc(len);
    if (!buf) { nvs_close(h); return cJSON_CreateArray(); }
    nvs_get_str(h, NVS_KEY_NETS, buf, &len);
    nvs_close(h);
    cJSON *arr = cJSON_Parse(buf);
    free(buf);
    return arr ? arr : cJSON_CreateArray();
}

static esp_err_t save_networks_json(cJSON *arr)
{
    char *str = cJSON_PrintUnformatted(arr);
    if (!str) return ESP_FAIL;
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (ret == ESP_OK) {
        ret = nvs_set_str(h, NVS_KEY_NETS, str);
        if (ret == ESP_OK) ret = nvs_commit(h);
        nvs_close(h);
    }
    free(str);
    return ret;
}

// ── WiFi event handler ────────────────────────────────────────────────────────

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_DISCONNECTED) {
            s_connected = false;
            s_ssid[0]   = '\0';
            ESP_LOGI(TAG, "Disconnected");
            esp_event_post(WIFI_MANAGER_EVENT_BASE, WIFI_MGR_EVT_DISCONNECTED,
                           NULL, 0, 0);
        } else if (id == WIFI_EVENT_SCAN_DONE) {
            uint16_t count = WIFI_MANAGER_MAX_SCAN_APS;
            wifi_ap_record_t recs[WIFI_MANAGER_MAX_SCAN_APS];
            esp_wifi_scan_get_ap_records(&count, recs);
            xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
            s_scan_count = 0;
            for (int i = 0; i < count && s_scan_count < WIFI_MANAGER_MAX_SCAN_APS; i++) {
                // Skip hidden SSIDs
                if (recs[i].ssid[0] == '\0') continue;
                // Skip duplicates (same SSID, keep strongest)
                bool dup = false;
                for (int j = 0; j < s_scan_count; j++) {
                    if (strcmp(s_scan_results[j].ssid, (char*)recs[i].ssid) == 0) {
                        dup = true;
                        break;
                    }
                }
                if (dup) continue;
                strncpy(s_scan_results[s_scan_count].ssid, (char*)recs[i].ssid,
                        WIFI_MANAGER_MAX_SSID_LEN - 1);
                s_scan_results[s_scan_count].ssid[WIFI_MANAGER_MAX_SSID_LEN - 1] = '\0';
                s_scan_results[s_scan_count].rssi     = recs[i].rssi;
                s_scan_results[s_scan_count].authmode = recs[i].authmode;
                s_scan_count++;
            }
            xSemaphoreGiveRecursive(s_lock);
            ESP_LOGI(TAG, "Scan done: %d networks", s_scan_count);
            esp_event_post(WIFI_MANAGER_EVENT_BASE, WIFI_MGR_EVT_SCAN_DONE,
                           NULL, 0, 0);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            strncpy(s_ssid, (char*)ap.ssid, WIFI_MANAGER_MAX_SSID_LEN - 1);
            s_ssid[WIFI_MANAGER_MAX_SSID_LEN - 1] = '\0';
            s_rssi = ap.rssi;
        }
        s_connected = true;
        ESP_LOGI(TAG, "Connected to %s", s_ssid);
        esp_event_post(WIFI_MANAGER_EVENT_BASE, WIFI_MGR_EVT_CONNECTED,
                       NULL, 0, 0);
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

esp_err_t wifi_manager_init(void)
{
    if (s_initialized) return ESP_OK;

    s_lock = xSemaphoreCreateRecursiveMutex();

    ESP_ERROR_CHECK(esp_netif_init());
    s_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    s_radio_running = true;

    esp_event_handler_register(WIFI_EVENT,  ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT,    IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);

    s_initialized = true;
    ESP_LOGI(TAG, "Initialized");
    return ESP_OK;
}

esp_err_t wifi_manager_scan(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    wifi_scan_config_t cfg = {
        .ssid        = NULL,
        .bssid       = NULL,
        .channel     = 0,
        .show_hidden = false,
        .scan_type   = WIFI_SCAN_TYPE_ACTIVE,
    };
    xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
    esp_err_t err = esp_wifi_scan_start(&cfg, false);
    xSemaphoreGiveRecursive(s_lock);
    return err;
}

int wifi_manager_get_scan_results(wifi_manager_ap_t *out, int max_count)
{
    xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
    int n = s_scan_count < max_count ? s_scan_count : max_count;
    memcpy(out, s_scan_results, n * sizeof(wifi_manager_ap_t));
    xSemaphoreGiveRecursive(s_lock);
    return n;
}

esp_err_t wifi_manager_connect(const char *ssid, const char *password, bool save)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    wifi_config_t cfg = {0};
    strncpy((char*)cfg.sta.ssid,     ssid,     sizeof(cfg.sta.ssid)     - 1);
    strncpy((char*)cfg.sta.password, password, sizeof(cfg.sta.password) - 1);
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
    esp_wifi_disconnect();
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_config failed: %s", esp_err_to_name(err));
        xSemaphoreGiveRecursive(s_lock);
        return err;
    }
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Connect failed: %s", esp_err_to_name(err));
        esp_event_post(WIFI_MANAGER_EVENT_BASE, WIFI_MGR_EVT_CONNECT_FAILED,
                       NULL, 0, 0);
        xSemaphoreGiveRecursive(s_lock);
        return err;
    }
    xSemaphoreGiveRecursive(s_lock);

    if (save) {
        cJSON *arr = load_networks_json();
        // Remove existing entry for this SSID if present
        int sz = cJSON_GetArraySize(arr);
        for (int i = sz - 1; i >= 0; i--) {
            cJSON *item = cJSON_GetArrayItem(arr, i);
            cJSON *s    = cJSON_GetObjectItem(item, "ssid");
            if (cJSON_IsString(s) && strcmp(s->valuestring, ssid) == 0) {
                cJSON_DeleteItemFromArray(arr, i);
            }
        }
        cJSON *entry = cJSON_CreateObject();
        if (!entry) {
            ESP_LOGE(TAG, "OOM saving network '%s'", ssid);
            cJSON_Delete(arr);
            return ESP_OK;  // connect already succeeded; save is best-effort
        }
        cJSON_AddStringToObject(entry, "ssid", ssid);
        cJSON_AddStringToObject(entry, "pass", password);
        cJSON_AddItemToArray(arr, entry);
        // Keep at most MAX_NETWORKS
        while (cJSON_GetArraySize(arr) > WIFI_MANAGER_MAX_NETWORKS) {
            cJSON_DeleteItemFromArray(arr, 0);
        }
        save_networks_json(arr);
        cJSON_Delete(arr);
        ESP_LOGI(TAG, "Saved network '%s'", ssid);
    }

    return ESP_OK;
}

esp_err_t wifi_manager_auto_connect(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    cJSON *arr = load_networks_json();
    int sz = cJSON_GetArraySize(arr);
    if (sz == 0) { cJSON_Delete(arr); return ESP_ERR_NOT_FOUND; }

    // Try the most-recently-saved network first (last in array)
    cJSON *item = cJSON_GetArrayItem(arr, sz - 1);
    cJSON *s    = cJSON_GetObjectItem(item, "ssid");
    cJSON *p    = cJSON_GetObjectItem(item, "pass");
    esp_err_t ret = ESP_ERR_NOT_FOUND;
    if (cJSON_IsString(s) && cJSON_IsString(p)) {
        ESP_LOGI(TAG, "Auto-connecting to '%s'", s->valuestring);
        ret = wifi_manager_connect(s->valuestring, p->valuestring, false);
    }
    cJSON_Delete(arr);
    return ret;
}

cJSON *wifi_manager_export_networks(void)
{
    cJSON *arr = load_networks_json();
    if (cJSON_GetArraySize(arr) == 0) {
        cJSON_Delete(arr);
        return NULL;
    }
    return arr;
}

bool wifi_manager_import_networks(const cJSON *arr)
{
    if (!cJSON_IsArray(arr)) return false;

    cJSON *out = cJSON_CreateArray();
    if (!out) return false;

    int sz = cJSON_GetArraySize((cJSON *)arr);
    for (int i = 0; i < sz && cJSON_GetArraySize(out) < WIFI_MANAGER_MAX_NETWORKS; i++) {
        cJSON *item = cJSON_GetArrayItem((cJSON *)arr, i);
        cJSON *s    = cJSON_GetObjectItem(item, "ssid");
        cJSON *p    = cJSON_GetObjectItem(item, "pass");
        if (!cJSON_IsString(s) || !cJSON_IsString(p)) continue;

        cJSON *entry = cJSON_CreateObject();
        if (!entry) continue;
        cJSON_AddStringToObject(entry, "ssid", s->valuestring);
        cJSON_AddStringToObject(entry, "pass", p->valuestring);
        cJSON_AddItemToArray(out, entry);
    }

    esp_err_t err = save_networks_json(out);
    int count = cJSON_GetArraySize(out);
    cJSON_Delete(out);
    if (err == ESP_OK) ESP_LOGI(TAG, "Imported %d saved network(s)", count);
    return err == ESP_OK;
}

esp_err_t wifi_manager_forget(const char *ssid)
{
    cJSON *arr = load_networks_json();
    int sz = cJSON_GetArraySize(arr);
    for (int i = sz - 1; i >= 0; i--) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        cJSON *s    = cJSON_GetObjectItem(item, "ssid");
        if (cJSON_IsString(s) && strcmp(s->valuestring, ssid) == 0) {
            cJSON_DeleteItemFromArray(arr, i);
        }
    }
    esp_err_t ret = save_networks_json(arr);
    cJSON_Delete(arr);
    return ret;
}

bool wifi_manager_is_connected(void)          { return s_connected; }
const char *wifi_manager_connected_ssid(void) { return s_ssid; }
int8_t wifi_manager_connected_rssi(void)      { return s_rssi; }

esp_err_t wifi_manager_release(void)
{
    if (!s_initialized) return ESP_OK;

    xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
    if (s_hold_count > 0) {
        // A long-lived consumer (e.g. SignalK's WebSocket) is mid-session —
        // don't yank the radio out from under it just because some unrelated
        // subsystem (e.g. NTP sync, finishing its own quick wake-sync cycle)
        // thinks it's done with WiFi. The holder will release() when its own
        // session ends.
        ESP_LOGI(TAG, "Release deferred — %d active hold(s) on the radio", s_hold_count);
        xSemaphoreGiveRecursive(s_lock);
        return ESP_OK;
    }
    if (!s_radio_running) {
        xSemaphoreGiveRecursive(s_lock);
        return ESP_OK;
    }
    s_connected     = false;
    s_radio_running = false;
    s_ssid[0]       = '\0';
    esp_wifi_disconnect();
    esp_err_t err = esp_wifi_stop();
    xSemaphoreGiveRecursive(s_lock);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Radio stopped (release)");
    }
    return err;
}

esp_err_t wifi_manager_wake(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
    if (s_radio_running) {
        xSemaphoreGiveRecursive(s_lock);
        return ESP_OK;
    }
    esp_err_t err = esp_wifi_start();
    if (err == ESP_OK) {
        s_radio_running = true;
        ESP_LOGI(TAG, "Radio started (wake)");
    }
    xSemaphoreGiveRecursive(s_lock);
    return err;
}

void wifi_manager_hold(void)
{
    xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
    s_hold_count++;
    xSemaphoreGiveRecursive(s_lock);
}

void wifi_manager_unhold(void)
{
    xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
    if (s_hold_count > 0) s_hold_count--;
    xSemaphoreGiveRecursive(s_lock);
}
