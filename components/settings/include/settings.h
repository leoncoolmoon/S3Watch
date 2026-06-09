#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_DISPLAY_TIMEOUT_10S 10000
#define SETTINGS_DISPLAY_TIMEOUT_20S 20000
#define SETTINGS_DISPLAY_TIMEOUT_30S 30000
#define SETTINGS_DISPLAY_TIMEOUT_1MIN 60000

void settings_init(void);
void settings_set_brightness(uint8_t level);
uint8_t settings_get_brightness(void);
void settings_set_display_timeout(uint32_t timeout);
uint32_t settings_get_display_timeout(void);
void settings_set_sound(bool enabled);
bool settings_get_sound(void);

// Alert volume (0-100) — applied to audio_alert playback.
void settings_set_notify_volume(uint8_t vol_percent);
uint8_t settings_get_notify_volume(void);

// Alarm sound selection: index into alarm_sound_names[] (0=Alarm, 1=Bird Song, 2=Retro Digital).
void    settings_set_alarm_sound(uint8_t idx);
uint8_t settings_get_alarm_sound(void);

// Alarm time + armed state. Persisted so a set alarm survives reboot; the
// alarm engine (alarm_clock.c) reads these at boot. Hour 0-23, min 0-59.
void settings_set_alarm_hour(int hour);
int  settings_get_alarm_hour(void);
void settings_set_alarm_min(int min);
int  settings_get_alarm_min(void);
void settings_set_alarm_enabled(bool enabled);
bool settings_get_alarm_enabled(void);

// Auto-silence timeout for a ringing alarm, in minutes (clamped 1-30, default 10).
void settings_set_alarm_timeout_min(int minutes);
int  settings_get_alarm_timeout_min(void);

// Persist settings to SPIFFS JSON and load from it
bool settings_save(void);
bool settings_load(void);

// Settings backup/restore via SD card — a single snapshot file at the SD
// card root (overwritten each time you back up; there's only ever one).
// Manual only, driven from the Backup & Restore screen. Restoring an
// older/incomplete backup is safe: missing fields simply keep their current
// values (same tolerant per-field parsing as the normal SPIFFS load path).
// All three return false if no SD card is present or the operation
// otherwise fails — never blocks or disrupts the rest of the app.
bool settings_backup_to_sd(void);
bool settings_restore_from_sd(void);
bool settings_sd_backup_exists(void);

// NTP server hostname
void        settings_set_ntp_server(const char *server);
const char *settings_get_ntp_server(void);

// Timezone as a POSIX TZ string (e.g. "UTC0", "EST5EDT,M3.2.0/2,M11.1.0/2").
// On set, setenv("TZ", ...) + tzset() is called immediately so localtime_r()
// picks up the new value. On boot, settings_init() applies the loaded value.
// Default: "UTC0".
void        settings_set_tz(const char *posix_tz);
const char *settings_get_tz(void);

// Time format: true = 24h, false = 12h
void settings_set_time_24h(bool enabled);
bool settings_get_time_24h(void);

// WiFi permission: true = user has allowed WiFi (NTP syncs daily, releases after)
void settings_set_wifi_enabled(bool enabled);
bool settings_get_wifi_enabled(void);

// SD-card logging: true = mirror ESP_LOG output to /sdcard/logs/ (takes
// effect on next boot — see sd_logger_init()).
void settings_set_sd_logging_enabled(bool enabled);
bool settings_get_sd_logging_enabled(void);

// SignalK server address (IPv4 string like "10.10.10.1" or a hostname) and
// TCP port. Empty host means "not configured" — consumers must check before
// attempting to connect. Port is clamped to [1, 65535].
void        settings_set_signalk_host(const char *host);   // accepts empty
const char *settings_get_signalk_host(void);
void        settings_set_signalk_port(uint16_t port);
uint16_t    settings_get_signalk_port(void);

// Watchface style: 0=face1, 1=face2
void settings_set_watchface_style(int style);
int  settings_get_watchface_style(void);

// Watchface background: 0=background_wf_2, 1=background_wf
void settings_set_watchface_bg(int bg);
int  settings_get_watchface_bg(void);

// Restore factory defaults and persist
bool settings_reset_defaults(void);

// Maintenance: format SPIFFS storage partition
bool settings_format_spiffs(void);

#ifdef __cplusplus
}
#endif
