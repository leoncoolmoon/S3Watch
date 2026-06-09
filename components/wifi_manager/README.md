# wifi_manager

Manages the ESP32 WiFi radio: saved network credentials, scanning, connecting, and radio power control. Uses the ESP-IDF event system to notify consumers of connection state changes.

## Radio power model

WiFi consumes significant power (~80 mA active). The manager uses a release/wake model:
- `wifi_manager_release()` stops the radio when it's not needed.
- `wifi_manager_wake()` restarts it before scanning or connecting.
- Multiple subsystems call `release()` when done; holds prevent premature shutdown.

### Hold / unhold

When a subsystem needs the radio for an extended session (e.g. SignalK WebSocket), it calls `wifi_manager_hold()` first. While any holds are outstanding, `release()` does nothing. The holder calls both `release()` and `unhold()` when the session ends.

## Saved networks

Up to `WIFI_MANAGER_MAX_NETWORKS` (8) networks are stored in NVS, in order of last-save. `auto_connect()` tries them in order. `forget()` removes a network by SSID.

## Events

Register for `WIFI_MANAGER_EVENT_BASE` events via the ESP-IDF default event loop:

| Event | Meaning |
|-------|---------|
| `WIFI_MGR_EVT_CONNECTED` | Association + IP acquired |
| `WIFI_MGR_EVT_DISCONNECTED` | Link lost |
| `WIFI_MGR_EVT_SCAN_DONE` | Scan complete; call `get_scan_results()` |
| `WIFI_MGR_EVT_CONNECT_FAILED` | Association or DHCP failed |

## Backup / restore

`wifi_manager_export_networks()` / `wifi_manager_import_networks()` let the settings backup include saved credentials. The export returns a `cJSON` array the caller owns; import replaces the saved list from a `cJSON` array.

## Dependencies

ESP-IDF `esp_wifi`, `esp_event`
