# ntp_sync

Synchronises the ESP32 system clock from an NTP server whenever WiFi is available, and triggers a daily re-sync.

## Behaviour

- Registers for `WIFI_MGR_EVT_CONNECTED` at init. Every time WiFi connects, NTP sync runs automatically.
- After sync, calls `wifi_manager_release()` to allow the radio to power down if nothing else needs it.
- `ntp_sync_check()` should be called periodically (e.g. once per second from a timer). It triggers a WiFi wake + sync if more than 24 hours have elapsed since the last successful sync.
- The NTP server hostname is persisted in `settings`.

## Dependencies

`wifi_manager`, `settings`
