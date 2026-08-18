# sd_logger

Mirrors all `ESP_LOG` output to a rotating log file on the SD card for post-mortem debugging.

## Behaviour

- Gated by `settings_get_sd_logging_enabled()`. When disabled, `sd_logger_init()` is a no-op: no card access, no hook installed.
- When enabled, installs a custom `esp_log_set_vprintf` hook that writes each log line to `/sdcard/logs/<boot-timestamp>.log`.
- A new log file is created each boot session; old sessions are retained (manual cleanup required).
- Acquires the SD card via `sd_manager_acquire()` at init (permanent hold — never released). Concurrent holders (e.g. `music_player`) share the mount safely via refcount.

## Usage

Call once, early in `app_main()`, before subsystems start logging heavily:

```c
settings_init();
sd_logger_init();   // must come after settings_init()
```

## Dependencies

`settings`, `sd_manager`, `esp32_s3_touch_amoled_2_06` (BSP — SD mount point)
