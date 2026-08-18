# power_manager API

## Types

```c
typedef enum { PM_WAKE_BUTTON, PM_WAKE_ALARM, PM_WAKE_NOTIFY } pm_wake_src_t;
typedef enum { PM_EVT_PREPARE_SLEEP, PM_EVT_WOKE_UP } pm_event_t;
typedef void (*pm_event_cb_t)(pm_event_t event, void *ctx);
typedef enum { PM_RAIL_CLIENT_AUDIO } pm_rail_client_t;

typedef struct {
    void (*on_sleep)(void);
    void (*on_wake)(void);
} pm_display_ops_t;
```

## Functions

### `power_manager_init`
```c
void power_manager_init(void);
```
One-time boot init. Enables all ALDO rails (50 ms settle), creates and acquires the no-sleep lock, calls `esp_pm_configure()`, and starts `task_coordinator` subscribers. Call after `bsp_extra_init()`, before `bsp_display_start()`.

---

### `power_manager_request_sleep` / `power_manager_request_wake`
```c
void power_manager_request_sleep(void);
void power_manager_request_wake(pm_wake_src_t source);
```
Trigger a sleep or wake transition. Idempotent — re-entrancy is guarded by `s_awake`. Both fire the appropriate listener events and invoke the registered display ops.

---

### `power_manager_is_awake`
```c
bool power_manager_is_awake(void);
```
Thread-safe state query. Used by `task_coordinator` to select on/off cadences.

---

### `power_manager_add_listener`
```c
void power_manager_add_listener(pm_event_cb_t cb, void *ctx);
```
Register a callback for `PM_EVT_PREPARE_SLEEP` and `PM_EVT_WOKE_UP`. Up to 8 listeners; fired in registration order. Callbacks run on the `task_coordinator` task — must complete quickly (<50 ms) and must not block.

---

### `power_manager_no_sleep_hold` / `power_manager_no_sleep_release`
```c
void power_manager_no_sleep_hold(void);
void power_manager_no_sleep_release(void);
```
Reference-counted wrapper around the shared `ESP_PM_NO_LIGHT_SLEEP` lock. Acquire before any operation that must not be interrupted by light sleep (I2C, SPI, codec open). Always pair: every hold must have a matching release.

---

### `power_manager_rail_hold` / `power_manager_rail_release`
```c
void power_manager_rail_hold(pm_rail_client_t client);
void power_manager_rail_release(pm_rail_client_t client);
```
The ALDO rail lock. **power_manager is the sole owner of every ALDO rail** — these are the only calls that turn a rail on/off (via `bsp_power_rail_enable`), on the 0↔1 refcount edge: a rail is powered iff some client holds it. Clients:
- `PM_RAIL_CLIENT_AUDIO` → ALDO3 (A3V3, audio analog) — held by `audio_manager` while a codec is open, so ALDO3 is on only during audio.
- `PM_RAIL_CLIENT_DISPLAY` → ALDO1/2/4 (the AMOLED panel rails, switched **as one group**) — held by `display_manager` while the display is on, released on display-off so the panel powers down for low standby (wake does a full panel reinit).

These do AXP2101 I2C, so **call from task context only** (not an ISR). Always pair.

---

### `power_manager_register_display_ops`
```c
void power_manager_register_display_ops(const pm_display_ops_t *ops);
```
Register the display sleep/wake vtable. Called once by `display_manager_init()`. `on_sleep` sends DCS sleep-in then releases the DISPLAY rail lock (cuts ALDO1/2/4); `on_wake` re-holds it (powers them) then reinitialises the power-cycled panel.
