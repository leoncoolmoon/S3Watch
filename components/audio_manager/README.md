# audio_manager

Owns the single ES8311 speaker codec hardware, the minimp3 decoder library, and provides arbitrated, reference-counted access to both.

## Responsibilities

- Calls `bsp_audio_codec_speaker_init()` once at boot — no other component touches the codec handle.
- Holds the `ESP_PM_NO_LIGHT_SLEEP` lock and ALDO3 (A3V3 analog rail) for exactly as long as the codec is open.
- Registers a `power_manager` sleep listener. On `PM_EVT_PREPARE_SLEEP`: leaves `AM_CLIENT_MUSIC` and `AM_CLIENT_ALARM` untouched (their PM lock and ALDO3 hold keep the CPU awake and audio supply live while the AMOLED panel DCS-sleeps), and only closes a `AM_CLIENT_NOTIFY` stream if one happens to be mid-play.
- Arbitrates between three client tiers: `AM_CLIENT_NOTIFY` and `AM_CLIENT_ALARM` (high priority) and `AM_CLIENT_MUSIC` (low priority). When a notification or alarm wants the codec while music is playing, music is paused, the sound plays, then music auto-resumes. NOTIFY and ALARM differ only at display-sleep: NOTIFY is closed, ALARM keeps ringing.
- Houses and compiles the minimp3 decoder (`third_party/minimp3/`). Its headers are exported on the public include path so `music_player` and other dependents can use `minimp3_ex.h` without an additional dependency.
- Provides `audio_manager_play_mp3()` for one-shot MP3 file playback (e.g. notifications). Internally spawns a SPIRAM decode task so callers with small stacks are safe.

## Initialization

Call `audio_manager_init()` in `main()` after `power_manager_init()` and before `bsp_display_start()`.

```c
power_manager_init();
audio_manager_init();   // <-- here
bsp_display_start();
```

## Clients

| Client | Priority | Display-sleep | Used by |
|--------|----------|---------------|---------|
| `AM_CLIENT_MUSIC` | Low — preempted by NOTIFY/ALARM | keeps playing | `music_player` |
| `AM_CLIENT_NOTIFY` | High — preempts MUSIC | closed | `audio_alert` (notifications, startup) |
| `AM_CLIENT_ALARM` | High — preempts MUSIC | keeps playing | `audio_alert` (ringing alarm) |

## Sleep behaviour

`AM_CLIENT_MUSIC` and `AM_CLIENT_ALARM` hold the PM no-sleep lock and ALDO3 rail while open. When `power_manager_request_sleep()` fires:
- The system-awake PM hold is released, but the audio hold remains → no-sleep refcount stays > 0 → CPU stays awake at DFS min freq.
- ALDO3 refcount stays at 1 → audio analog supply stays powered.
- The AMOLED panel DCS-sleeps independently. Music — or a ringing alarm — plays through the dark display. (A ringing alarm therefore keeps the CPU out of light sleep until it is dismissed or auto-silenced; this is bounded by `alarm_timeout_min`.)

## Pause / resume flow (notification preemption)

When `audio_manager_open(AM_CLIENT_NOTIFY, …)` is called while `AM_CLIENT_MUSIC` holds the codec:

1. audio_manager calls the registered `pause_fn` (blocks until music closes).
2. Codec opens for the notification.
3. When `audio_manager_close(AM_CLIENT_NOTIFY)` is called, audio_manager calls `resume_fn` (async).
4. Music reopens the codec and continues from where it left off.

`music_player` registers these hooks at engine-start via `audio_manager_register_music_hooks()`.

## Dependencies

`esp32_s3_touch_amoled_2_06` (BSP — codec init), `power_manager`
