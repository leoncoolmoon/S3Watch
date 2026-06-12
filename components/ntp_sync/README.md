# ntp_sync

Synchronises the ESP32 system clock from an NTP server whenever WiFi is available, and triggers a daily re-sync.

## Behaviour

- Registers for `WIFI_MGR_EVT_CONNECTED` at init. Every time WiFi connects, NTP sync runs automatically.
- After sync, calls `wifi_manager_release()` to allow the radio to power down if nothing else needs it.
- `ntp_sync_check()` is called periodically (every 30 s by `ui_power_events` while the display is on). It triggers a sync attempt when the clock is invalid (dead RTC backup), never synced, or more than 24 hours past the last successful sync.
- **Attempt throttle + failure watchdog** (`ntp_sync_attempt()`): radio wakes for NTP are limited to **one attempt per 6 hours**, and every attempt arms a **45 s deadline** — if no sync lands (away from the saved network, AP down, server unreachable), the radio is released instead of left powered indefinitely (which used to silently drain the battery). The deadline is checked by the **`ntp_watchdog` task_coordinator subscriber** (1 s on / 2 s off — it must run display-off too, since an attempt window can span a display sleep), following the house pattern of coordinator-checked deadlines (same as alarm_manager's auto-silence) rather than a standalone timer. Success disarms it; `wifi_manager_release()` still defers if another consumer (e.g. SignalK) holds the radio. Attempt bookkeeping uses the monotonic `esp_timer` clock, so it's immune to the clock jumps NTP itself causes.
- **Boot-quiet window (15 s):** attempts are refused during early boot (without recording an attempt), and `main.cpp` releases the radio unconditionally at boot. The first sync therefore lands at the first `ntp_sync_check` after the window (~30 s in). Rationale: the boot tone streams MP3 from SPIFFS with only the ~33 ms I2S DMA as buffer — WiFi bring-up plus the sync's NVS commit (a flash-cache stall freezes every flash-resident task, decoder included) caused audible cutouts. The clock is already correct at boot from the battery-backed RTC, so the deferral costs nothing.
- The NTP server hostname is persisted in `settings`.

## Dependencies

`wifi_manager`, `settings`
