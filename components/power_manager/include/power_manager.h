#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

// Call in main BEFORE display_manager_init.
// Creates the no-light-sleep PM lock, calls esp_pm_configure, initialises
// the task_coordinator, and subscribes PMU/button polling callbacks.
void power_manager_init(void);

// ── Sleep / wake ─────────────────────────────────────────────────────────
typedef enum { PM_WAKE_BUTTON, PM_WAKE_ALARM, PM_WAKE_NOTIFY } pm_wake_src_t;
void power_manager_request_wake(pm_wake_src_t source);
void power_manager_request_sleep(void);
bool power_manager_is_awake(void);

// ── Multi-slot event listeners (replaces single-slot s_pre_off_cb) ────────
// Up to 8 listeners; fired in registration order.
// Callbacks must complete quickly (<50 ms) — called from coordinator task.
typedef enum { PM_EVT_PREPARE_SLEEP, PM_EVT_WOKE_UP } pm_event_t;
typedef void (*pm_event_cb_t)(pm_event_t event, void *ctx);
void power_manager_add_listener(pm_event_cb_t cb, void *ctx);

// ── ALDO rail resource management ────────────────────────────────────────
// Reference-counted per named client.  power_manager maps each client to
// its set of BSP rails internally; callers never reference bsp_power_rail_t.
// hold/release are ISR-safe (no I2C) — actual rail writes happen in the
// sleep/wake sequences.
typedef enum { PM_RAIL_CLIENT_AUDIO } pm_rail_client_t;
void power_manager_rail_hold(pm_rail_client_t client);
void power_manager_rail_release(pm_rail_client_t client);

// ── Light-sleep prevention ────────────────────────────────────────────────
// Reference-counted ESP_PM_NO_LIGHT_SLEEP lock (one underlying lock shared
// by the whole system).  Replaces separate locks in display_manager and
// music_player.
void power_manager_no_sleep_hold(void);
void power_manager_no_sleep_release(void);

// ── Display driver registration ───────────────────────────────────────────
// Called once by display_manager_init.  power_manager invokes these during
// its sleep/wake sequences.
typedef struct {
    void (*on_sleep)(void); // suspend LVGL, sleep touch IC, bsp_display_sleep
    void (*on_wake)(void);  // pulse RESETB, bsp_display_wake, I2C recover, resume LVGL
} pm_display_ops_t;
void power_manager_register_display_ops(const pm_display_ops_t *ops);

#ifdef __cplusplus
}
#endif
