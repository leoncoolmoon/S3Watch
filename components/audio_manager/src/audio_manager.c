#include "audio_manager.h"
#include "bsp/esp32_s3_touch_amoled_2_06.h"
#include "power_manager.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "minimp3_ex.h"
#include <stdio.h>

static const char *TAG = "AUDIO_MGR";

static esp_codec_dev_handle_t      s_spk       = NULL;
static bool                        s_open      = false;
static am_client_t                 s_holder    = AM_CLIENT_MUSIC;
static esp_codec_dev_sample_info_t s_fmt       = {0};
static int                         s_volume    = 60;
static SemaphoreHandle_t           s_mutex     = NULL;
static am_pause_fn_t               s_pause_fn  = NULL;
static am_resume_fn_t              s_resume_fn = NULL;

static void am_pm_cb(pm_event_t evt, void *ctx)
{
    (void)ctx;
    if (evt == PM_EVT_PREPARE_SLEEP) audio_manager_suspend();
}

esp_err_t audio_manager_init(void)
{
    if (s_spk) return ESP_OK;
    s_spk = bsp_audio_codec_speaker_init();
    if (!s_spk) {
        ESP_LOGE(TAG, "speaker init failed");
        return ESP_FAIL;
    }
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;
    power_manager_add_listener(am_pm_cb, NULL);
    return ESP_OK;
}

esp_err_t audio_manager_open(am_client_t client,
                              uint32_t hz, uint8_t bits, uint8_t ch)
{
    if (!s_spk) return ESP_ERR_INVALID_STATE;

    // NOTIFY preempts MUSIC: pause music first, outside the mutex, because
    // pause_fn blocks until music_player_task closes the codec (which itself
    // calls audio_manager_close → takes the mutex). Releasing here prevents
    // a deadlock between the two callers.
    if (client == AM_CLIENT_NOTIFY) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        bool need_pause = s_open && s_holder == AM_CLIENT_MUSIC;
        xSemaphoreGive(s_mutex);
        if (need_pause && s_pause_fn) s_pause_fn();
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    // MUSIC must not open while NOTIFY holds.
    if (client == AM_CLIENT_MUSIC && s_open && s_holder == AM_CLIENT_NOTIFY) {
        xSemaphoreGive(s_mutex);
        ESP_LOGW(TAG, "MUSIC tried to open while NOTIFY holds — ignoring");
        return ESP_ERR_INVALID_STATE;
    }

    // No-op when already open with identical format for the same client.
    if (s_open && s_holder == client
            && s_fmt.sample_rate     == (int)hz
            && s_fmt.bits_per_sample == (int)bits
            && s_fmt.channel         == (int)ch) {
        xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    // Close existing session before reopening with a different format.
    if (s_open) {
        esp_codec_dev_set_out_mute(s_spk, true);
        esp_codec_dev_close(s_spk);
        s_open = false;
        power_manager_no_sleep_release();
        power_manager_rail_release(PM_RAIL_CLIENT_AUDIO);
    }

    esp_codec_dev_sample_info_t fs = {
        .sample_rate     = (int)hz,
        .channel         = (int)ch,
        .bits_per_sample = (int)bits,
    };
    if (esp_codec_dev_open(s_spk, &fs) != ESP_OK) {
        xSemaphoreGive(s_mutex);
        ESP_LOGE(TAG, "codec open failed (%u Hz %u-bit %u-ch)",
                 (unsigned)hz, (unsigned)bits, (unsigned)ch);
        return ESP_FAIL;
    }

    s_fmt    = fs;
    s_open   = true;
    s_holder = client;
    power_manager_no_sleep_hold();
    power_manager_rail_hold(PM_RAIL_CLIENT_AUDIO);
    esp_codec_dev_set_out_vol(s_spk, s_volume);
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_codec_dev_set_out_mute(s_spk, false);

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

void audio_manager_close(am_client_t client)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (!s_open || s_holder != client) {
        xSemaphoreGive(s_mutex);
        return;
    }
    esp_codec_dev_set_out_mute(s_spk, true);
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_codec_dev_close(s_spk);
    s_open = false;
    power_manager_no_sleep_release();
    power_manager_rail_release(PM_RAIL_CLIENT_AUDIO);
    bool was_notify = (client == AM_CLIENT_NOTIFY);
    xSemaphoreGive(s_mutex);

    // After NOTIFY releases, auto-resume music (async — just posts a command).
    if (was_notify && s_resume_fn) s_resume_fn();
}

int audio_manager_write(const void *data, int len)
{
    if (!s_open || !s_spk) return 0;
    return esp_codec_dev_write(s_spk, (void *)data, len);
}

void audio_manager_set_volume(int vol)
{
    if (vol < 0)   vol = 0;
    if (vol > 100) vol = 100;
    s_volume = vol;
    if (s_spk) esp_codec_dev_set_out_vol(s_spk, vol);
}

void audio_manager_mute(bool mute)
{
    if (s_spk) esp_codec_dev_set_out_mute(s_spk, mute);
}

bool audio_manager_is_open(void)
{
    return s_open;
}

void audio_manager_suspend(void)
{
    // MUSIC holds the no-sleep PM lock and ALDO3 rail via audio_manager_open —
    // the CPU stays awake at DFS min freq while the AMOLED DCS-sleeps. Leave
    // music running so it plays through display sleep.
    // Only close a one-shot NOTIFY stream that's mid-play. Do NOT call
    // audio_manager_close(NOTIFY) here — that triggers the music auto-resume hook.
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (!s_open || s_holder != AM_CLIENT_NOTIFY) {
        xSemaphoreGive(s_mutex);
        return;
    }
    esp_codec_dev_set_out_mute(s_spk, true);
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_codec_dev_close(s_spk);
    s_open = false;
    power_manager_no_sleep_release();
    power_manager_rail_release(PM_RAIL_CLIENT_AUDIO);
    xSemaphoreGive(s_mutex);
}

// ── MP3 file playback ──────────────────────────────────────────────────────

typedef struct {
    const char       *path;
    am_client_t       client;
    SemaphoreHandle_t done;
    esp_err_t         result;
} am_play_args_t;

static size_t am_mp3_read(void *buf, size_t size, void *user_data)
{
    size_t done = 0;
    while (done < size) {
        size_t n = fread((uint8_t *)buf + done, 1, size - done, (FILE *)user_data);
        if (n == 0) break;
        done += n;
    }
    return done;
}

static int am_mp3_seek(uint64_t pos, void *user_data)
{
    return fseek((FILE *)user_data, (long)pos, SEEK_SET);
}

static void am_mp3_task(void *pv)
{
    am_play_args_t *a = (am_play_args_t *)pv;
    esp_err_t result = ESP_FAIL;

    FILE *f = fopen(a->path, "rb");
    if (!f) goto done;

    mp3dec_ex_t *dec = heap_caps_malloc(sizeof(mp3dec_ex_t), MALLOC_CAP_SPIRAM);
    if (!dec) { fclose(f); goto done; }

    mp3dec_io_t io = { am_mp3_read, f, am_mp3_seek, f };
    if (mp3dec_ex_open_cb(dec, &io, MP3D_DO_NOT_SCAN) != 0) {
        free(dec); fclose(f); goto done;
    }

    int hz = dec->info.hz       > 0 ? dec->info.hz       : 44100;
    int ch = dec->info.channels > 0 ? dec->info.channels : 1;

    audio_manager_set_volume(s_volume);
    if (audio_manager_open(a->client, (uint32_t)hz, 16, (uint8_t)ch) == ESP_OK) {
        int16_t pcm_buf[MINIMP3_MAX_SAMPLES_PER_FRAME];
        size_t n;
        while ((n = mp3dec_ex_read(dec, pcm_buf, MINIMP3_MAX_SAMPLES_PER_FRAME)) > 0)
            audio_manager_write(pcm_buf, (int)(n * sizeof(int16_t)));
        audio_manager_close(a->client);
        result = ESP_OK;
    }

    mp3dec_ex_close(dec);
    free(dec);
    fclose(f);

done:
    a->result = result;
    xSemaphoreGive(a->done);
    vTaskDeleteWithCaps(NULL);
}

esp_err_t audio_manager_play_mp3(const char *path, am_client_t client)
{
    am_play_args_t args = { path, client, xSemaphoreCreateBinary(), ESP_FAIL };
    if (!args.done) return ESP_ERR_NO_MEM;
    if (xTaskCreateWithCaps(am_mp3_task, "am_mp3", 32 * 1024, &args,
                            tskIDLE_PRIORITY + 4, NULL,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        vSemaphoreDelete(args.done);
        return ESP_FAIL;
    }
    xSemaphoreTake(args.done, portMAX_DELAY);
    vSemaphoreDelete(args.done);
    return args.result;
}

// ── music pause/resume hooks ───────────────────────────────────────────────

void audio_manager_register_music_hooks(am_pause_fn_t pause_fn,
                                        am_resume_fn_t resume_fn)
{
    s_pause_fn  = pause_fn;
    s_resume_fn = resume_fn;
}
