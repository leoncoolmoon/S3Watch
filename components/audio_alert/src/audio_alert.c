#include "audio_alert.h"
#include "audio_manager.h"
#include <math.h>
#include "esp_log.h"
#include "settings.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "AUDIO_ALERT";

// ---------------------------------------------------------------------------
// Alarm sound table
// ---------------------------------------------------------------------------

static const char * const alarm_paths[ALARM_SOUND_COUNT] = {
    "/spiffs/alarms/alarm.mp3",
    "/spiffs/alarms/bird_song.mp3",
    "/spiffs/alarms/retro_digital.mp3",
};
const char * const alarm_sound_names[ALARM_SOUND_COUNT] = {
    "Alarm",
    "Bird Song",
    "Retro Digital",
};

static volatile bool s_alarm_stop = false;

static void alarm_loop_task(void *pv)
{
    const char *path = (const char *)pv;
    audio_manager_set_volume((int)settings_get_notify_volume());
    audio_manager_play_mp3_looped(path, AM_CLIENT_NOTIFY, &s_alarm_stop);
    vTaskDelete(NULL);
}

void audio_alert_alarm_start(uint8_t idx)
{
    if (!settings_get_sound()) return;
    if (idx >= ALARM_SOUND_COUNT) idx = 0;
    audio_alert_alarm_stop();   // cancel any previous alarm still fading out
    s_alarm_stop = false;
    xTaskCreate(alarm_loop_task, "alarm_loop", 32 * 1024,
                (void *)alarm_paths[idx], 4, NULL);
}

void audio_alert_alarm_stop(void)
{
    s_alarm_stop = true;
}

esp_err_t audio_alert_init(void)
{
    return ESP_OK; // hardware owned by audio_manager
}

static void play_pcm_16_mono_22k(const int16_t* pcm, size_t samples)
{
    int vol = (int)settings_get_notify_volume();
    audio_manager_set_volume(vol);
    if (audio_manager_open(AM_CLIENT_NOTIFY, 22050, 16, 1) != ESP_OK) return;

    // Silence pad to avoid initial click/pop
    enum { ZERO_PAD_SAMP = 1024 }; // ~46ms at 22.05 kHz
    int16_t zero_pad[ZERO_PAD_SAMP] = {0};
    audio_manager_write((void*)zero_pad, sizeof(zero_pad));

    const size_t chunk_samp = 256;
    size_t written = 0;
    while (written < samples) {
        size_t n = samples - written;
        if (n > chunk_samp) n = chunk_samp;
        audio_manager_write((void*)(pcm + written), (int)(n * sizeof(int16_t)));
        written += n;
    }

    // Silence tail for clean ramp-down
    audio_manager_write((void*)zero_pad, sizeof(zero_pad));

    // Wait for audio to drain before closing
    uint32_t total_samples = samples + (2 * ZERO_PAD_SAMP);
    uint32_t ms = (uint32_t)((total_samples * 1000UL) / 22050);
    vTaskDelay(pdMS_TO_TICKS(ms + 10));

    audio_manager_close(AM_CLIENT_NOTIFY);
}


void audio_alert_notify(void)
{
    if (!settings_get_sound()) return;
    audio_manager_set_volume((int)settings_get_notify_volume());
    if (audio_manager_play_mp3("/spiffs/notification.mp3",
                               AM_CLIENT_NOTIFY) == ESP_OK) return;

    ESP_LOGI(TAG, "notification.mp3 not found, using synthesized tone");
    enum { SR = 22050 };
    const float base_f = 440.0f;
    const float dur_s  = 0.32f;
    size_t N = (size_t)(SR * dur_s);
    const size_t max_samples = 8192;
    if (N > max_samples) N = max_samples;
    int16_t *buf = (int16_t *)heap_caps_malloc(max_samples * sizeof(int16_t),
                                               MALLOC_CAP_SPIRAM);
    if (!buf) {
        ESP_LOGE(TAG, "synth buf alloc failed");
        return;
    }

    const float attack_s = 0.004f;
    const size_t attack_n = (size_t)(attack_s * SR);
    const float a0 = 1.00f, d0 = 5.0f;
    const float a1 = 0.45f, d1 = 8.0f;
    const float a2 = 0.25f, d2 = 12.0f;
    const float a3 = 0.20f, d3 = 10.0f;

    for (size_t n = 0; n < N; ++n) {
        float t = (float)n / (float)SR;
        float glide = 1.0f + 0.06f * expf(-40.0f * t);
        float f0 = base_f * glide;
        float f1 = (base_f * 1.50f) * glide;
        float f2 = (base_f * 2.00f) * glide;
        float f3 = (base_f * 1.96f) * glide;
        float e0 = expf(-d0 * t);
        float e1 = expf(-d1 * t);
        float e2 = expf(-d2 * t);
        float e3 = expf(-d3 * t);
        float s = 0.0f;
        s += a0 * e0 * sinf(2.0f * (float)M_PI * f0 * t);
        s += a1 * e1 * sinf(2.0f * (float)M_PI * f1 * t);
        s += a2 * e2 * sinf(2.0f * (float)M_PI * f2 * t);
        s += a3 * e3 * sinf(2.0f * (float)M_PI * f3 * t);
        float fade_in = (n < attack_n) ? (float)n / (float)attack_n : 1.0f;
        s *= fade_in;
        int v = (int)(s * 20000.0f);
        if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
        buf[n] = (int16_t)v;
    }
    play_pcm_16_mono_22k(buf, N);
    heap_caps_free(buf);
}

static void audio_startup_tone_task(void *pv)
{
    (void)pv;
    vTaskDelay(pdMS_TO_TICKS(400));
    audio_manager_set_volume((int)settings_get_notify_volume());
    if (audio_manager_play_mp3("/spiffs/boot.mp3", AM_CLIENT_NOTIFY) != ESP_OK) {
        ESP_LOGI(TAG, "boot.mp3 not found, using synthesized tone");
        audio_alert_notify();
    }
    vTaskDelete(NULL);
}

void audio_alert_play_startup(void)
{
    if (!settings_get_sound()) return;
    xTaskCreate(audio_startup_tone_task, "tone_startup", 8192, NULL, 3, NULL);
}

void audio_alert_suspend(void)
{
    audio_manager_suspend();
}
