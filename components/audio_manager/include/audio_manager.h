#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Priority levels for codec access.  NOTIFY preempts MUSIC: if MUSIC holds the
// codec when NOTIFY calls audio_manager_open(), music is paused (via registered
// hook), the notification plays, and music auto-resumes when it closes.
typedef enum {
    AM_CLIENT_MUSIC  = 0,
    AM_CLIENT_NOTIFY = 1,
} am_client_t;

// Registered by music_player so audio_manager can pause/resume it.
// pause_fn must block until the music task has closed the codec.
// resume_fn is async (posts a command and returns immediately).
typedef void (*am_pause_fn_t)(void);
typedef void (*am_resume_fn_t)(void);

// Call once from main() after power_manager_init(), before bsp_display_start().
esp_err_t audio_manager_init(void);

// Open codec for the given client + format.  Handles PM no-sleep lock, ALDO
// rail hold, 10 ms settle, and unmute.  No-op if same client + format already
// open.  AM_CLIENT_NOTIFY will pause music first if music is playing.
esp_err_t audio_manager_open(am_client_t client,
                              uint32_t sample_rate, uint8_t bits, uint8_t channels);

// Graceful close: mute -> 10 ms -> close -> release lock + rail.
// AM_CLIENT_NOTIFY additionally calls the registered resume hook so music
// restarts automatically.
void audio_manager_close(am_client_t client);

// Raw PCM write; thin pass-through to esp_codec_dev_write().
int audio_manager_write(const void *data, int len);

// Volume (0-100) and mute.  volume is persisted for next open.
void audio_manager_set_volume(int vol);
void audio_manager_mute(bool mute);

bool audio_manager_is_open(void);

// Mute + close if open.  Called internally by the PM sleep listener; can also
// be called externally to force a clean shutdown.
void audio_manager_suspend(void);

// Play an MP3 file from the VFS (e.g. /spiffs/notification.mp3).  Blocks
// until complete.  Dispatches decode to a SPIRAM task internally — callers
// with small stacks (UI task, alarm task) are safe.  Returns ESP_OK on
// success, ESP_FAIL if the file is not found or cannot be decoded.
esp_err_t audio_manager_play_mp3(const char *path, am_client_t client);

// Play an MP3 file in a continuous loop, blocking until *stop is set true.
// Checks *stop between decoded frames (~26 ms at 44.1 kHz) so stop latency
// is low.  File is reopened at each loop boundary.  Caller must run this
// from a dedicated task (e.g. audio_alert's alarm loop task).
esp_err_t audio_manager_play_mp3_looped(const char *path, am_client_t client,
                                         volatile bool *stop);

// Register pause/resume hooks.  Call from music_player after its engine
// starts.  Both may be NULL (no auto-pause behaviour until registered).
void audio_manager_register_music_hooks(am_pause_fn_t pause_fn,
                                        am_resume_fn_t resume_fn);

#ifdef __cplusplus
}
#endif
