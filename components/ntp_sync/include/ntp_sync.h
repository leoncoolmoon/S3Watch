#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Call once after wifi_manager_init(). Registers for WIFI_MGR_EVT_CONNECTED
// and syncs automatically each time WiFi connects.
esp_err_t ntp_sync_init(void);

// Trigger a sync immediately (must be connected).
esp_err_t ntp_sync_now(void);

// Get/set the NTP server hostname (persisted in settings).
const char *ntp_sync_get_server(void);
void        ntp_sync_set_server(const char *server);

// Periodic re-sync check (called every 30 s by ui_power_events while the
// display is on). Triggers a guarded WiFi wake + sync when the clock is
// invalid, never synced, or >24 h since the last successful sync.
void ntp_sync_check(void);

// Guarded sync attempt: wakes the radio + tries saved networks, throttled to
// one attempt per 6 h, with a 45 s failure watchdog that releases the radio if
// no sync lands (so a failed attempt never leaves WiFi powered). Also gated by
// a 15 s boot-quiet window so radio bring-up / the sync's flash writes never
// land inside the boot tone's tight playback window. Returns
// ESP_ERR_NOT_FINISHED when throttled or boot-quiet, ESP_ERR_INVALID_STATE
// when the user has WiFi disabled. Driven by ntp_sync_check().
esp_err_t ntp_sync_attempt(void);

#ifdef __cplusplus
}
#endif
