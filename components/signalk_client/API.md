# signalk_client API

## Types

```c
typedef enum {
    SIGNALK_PATH_SOG,
    SIGNALK_PATH_HEADING_MAG,
    SIGNALK_PATH_DEPTH_BELOW_TRANSDUCER,
    SIGNALK_PATH_WIND_ANGLE_APPARENT,
    SIGNALK_PATH_WIND_SPEED_APPARENT,
    SIGNALK_PATH_COUNT,
} signalk_path_t;

typedef enum {
    SIGNALK_STATE_IDLE,
    SIGNALK_STATE_NO_CONFIG,
    SIGNALK_STATE_WIFI_DISABLED,
    SIGNALK_STATE_WIFI_UP,
    SIGNALK_STATE_CONNECTING,
    SIGNALK_STATE_CONNECTED,
    SIGNALK_STATE_ERROR,
} signalk_state_t;

typedef struct {
    bool    valid;        // true once a delta has arrived for this path
    int64_t updated_ms;   // esp_timer_get_time() / 1000 at last update
    double  value;        // SI units as received (m/s, rad, m)
} signalk_value_t;

typedef enum {
    SIGNALK_ALERT_NORMAL = 0,
    SIGNALK_ALERT_ALERT,
    SIGNALK_ALERT_WARN,
    SIGNALK_ALERT_ALARM,
    SIGNALK_ALERT_EMERGENCY,
} signalk_alert_state_t;

typedef struct {
    bool                   valid;
    signalk_alert_state_t  state;
    int64_t                updated_ms;
    char                   path[80];
    char                   message[96];
} signalk_alert_t;
```

## Client

```c
void signalk_client_init(void);   // register WiFi handler; call once at boot
void signalk_client_start(void);  // connect + subscribe; idempotent
void signalk_client_stop(void);   // disconnect; idempotent
signalk_state_t signalk_client_state(void);

// Read a scalar value by path enum. Returns false if never received.
bool signalk_client_get(signalk_path_t p, signalk_value_t *out);
```

## Alerts

```c
int  signalk_alerts_count(void);   // active alerts (state > NORMAL)
bool signalk_alerts_get(int index, signalk_alert_t *out);  // by index, severity-desc order
void signalk_alerts_clear(void);   // wipe cache; called by stop()
```

## Test hooks

```c
// Feed a delta JSON directly into the parser (no WebSocket needed).
void signalk_test_inject_delta(const char *json, int len);

// Clear the scalar value cache.
void signalk_test_clear_values(void);
```
