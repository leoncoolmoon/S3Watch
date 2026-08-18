#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SIGNALK_PATH_SOG = 0,
    SIGNALK_PATH_HEADING_MAG,
    SIGNALK_PATH_DEPTH_BELOW_TRANSDUCER,
    SIGNALK_PATH_WIND_ANGLE_APPARENT,
    SIGNALK_PATH_WIND_SPEED_APPARENT,
    SIGNALK_PATH_COUNT,
} signalk_path_t;

typedef enum {
    SIGNALK_STATE_IDLE,           // not started — radio off, no client
    SIGNALK_STATE_NO_CONFIG,      // host empty
    SIGNALK_STATE_WIFI_DISABLED,  // user has WiFi permission turned off
    SIGNALK_STATE_WIFI_UP,        // waiting for WiFi association
    SIGNALK_STATE_CONNECTING,     // WS handshake in progress
    SIGNALK_STATE_CONNECTED,      // subscribed, receiving deltas
    SIGNALK_STATE_ERROR,          // last attempt failed; will retry on next start
} signalk_state_t;

typedef struct {
    bool    valid;        // true once at least one delta arrived for this path
    int64_t updated_ms;   // esp_timer_get_time() / 1000
    double  value;        // SI units exactly as received (m/s, rad, m, …)
} signalk_value_t;

// Call once during boot. Registers WiFi event handlers + display-off hook.
void signalk_client_init(void);

// Bring radio up, connect to AP, open WS, subscribe. Idempotent; no-op if
// already started or if host is unconfigured.
void signalk_client_start(void);

// Close WS, disconnect, stop the radio entirely. Idempotent.
void signalk_client_stop(void);

signalk_state_t signalk_client_state(void);
bool            signalk_client_get(signalk_path_t p, signalk_value_t *out);

// ── SignalK notifications (alerts/alarms) ─────────────────────────────────
//
// Every leaf under `notifications.*` is an active alarm/alert/etc.; clear is
// signalled by state -> normal/nominal OR a delta with value: null. The
// client maintains a small cache keyed by full path. The alerts gui reads
// it via signalk_alerts_get(); the client wipes it on stop so the next
// session starts fresh.

#define SIGNALK_ALERT_MAX_ACTIVE 16
#define SIGNALK_ALERT_PATH_LEN   80
#define SIGNALK_ALERT_MSG_LEN    96

typedef enum {
    SIGNALK_ALERT_NORMAL = 0,   // includes "nominal" — never displayed
    SIGNALK_ALERT_ALERT,
    SIGNALK_ALERT_WARN,
    SIGNALK_ALERT_ALARM,
    SIGNALK_ALERT_EMERGENCY,
} signalk_alert_state_t;

typedef struct {
    bool                   valid;
    signalk_alert_state_t  state;
    int64_t                updated_ms;
    char                   path[SIGNALK_ALERT_PATH_LEN];
    char                   message[SIGNALK_ALERT_MSG_LEN];
} signalk_alert_t;

// Number of currently-active alerts (state > NORMAL). 0 == "All Normal".
int  signalk_alerts_count(void);

// Read active alerts by index in severity-desc, most-recent order. Returns
// false if index is out of range. Snapshot copies into out.
bool signalk_alerts_get(int index, signalk_alert_t *out);

// Wipe the cache. Invoked from signalk_client_stop().
void signalk_alerts_clear(void);

// ── Test hooks ────────────────────────────────────────────────────────────
// Used by the on-device test runner (components/ondev_test) to exercise
// the delta parser without spinning up a real WebSocket. Harmless in
// production: ~50 bytes of code, no state change beyond the documented
// cache mutations.

// Feed a delta JSON straight into the parser. Equivalent to a
// WEBSOCKET_EVENT_DATA arriving with this payload.
void signalk_test_inject_delta(const char *json, int len);

// Wipe the scalar value cache (s_values). Counterpart to
// signalk_alerts_clear(). Useful for fixture cleanup between test suites.
void signalk_test_clear_values(void);

#ifdef __cplusplus
}
#endif
