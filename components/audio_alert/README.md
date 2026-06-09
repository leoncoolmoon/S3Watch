# audio_alert

Plays notification sounds — either an MP3 file from SPIFFS or a synthesized bell tone as fallback. Pure audio-content component; all hardware is owned by `audio_manager`.

## Responsibilities

- Loads and plays `/spiffs/notification.mp3` via `audio_manager_play_mp3()`.
- Falls back to a synthesized multi-partial bell tone if the MP3 is absent or unreadable.
- Schedules a startup tone shortly after boot (delayed 400 ms for PA settle).
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
