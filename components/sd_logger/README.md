# sd_logger

Mirrors all `ESP_LOG` output to a rotating log file on the SD card for post-mortem debugging.

## Behaviour

- Gated by `settings_get_sd_logging_enabled()`. When disabled, `sd_logger_init()` is a no-op: no card access, no hook installed.
- When enabled, installs a custom `esp_log_set_vprintf` hook that writes each log line to `/sdcard/logs/<boot-timestamp>.log`.
- A new log file is created each boot session; old sessions are retained (manual cleanup required).
- The SD card is mounted by the logger if not already mounted; it stays mounted for the session.

## Usage

Call once, early in `app_main()`, before subsystems start logging heavily:

```c
settings_init();
sd_logger_init();   // must come after settings_init()
```

## Dependencies

`settings`, `esp32_s3_touch_amoled_2_06` (BSP — SD mount point)
