# wifi_manager API

## Types

```c
typedef enum {
    WIFI_MGR_EVT_CONNECTED = 1,
    WIFI_MGR_EVT_DISCONNECTED,
    WIFI_MGR_EVT_SCAN_DONE,
    WIFI_MGR_EVT_CONNECT_FAILED,
} wifi_manager_event_id_t;

typedef struct {
    char    ssid[33];
    int8_t  rssi;
    uint8_t authmode;   // 0=open, 3=WPA2, 5=WPA3
} wifi_manager_ap_t;

ESP_EVENT_DECLARE_BASE(WIFI_MANAGER_EVENT_BASE);
```

## Lifecycle

```c
esp_err_t wifi_manager_init(void);        // call once; does not start radio
esp_err_t wifi_manager_release(void);     // stop radio (deferred if holds outstanding)
esp_err_t wifi_manager_wake(void);        // start radio
```

## Connection

```c
esp_err_t wifi_manager_connect(const char *ssid, const char *password, bool save);
esp_err_t wifi_manager_auto_connect(void);  // try saved networks in order
esp_err_t wifi_manager_forget(const char *ssid);

esp_err_t wifi_manager_scan(void);  // async; results via WIFI_MGR_EVT_SCAN_DONE
int       wifi_manager_get_scan_results(wifi_manager_ap_t *out, int max_count);
```

## State queries

```c
bool        wifi_manager_is_connected(void);
const char *wifi_manager_connected_ssid(void);
int8_t      wifi_manager_connected_rssi(void);
```

## Hold / unhold (for long-lived sessions)

```c
void wifi_manager_hold(void);    // increment hold count; blocks release()
void wifi_manager_unhold(void);  // decrement; does not itself stop radio
```
Call `release()` separately when the session ends, regardless of unhold.

## Backup / restore

```c
cJSON *wifi_manager_export_networks(void);           // caller owns returned cJSON*
bool   wifi_manager_import_networks(const cJSON *arr); // replaces saved list; persists to NVS
```
Both return NULL / false on failure or empty list.
