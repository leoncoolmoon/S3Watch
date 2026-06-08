#pragma once
#include "esp_err.h"
#include "esp_codec_dev.h"
#ifdef __cplusplus
extern "C" {
#endif

esp_err_t audio_alert_init(void);
// Returns the shared speaker codec-device handle (lazily created by
// audio_alert_init() on first use), for subsystems that need to drive
// playback directly — e.g. music_player streaming decoded PCM. There is
// exactly one es8311/I2S codec on this board, and bsp_audio_codec_speaker_init()
// is NOT idempotent (it allocates a fresh esp_codec_dev_handle_t wrapping the
// same hardware on every call), so a second independent handle would race
// this one over open/close/mute/volume state. Returns NULL on init failure.
//
// Known seam: callers that hold the device for long, continuous streams
// (music_player) and short one-off sounds (audio_alert_notify/play_startup)
// are not arbitrated against each other — both assume exclusive use. If a
// notification ever needs to interrupt music playback, that coordination
// will need to be added then; it isn't attempted here.
esp_codec_dev_handle_t audio_alert_acquire_speaker(void);
void audio_alert_notify(void);
// Schedule a one-shot startup tone after boot, with a short delay
// to allow the codec/PA to settle and avoid first-play clicks.
void audio_alert_play_startup(void);
// Mute and stop the codec/I2S before display power rails are cut.
// Safe to call when audio was never played.
void audio_alert_suspend(void);

#ifdef __cplusplus
}
#endif
