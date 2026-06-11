# audio_manager API

## Types

```c
typedef enum {
    AM_CLIENT_MUSIC  = 0,  // low priority — preempted by NOTIFY/ALARM
    AM_CLIENT_NOTIFY = 1,  // high priority — pauses MUSIC, auto-resumes after; closed on display sleep
    AM_CLIENT_ALARM  = 2,  // high priority — pauses MUSIC like NOTIFY, but keeps playing through display sleep
} am_client_t;

typedef void (*am_pause_fn_t)(void);   // must block until codec is closed
typedef void (*am_resume_fn_t)(void);  // async — post a command and return
```

## Functions

### `audio_manager_init`
```c
esp_err_t audio_manager_init(void);
```
Initialize hardware, create mutex, register PM sleep listener. Call once after `power_manager_init()`.

---

### `audio_manager_open`
```c
esp_err_t audio_manager_open(am_client_t client,
                              uint32_t sample_rate, uint8_t bits, uint8_t channels);
```
Open the codec for the given format. Acquires the PM no-sleep lock and ALDO3 rail hold. Applies the last-set volume and unmutes. No-op if same client and format are already open. `AM_CLIENT_NOTIFY` and `AM_CLIENT_ALARM` will pause `AM_CLIENT_MUSIC` first if music is playing.

---

### `audio_manager_close`
```c
void audio_manager_close(am_client_t client);
```
Mute → 10 ms settle → close → release PM lock and ALDO rail. For `AM_CLIENT_NOTIFY` and `AM_CLIENT_ALARM` (any non-MUSIC client), additionally calls the registered `resume_fn` so music restarts automatically — **but only if this client actually paused playing music on open** (it tracks that internally). Music the user had already paused is left paused.

---

### `audio_manager_write`
```c
int audio_manager_write(const void *data, int len);
```
Pass-through to `esp_codec_dev_write()`. Returns bytes written, or 0 if the codec is not open.

---

### `audio_manager_set_volume`
```c
void audio_manager_set_volume(int vol);  // 0–100
```
Sets codec output volume immediately. Persisted internally and applied on next `audio_manager_open()`.

---

### `audio_manager_mute`
```c
void audio_manager_mute(bool mute);
```
Directly set the codec mute register. Used for manual mute control independent of open/close.

---

### `audio_manager_is_open`
```c
bool audio_manager_is_open(void);
```
Returns true if the codec is currently open by any client.

---

### `audio_manager_suspend`
```c
void audio_manager_suspend(void);
```
Close any active `AM_CLIENT_NOTIFY` stream cleanly (mute → 10 ms → close → release PM lock and ALDO rail). **No-op for `AM_CLIENT_MUSIC` and `AM_CLIENT_ALARM`** — both hold their own PM no-sleep lock and ALDO3 rail, so the CPU stays awake and audio supply stays powered while the AMOLED panel DCS-sleeps. This is what lets music play through display sleep and a ringing alarm keep sounding after the screen times out (until dismissed or the auto-silence timeout). Called automatically by the PM listener on `PM_EVT_PREPARE_SLEEP`; can also be called directly.

---

### `audio_manager_play_mp3`
```c
esp_err_t audio_manager_play_mp3(const char *path, am_client_t client);
```
Play an MP3 file from the VFS (e.g. `/spiffs/notification.mp3`). Blocks the caller until playback is complete. Internally dispatches decode to a dedicated 32 KB SPIRAM task — callers with small stacks (UI task, alarm task) are safe. `mp3dec_ex_t` (~11.5 KB) is heap-allocated in SPIRAM; `mp3dec_scratch_t` (~13 KB) is allocated on the decode task's stack per frame.

Returns `ESP_OK` on success, `ESP_FAIL` if the file is not found or cannot be decoded.

---

### `audio_manager_play_mp3_looped`
```c
esp_err_t audio_manager_play_mp3_looped(const char *path,
                                         am_client_t client,
                                         volatile bool *stop);
```
Play an MP3 file in a continuous loop until `*stop` becomes `true`. Blocks the caller. Checks `*stop` between decoded MPEG frames (~26 ms latency at 44.1 kHz). At EOF the file/decoder is closed and reopened to loop from the beginning (expect a brief ~50–100 ms gap between loops). `mp3dec_ex_t` (~11.5 KB) is allocated in SPIRAM.

**Always call from a dedicated FreeRTOS task** with at least 32 KB stack — this function is blocking and long-lived. Set `*stop = true` from another task to stop playback; the function returns after the current frame finishes.

Returns `ESP_OK` when stopped cleanly, `ESP_FAIL` if the file cannot be opened.

---

### `audio_manager_register_music_hooks`
```c
void audio_manager_register_music_hooks(am_pause_fn_t pause_fn,
                                        am_resume_fn_t resume_fn);
```
Register the pause/resume hooks used for notification preemption. Called once by `music_player` after its engine tasks start. Both may be NULL; without them, `AM_CLIENT_NOTIFY` will still open cleanly as long as music has already closed the codec on its own.
