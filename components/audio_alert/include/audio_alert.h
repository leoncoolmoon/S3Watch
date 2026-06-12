#pragma once
#include "esp_err.h"
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#ifdef __cplusplus
extern "C" {
#endif

esp_err_t audio_alert_init(void);
void audio_alert_notify(void);

// Play the boot tone (boot.mp3, synth fallback) on a one-shot task. `done`
// (optional, may be NULL) is given when playback has fully finished — given
// immediately if sound is disabled or the task can't spawn, so a waiter is
// never left hanging. boot_manager blocks the splash→watchface handoff on it.
void audio_alert_play_startup(SemaphoreHandle_t done);
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
