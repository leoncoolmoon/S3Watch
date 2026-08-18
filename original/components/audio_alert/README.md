# audio_alert

Plays notification, alarm, and startup sounds from SPIFFS. Pure audio-content component; all hardware is owned by `audio_manager`.

## Responsibilities

- **Notification chime** — plays `/spiffs/notification.mp3`; falls back to a synthesized multi-partial bell tone if the file is absent.
- **Boot sound** — plays `/spiffs/boot.mp3` 400 ms after startup; falls back to the notification chime if absent.
- **Alarm looping** — plays one of three alarm MP3s from `/spiffs/alarms/` in a continuous loop until explicitly stopped; driven by `audio_alert_alarm_start()` / `audio_alert_alarm_stop()`. Uses the `AM_CLIENT_ALARM` codec tier, so the loop **keeps ringing through display sleep** (until Dismiss or the auto-silence timeout).
- Exposes `audio_alert_suspend()` as a safety valve (delegates to `audio_manager_suspend()`).

## What it does NOT own

- Codec handle, I2S channel, PA enable line — all in `audio_manager`.
- PM no-sleep lock, ALDO3 rail hold — managed by `audio_manager` on open/close.
- PM sleep listener — registered in `audio_manager`.
- minimp3 decoder — housed in `audio_manager/third_party/minimp3/`.

## Initialization

`audio_alert_init()` is a no-op. Hardware is initialized by `audio_manager_init()`. No explicit call to `audio_alert_init()` is required.

## Dependencies

`audio_manager`, `settings`
