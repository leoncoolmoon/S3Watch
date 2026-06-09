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
Spawn a detached task that waits 400 ms then calls `audio_alert_notify()`. Intended for boot-time audio feedback after the system is fully initialized. Respects `settings_get_sound()`.

---

### `audio_alert_suspend`
```c
void audio_alert_suspend(void);
```
Delegates to `audio_manager_suspend()`. Mutes and closes the codec if open. The PM listener in `audio_manager` calls this automatically on sleep; this function is available as a manual override.
