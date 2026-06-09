# audio_alert API

### `audio_alert_init`
```c
esp_err_t audio_alert_init(void);
```
No-op. Hardware init is handled by `audio_manager_init()`. Safe to call but not required.

---

### `audio_alert_notify`
```c
void audio_alert_notify(void);
```
Play a notification sound. Tries `/spiffs/notification.mp3` via `audio_manager_play_mp3()`; falls back to synthesized bell tone if the file is absent or unreadable. Respects `settings_get_sound()` — silently returns if sound is disabled. Blocks the calling task until playback completes (decode runs in an internal SPIRAM task, so small-stack callers are safe). Acquires `AM_CLIENT_NOTIFY` access via `audio_manager`, which pauses music playback if needed and auto-resumes it when done.

---

### `audio_alert_play_startup`
```c
void audio_alert_play_startup(void);
```
Spawn a detached task that waits 400 ms then plays `/spiffs/boot.mp3` via `audio_manager_play_mp3()`. If `boot.mp3` is absent or unreadable, falls back to `audio_alert_notify()` (notification chime). Respects `settings_get_sound()`. The boot sound and the notification chime are independent files — replacing one does not affect the other.

---

### `audio_alert_alarm_start`
```c
void audio_alert_alarm_start(uint8_t idx);
```
Start looping the alarm sound at index `idx` (0–`ALARM_SOUND_COUNT-1`). Spawns a 32 KB FreeRTOS task that calls `audio_manager_play_mp3_looped()` until `audio_alert_alarm_stop()` is called. If a loop is already running it is stopped first. No-op if `settings_get_sound()` is false. `idx` is clamped to 0 if out of range.

Available sounds (`alarm_sound_names[]`):

| idx | Name | File |
|-----|------|------|
| 0 | Alarm | `/spiffs/alarms/alarm.mp3` |
| 1 | Bird Song | `/spiffs/alarms/bird_song.mp3` |
| 2 | Retro Digital | `/spiffs/alarms/retro_digital.mp3` |

---

### `audio_alert_alarm_stop`
```c
void audio_alert_alarm_stop(void);
```
Signal the alarm loop task to stop. Returns immediately; the task self-deletes within one decoded frame (~26 ms). Safe to call even if no alarm is running.

---

### `audio_alert_suspend`
```c
void audio_alert_suspend(void);
```
Delegates to `audio_manager_suspend()`. Mutes and closes the codec if open. The PM listener in `audio_manager` calls this automatically on sleep; this function is available as a manual override.
