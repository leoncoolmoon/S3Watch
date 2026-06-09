# settings API

## Lifecycle

```c
void settings_init(void);      // load from SPIFFS, apply TZ; call once after bsp_display_start()
bool settings_save(void);      // persist current values to SPIFFS
bool settings_load(void);      // reload from SPIFFS (usually not needed — init loads once)
bool settings_reset_defaults(void);  // factory reset + persist
bool settings_format_spiffs(void);   // erase SPIFFS storage partition
```

## Display

```c
void    settings_set_brightness(uint8_t level);   // 0–255
uint8_t settings_get_brightness(void);

void     settings_set_display_timeout(uint32_t timeout_ms);
uint32_t settings_get_display_timeout(void);
// Predefined constants: SETTINGS_DISPLAY_TIMEOUT_10S/20S/30S/1MIN
```

## Sound

```c
void    settings_set_sound(bool enabled);
bool    settings_get_sound(void);

void    settings_set_notify_volume(uint8_t vol_percent);  // 0–100
uint8_t settings_get_notify_volume(void);
```

## Time

```c
void        settings_set_tz(const char *posix_tz);   // applies TZ env var immediately
const char *settings_get_tz(void);

void settings_set_time_24h(bool enabled);
bool settings_get_time_24h(void);
```

## Network

```c
void        settings_set_ntp_server(const char *server);
const char *settings_get_ntp_server(void);

void settings_set_wifi_enabled(bool enabled);
bool settings_get_wifi_enabled(void);
```

## SignalK

```c
void        settings_set_signalk_host(const char *host);  // empty = not configured
const char *settings_get_signalk_host(void);

void     settings_set_signalk_port(uint16_t port);  // clamped to [1, 65535]
uint16_t settings_get_signalk_port(void);
```

## UI

```c
void settings_set_watchface_style(int style);   // 0=face1, 1=face2
int  settings_get_watchface_style(void);

void settings_set_watchface_bg(int bg);          // 0=bg_wf_2, 1=bg_wf
int  settings_get_watchface_bg(void);
```

## Logging

```c
void settings_set_sd_logging_enabled(bool enabled);
bool settings_get_sd_logging_enabled(void);
```

## Backup / restore

```c
bool settings_backup_to_sd(void);     // write snapshot to SD card root
bool settings_restore_from_sd(void);  // apply snapshot field-by-field (tolerant of missing fields)
bool settings_sd_backup_exists(void); // true if a backup file exists on SD
```
