#pragma once
#include "esp_err.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

esp_err_t audio_alert_init(void);
void audio_alert_notify(void);
void audio_alert_play_startup(void);
void audio_alert_suspend(void);

// Alarm sounds available in /spiffs/alarms/.
#define ALARM_SOUND_COUNT 3
extern const char * const alarm_sound_names[ALARM_SOUND_COUNT]; // "Alarm", "Bird Song", "Retro Digital"

// Start looping the alarm sound for the given settings index (0-2).
// Non-blocking: spawns a task. Safe to call from any context.
void audio_alert_alarm_start(uint8_t idx);

// Signal the looping alarm task to stop. Returns immediately;
// audio stops within one decoded frame (~26 ms).
void audio_alert_alarm_stop(void);

#ifdef __cplusplus
}
#endif
