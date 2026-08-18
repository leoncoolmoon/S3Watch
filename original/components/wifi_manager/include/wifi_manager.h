#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "esp_event.h"

// Forward-declared rather than #include "cJSON.h" — keeps this header (and
// every component that includes it, e.g. gui) from needing cJSON on its
// include path just for two backup/restore-only functions. settings.c, the
// only real consumer, already has full cJSON visibility of its own.
typedef struct cJSON cJSON;

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_MANAGER_MAX_NETWORKS   8
#define WIFI_MANAGER_MAX_SSID_LEN   33
#define WIFI_MANAGER_MAX_PASS_LEN   65
#define WIFI_MANAGER_MAX_SCAN_APS   20

ESP_EVENT_DECLARE_BASE(WIFI_MANAGER_EVENT_BASE);

typedef enum {
    WIFI_MGR_EVT_CONNECTED = 1,
    WIFI_MGR_EVT_DISCONNECTED,
    WIFI_MGR_EVT_SCAN_DONE,
    WIFI_MGR_EVT_CONNECT_FAILED,
} wifi_manager_event_id_t;

typedef struct {
    char ssid[WIFI_MANAGER_MAX_SSID_LEN];
    int8_t rssi;
    uint8_t authmode;   // wifi_auth_mode_t — open=0, WPA2=3, WPA3=5
} wifi_manager_ap_t;

esp_err_t wifi_manager_init(void);

// Scan — results delivered via WIFI_MGR_EVT_SCAN_DONE event.
// Call wifi_manager_get_scan_results() from the event handler.
esp_err_t wifi_manager_scan(void);
int       wifi_manager_get_scan_results(wifi_manager_ap_t *out, int max_count);

// Connect to a network and optionally save it to NVS.
esp_err_t wifi_manager_connect(const char *ssid, const char *password, bool save);

// Try connecting to any saved network in order of last-saved.
esp_err_t wifi_manager_auto_connect(void);

// Forget a saved network by SSID.
esp_err_t wifi_manager_forget(const char *ssid);

// Export/import the saved-network list — for settings backup/restore, so a
// reflash doesn't mean re-typing every saved network's password by hand.
// Export returns a new cJSON array of {"ssid","pass"} objects (caller owns —
// must cJSON_Delete), or NULL if nothing is saved. Import replaces the saved
// list with arr's entries (non-object/non-string entries skipped, capped at
// WIFI_MANAGER_MAX_NETWORKS) and persists to NVS immediately; arr itself is
// left untouched (caller retains ownership).
cJSON *wifi_manager_export_networks(void);
bool   wifi_manager_import_networks(const cJSON *arr);

// Lock-free snapshot (volatile bool read; state writes are serialized
// internally).
bool        wifi_manager_is_connected(void);

// Copy the connected SSID into buf (always NUL-terminated; empty when not
// connected). Snapshot taken under the internal lock. Replaces the old
// pointer-returning connected_ssid() getter, which let callers read the
// buffer while the event task was mid-rewrite (connected_rssi() had no
// callers at all and was removed).
void        wifi_manager_get_connected_ssid(char *buf, size_t len);

// Power control — call release() after NTP sync to stop the radio.
// Call wake() before scanning or connecting again.
esp_err_t wifi_manager_release(void);
esp_err_t wifi_manager_wake(void);

// Holds — for consumers with a long-lived need for the radio (e.g. the
// SignalK WebSocket). Several independent subsystems treat "WiFi just came
// up" as their cue to do a quick task and call release() when done (NTP sync
// syncs the clock then releases; the WiFi scan screen wakes to scan then
// releases on close) — that's fine when nothing else needs the radio, but it
// would otherwise yank the radio out from under a long session that merely
// happened to start around the same time. Bracket such a session with
// hold()/unhold(): while any holds are outstanding, release() defers (logs
// and returns ESP_OK without stopping the radio) instead of stopping it out
// from under the holder. unhold() does not itself stop the radio — the
// holder should still call release() when its own session ends.
void wifi_manager_hold(void);
void wifi_manager_unhold(void);

#ifdef __cplusplus
}
#endif
