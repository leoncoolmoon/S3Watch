# signalk_client

WebSocket client for the [SignalK](https://signalk.org/) marine data protocol. Subscribes to instrument paths (speed, heading, depth, wind) and alert notifications, and makes the latest values available to the UI.

## Data model

SignalK publishes delta updates over WebSocket. The client maintains two caches:

1. **Scalar values** — one `signalk_value_t` per tracked path, updated on each delta. Paths tracked:

| Enum | SignalK path | Unit |
|------|-------------|------|
| `SIGNALK_PATH_SOG` | `navigation.speedOverGround` | m/s |
| `SIGNALK_PATH_HEADING_MAG` | `navigation.headingMagnetic` | rad |
| `SIGNALK_PATH_DEPTH_BELOW_TRANSDUCER` | `environment.depth.belowTransducer` | m |
| `SIGNALK_PATH_WIND_ANGLE_APPARENT` | `environment.wind.angleApparent` | rad |
| `SIGNALK_PATH_WIND_SPEED_APPARENT` | `environment.wind.speedApparent` | m/s |

2. **Alerts** — up to 16 active notifications from `notifications.*` paths. State > NORMAL means the alert is active. Cleared on stop.

## Connection lifecycle

```
signalk_client_init()          // registers WiFi event handler; WiFi not started yet
   ↓ (WiFi comes up)
signalk_client_start()         // wake radio → TCP connect → WS handshake → subscribe
   ↓ (user closes dashboard)
signalk_client_stop()          // close WS → stop radio (unless held elsewhere)
```

`signalk_client_init()` registers for WiFi connect events and starts automatically when WiFi is up and a host is configured.

## Configuration

Server host and port are set via `settings_set_signalk_host()` / `settings_set_signalk_port()`. An empty host means "not configured" — `start()` is a no-op in that case.

## Dependencies

`wifi_manager`, `settings`, `esp_websocket_client`, `json`
