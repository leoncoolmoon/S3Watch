// MP3 playback engine + catalog accessor passthroughs.
//
// The catalog (g_music_artists/albums/tracks, built by music_catalog_load())
// is read-only after construction, so the public music_catalog_* accessors
// below need no locking — they're called from the LVGL/UI task while
// browsing.
//
// Playback is owned entirely by a dedicated background task (started lazily
// on first transport call, then persistent for the boot session — this is
// what makes "keep playing while you navigate elsewhere" work). All
// interaction with it goes through a small command queue; the only state
// shared back out is the "now playing" snapshot (s_now) plus s_shuffle,
// guarded by a portMUX_TYPE exactly like signalk_client's s_mux / ntp_sync's
// s_sync_mux — small POD, cheap critical sections, no blocking inside them.

#include "music_player.h"
#include "music_catalog.h"

#include "bsp/esp32_s3_touch_amoled_2_06.h"
#include "audio_alert.h"
#include "settings.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_pm.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/idf_additions.h"

#include "minimp3_ex.h"

#include <string.h>
#include <errno.h>
#include <stdio.h>

static const char *TAG = "MUSIC_PLAYER";

// ── catalog: lazy load + read-only accessor passthroughs ────────────────

static bool s_sd_mounted = false;

// Mounts the card if nothing else has (mirrors settings.c's sd_acquire guard
// against double-mounting sd_logger's session-long mount), then leaves it
// mounted for the rest of the boot session — same persistent-mount approach
// as sd_logger, since playback needs the card available indefinitely anyway.
static bool ensure_sd_mounted(void)
{
    if (s_sd_mounted) return true;
    if (bsp_sdcard == NULL && bsp_sdcard_mount() != ESP_OK) {
        ESP_LOGW(TAG, "SD card mount failed");
        return false;
    }
    s_sd_mounted = true;
    return true;
}

esp_err_t music_player_load_catalog(void)
{
    if (!ensure_sd_mounted()) return ESP_FAIL;
    return music_catalog_load(BSP_SD_MOUNT_POINT);
}

bool music_player_catalog_ready(void)
{
    return g_music_track_count > 0;
}

uint32_t music_catalog_artist_count(void)
{
    return g_music_artist_count;
}

const char *music_catalog_artist_name(uint32_t artist_index)
{
    return artist_index < g_music_artist_count ? g_music_artists[artist_index].name : "";
}

uint32_t music_catalog_album_count(uint32_t artist_index)
{
    return artist_index < g_music_artist_count ? g_music_artists[artist_index].album_count : 0;
}

const char *music_catalog_album_name(uint32_t artist_index, uint32_t album_index)
{
    if (artist_index >= g_music_artist_count) return "";
    const music_artist_t *ar = &g_music_artists[artist_index];
    return album_index < ar->album_count ? g_music_albums[ar->album_start + album_index].name : "";
}

uint32_t music_catalog_track_count(uint32_t artist_index, uint32_t album_index)
{
    if (artist_index >= g_music_artist_count) return 0;
    const music_artist_t *ar = &g_music_artists[artist_index];
    if (album_index >= ar->album_count) return 0;
    return g_music_albums[ar->album_start + album_index].track_count;
}

const char *music_catalog_track_title(uint32_t artist_index, uint32_t album_index, uint32_t track_index)
{
    uint32_t flat = music_catalog_track_flat_index(artist_index, album_index, track_index);
    return flat < g_music_track_count ? g_music_tracks[flat].title : "";
}

// Returns g_music_track_count (one-past-the-end / always invalid) for any
// out-of-range hierarchy position — callers compare against that, mirroring
// how the rest of this file treats "< g_music_track_count" as the validity test.
uint32_t music_catalog_track_flat_index(uint32_t artist_index, uint32_t album_index, uint32_t track_index)
{
    if (artist_index >= g_music_artist_count) return g_music_track_count;
    const music_artist_t *ar = &g_music_artists[artist_index];
    if (album_index >= ar->album_count) return g_music_track_count;
    const music_album_t *al = &g_music_albums[ar->album_start + album_index];
    if (track_index >= al->track_count) return g_music_track_count;
    return al->track_start + track_index;
}

uint32_t music_catalog_total_track_count(void)
{
    return g_music_track_count;
}

// ── shared "now playing" snapshot + shuffle flag ─────────────────────────

static portMUX_TYPE        s_mux = portMUX_INITIALIZER_UNLOCKED;
static music_now_playing_t s_now = {0};   // zero-initialised: title/artist/album start "" (see strncpy note below)
static bool                s_shuffle = false;

void music_player_get_now_playing(music_now_playing_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_now;
    portEXIT_CRITICAL(&s_mux);
}

bool music_player_get_shuffle(void)
{
    bool v;
    portENTER_CRITICAL(&s_mux);
    v = s_shuffle;
    portEXIT_CRITICAL(&s_mux);
    return v;
}

void music_player_set_shuffle(bool on)
{
    portENTER_CRITICAL(&s_mux);
    s_shuffle = on;
    portEXIT_CRITICAL(&s_mux);
}

// strncpy(dst, src, sizeof(dst)-1) leaves dst[sizeof(dst)-1] untouched; since
// s_now is a zero-initialised static, that byte is permanently '\0' and every
// copy is therefore null-terminated — same convention as setting_ondev_test_screen.c.
static void set_now_playing_track(uint32_t flat_index)
{
    const music_track_t  *tr = &g_music_tracks[flat_index];
    const music_album_t  *al = &g_music_albums[tr->album_index];
    const music_artist_t *ar = &g_music_artists[tr->artist_index];

    portENTER_CRITICAL(&s_mux);
    s_now.active           = true;
    s_now.flat_track_index = flat_index;
    s_now.position_s       = 0;
    s_now.duration_s       = tr->duration_s;
    strncpy(s_now.title,  tr->title, sizeof(s_now.title)  - 1);
    strncpy(s_now.artist, ar->name,  sizeof(s_now.artist) - 1);
    strncpy(s_now.album,  al->name,  sizeof(s_now.album)  - 1);
    portEXIT_CRITICAL(&s_mux);
}

static void set_playing(bool playing)
{
    portENTER_CRITICAL(&s_mux);
    s_now.playing = playing;
    portEXIT_CRITICAL(&s_mux);
}

static void set_position(uint32_t position_s)
{
    portENTER_CRITICAL(&s_mux);
    s_now.position_s = position_s;
    portEXIT_CRITICAL(&s_mux);
}

// ── playback engine: dedicated task, command queue, decoder ─────────────
//
// task_coordinator (components/task_coordinator) is explicitly for short,
// non-blocking, boot-registered callbacks sharing one task — wrong model for
// something that blocks on SD reads and must continuously feed DMA. This
// mirrors sd_logger's sd_log_writer_task / wifi_manager's wifi_release_task:
// a dedicated long-lived task, created lazily on first use.

typedef enum {
    CMD_PLAY,
    CMD_TOGGLE_PLAY_PAUSE,
    CMD_NEXT,
    CMD_PREV,
} player_cmd_type_t;

typedef struct {
    player_cmd_type_t type;
    uint32_t          arg; // CMD_PLAY: flat track index
} player_cmd_t;

#define CMD_QUEUE_LEN 4

// One MP3 frame's worth of interleaved samples per mp3dec_ex_read() call —
// ~26ms at 44.1kHz/stereo, small enough that pause/skip commands (polled
// between chunks) land within a frame or two.
#define PCM_CHUNK_SAMPLES MINIMP3_MAX_SAMPLES_PER_FRAME

// mp3dec_ex_t embeds mp3dec_t, whose mdct_overlap[2][9*32] + qmf_state[15*2*32]
// arrays alone are several KB — exactly the class of "large array in
// long-lived per-stream state" that must be heap/static, never stack-local in
// a decode loop (this is the lesson from the wifi_rel stack-overflow bootloop
// documented at the top of sd_logger.c). Both it and the PCM chunk buffer are
// static module state, owned solely by music_player_task.
static mp3dec_ex_t s_dec;
static bool        s_dec_open = false;
static int16_t    *s_pcm_buf  = NULL;

// Streaming I/O backing for s_dec (see open_track): minimp3 reads/seeks
// through these instead of loading the whole file into one malloc(), since
// tracks here run into the 10+MB range — far past what a single PSRAM
// allocation can serve. mp3dec_ex_t stores a *pointer* to s_io, so it must
// outlive the decoder (hence static, not stack-local).
static FILE        *s_file = NULL;
static mp3dec_io_t  s_io;

static esp_codec_dev_handle_t      s_spk = NULL;
static bool                        s_codec_open = false;
static esp_codec_dev_sample_info_t s_codec_fmt  = {0};

// display_manager holds its own ESP_PM_NO_LIGHT_SLEEP lock, but only while the
// screen is on — it's released the moment the panel sleeps (see "PM lock
// released — CPU may enter light sleep" in display_turn_off_internal()), which
// is exactly when this app's "sounds great with the display on, staticy once
// it sleeps" symptom appears. esp_codec_dev/I2S separately holds an
// APB_FREQ_MAX lock while its channel is enabled (keeps the APB clock pinned
// so DFS can't shift it underneath the I2S clock divider — see the comment by
// audio_alert.c's esp_codec_dev_close() call), but that's a different lock
// type: it doesn't stop the CPU from entering automatic light sleep, which is
// what was glitching the I2S clock and coming out as static (not dropouts —
// the DMA itself stayed fed; see music_feed_task). So: our own lock, held
// for exactly as long as the codec is physically open, regardless of what the
// display is doing.
#if CONFIG_PM_ENABLE
static esp_pm_lock_handle_t s_no_ls_lock = NULL;
#endif

static void no_light_sleep_acquire(void)
{
#if CONFIG_PM_ENABLE
    if (s_no_ls_lock) (void)esp_pm_lock_acquire(s_no_ls_lock);
#endif
}

static void no_light_sleep_release(void)
{
#if CONFIG_PM_ENABLE
    if (s_no_ls_lock) (void)esp_pm_lock_release(s_no_ls_lock);
#endif
}

static QueueHandle_t s_cmd_queue = NULL;
static TaskHandle_t  s_task      = NULL;

// minimp3 itself is fast (decodes many times faster than real-time even on
// far weaker CPUs), but on top of mp3dec_ex_t this task's call chain runs
// minimp3's own mp3dec_decode_frame(), which keeps a ~16KB mp3dec_scratch_t
// (grbuf/syn/maindata/...) on ITS stack — by far the largest stack frame
// anywhere in this codebase (sd_log_writer_task=3072, wifi_release_task=4096).
// Sizing that out of internal SRAM would be wasteful of a resource the rest
// of the system needs (LVGL, WiFi, ...); PSRAM's extra access latency is
// immaterial against the ~26ms-per-frame real-time budget. Verify headroom
// on-device with uxTaskGetStackHighWaterMark(s_task).
#define MUSIC_PLAYER_TASK_STACK (40 * 1024)
// Runs above the LVGL port task (priority 3 — see bsp_display_lvgl_init /
// cfg_port.task_priority in the BSP) so command handling / decoding stays
// responsive and doesn't fall behind for long, but it no longer has to win
// every scheduling race against the UI: see MUSIC_PCM_RING_BYTES below for
// why that's now music_feed_task's job, not this one's.
#define MUSIC_PLAYER_TASK_PRIO  (tskIDLE_PRIORITY + 4)

// ── PCM ring buffer + dedicated feed task ───────────────────────────────
//
// First cut wrote decoded PCM straight to esp_codec_dev_write() from this
// (decode) task — i.e. the thing doing SD reads + minimp3 decoding was also
// the only thing keeping the I2S DMA fed. That DMA is sized at the driver
// default (dma_desc_num=6 * dma_frame_num=240 frames, ~33ms of audio at
// 44.1kHz — esp_codec_dev_write() -> i2s_channel_write() only blocks until
// there's room in it, it doesn't pre-buffer further). Putting this task at a
// higher priority than LVGL (the earlier fix) helped, but 33ms is so tight
// that *any* stall on the producer side — an SD refill (mp3dec_ex_read_frame
// pulling another MINIMP3_IO_SIZE chunk through io_read), a slow decode on a
// PSRAM-resident mp3dec_scratch_t, even just losing a scheduling race to
// something else briefly — still ran the buffer dry mid-track: the reported
// "skips and static".
//
// Standard fix: decouple the two with a ring buffer. music_player_task keeps
// decoding and pushes raw PCM bytes into s_pcm_stream — it can run a bit
// behind real-time momentarily without anyone hearing it, because...
#define MUSIC_PCM_RING_BYTES (128 * 1024) // ~0.74s @ 44.1kHz/stereo/16-bit — ~22x the I2S DMA's ~33ms

// ...music_feed_task (below) does nothing BUT drain that ring buffer straight
// into esp_codec_dev_write(). With no SD I/O, no decoding, no command
// handling — just a receive + a write — it is by far the least likely thing
// in this firmware to ever miss the DMA's 33ms deadline, so it gets the
// highest of the two priorities and is the one that "must always preempt the
// UI to keep the tiny hardware buffer fed."
#define MUSIC_FEED_CHUNK_BYTES (PCM_CHUNK_SAMPLES * sizeof(int16_t))
#define MUSIC_FEED_TASK_STACK  4096
#define MUSIC_FEED_TASK_PRIO   (tskIDLE_PRIORITY + 5)

static StreamBufferHandle_t s_pcm_stream = NULL; // raw interleaved PCM bytes, decoder -> feeder
static int16_t             *s_feed_buf   = NULL; // music_feed_task's receive scratch (PSRAM, MUSIC_FEED_CHUNK_BYTES)
static TaskHandle_t         s_feed_task  = NULL;

// Track-change handshake: the ring buffer can hold up to ~0.7s of audio, so
// switching tracks (or pausing) without draining it would tail off into a
// moment of whatever was already queued. xStreamBufferReset() can't safely be
// called from here — it fails (by design) whenever a task is blocked on the
// buffer's send/receive, which music_feed_task usually is. So instead: ask
// the feeder (the buffer's only consumer, hence the only side that can drain
// it without racing itself) to do it, and block on s_flush_done until it has.
static volatile bool     s_flush_req  = false;
static SemaphoreHandle_t s_flush_done = NULL;

static bool open_codec(int hz, int channels)
{
    if (channels != 1 && channels != 2) channels = 2;
    if (s_codec_open && s_codec_fmt.sample_rate == hz && s_codec_fmt.channel == channels) {
        return true; // already open with a matching format — common case (most tracks share a rate)
    }
    if (s_codec_open) {
        esp_codec_dev_set_out_mute(s_spk, true);
        esp_codec_dev_close(s_spk);
        s_codec_open = false;
        no_light_sleep_release();
    }
    // Pass the MP3's actual detected rate straight through — play_pcm_stream_i16
    // (audio_alert.c) already does this generically for arbitrary rates, so the
    // data interface is known to reconfigure clocks per esp_codec_dev_open().
    esp_codec_dev_sample_info_t fs = { .sample_rate = hz, .channel = channels, .bits_per_sample = 16 };
    if (esp_codec_dev_open(s_spk, &fs) != ESP_OK) {
        ESP_LOGW(TAG, "codec open failed (%d Hz, %d ch)", hz, channels);
        return false;
    }
    s_codec_fmt  = fs;
    s_codec_open = true;
    // Held for exactly as long as the I2S channel is enabled — see s_no_ls_lock.
    no_light_sleep_acquire();

    int vol = (int)settings_get_notify_volume();
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    esp_codec_dev_set_out_vol(s_spk, vol);
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_codec_dev_set_out_mute(s_spk, false);
    return true;
}

static void close_codec(void)
{
    if (!s_codec_open) return;
    // Mirrors audio_alert_suspend()'s mute -> settle -> close sequence: give
    // the mute time to actually take hold through the codec before esp_codec_
    // dev_close() cuts the PA rail (es8311_pa_power -> BSP_POWER_AMP_IO),
    // otherwise the power-down lands mid-signal and comes out as a squeal.
    esp_codec_dev_set_out_mute(s_spk, true);
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_codec_dev_close(s_spk);
    s_codec_open = false;
    no_light_sleep_release();
}

// The sole consumer of s_pcm_stream and the only thing that calls
// esp_codec_dev_write() — deliberately tiny (receive + write, nothing that
// touches SD or minimp3) so it's the least likely task in the firmware to
// ever blow the I2S DMA's ~33ms deadline. See MUSIC_PCM_RING_BYTES for the
// full story; this is the half of that split that has to win every race.
static void music_feed_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (s_flush_req) {
            // We're the buffer's only reader, so we're the only side that can
            // drain it without a second receiver racing us for the same bytes.
            uint8_t scratch[256];
            while (xStreamBufferReceive(s_pcm_stream, scratch, sizeof(scratch), 0) > 0) { }
            s_flush_req = false;
            xSemaphoreGive(s_flush_done);
            continue;
        }
        // Bounded, not portMAX_DELAY: idle/paused stretches send nothing, and
        // this loop must keep coming back around to notice s_flush_req.
        size_t n = xStreamBufferReceive(s_pcm_stream, s_feed_buf, MUSIC_FEED_CHUNK_BYTES, pdMS_TO_TICKS(50));
        if (n > 0) {
            esp_codec_dev_write(s_spk, s_feed_buf, (int)n);
        }
    }
}

// Drains s_pcm_stream before a track switch / pause / stop so playback cuts
// over immediately rather than tailing off into ~0.7s of whatever was already
// queued. xStreamBufferReset() isn't usable here — see s_flush_req's comment —
// so this hands the job to music_feed_task and blocks until it confirms via
// s_flush_done. Always called from music_player_task, never the feed task
// itself, so there's no risk of waiting on a semaphore only it can give.
static void flush_pcm_stream(void)
{
    if (!s_pcm_stream || !s_flush_done) return;
    s_flush_req = true;
    xSemaphoreTake(s_flush_done, portMAX_DELAY);
}

// mp3dec_ex_open()'s default path (fopen + malloc(filesize) + fread) loads
// the entire file into one allocation — these tracks run 10-14MB, far past
// what a single PSRAM allocation can serve (confirmed on-device: every track
// failed with mp3dec rc=-2 / MP3D_E_MEMORY). These callbacks let minimp3
// stream through mp3dec_ex_open_cb() instead, which only needs a small
// MINIMP3_IO_SIZE (128KB) working buffer.
// minimp3's refill logic (mp3dec_ex_read_frame) treats any io->read() that
// returns less than the requested size as EOF — it then stops refilling for
// the rest of the stream. Plain fread() is allowed to return short counts
// without that meaning EOF (e.g. one VFS/FATFS cluster's worth per call), so
// a single fread() here was tripping minimp3's EOF path after just the first
// ~chunk and looping that same buffered audio forever (the "loops the first
// couple seconds / one buffer full" symptom). Loop until the buffer is
// completely full or fread() genuinely reports EOF/error (returns 0).
static size_t io_read(void *buf, size_t size, void *user_data)
{
    FILE  *f     = (FILE *)user_data;
    uint8_t *p   = (uint8_t *)buf;
    size_t total = 0;
    while (total < size) {
        size_t n = fread(p + total, 1, size - total, f);
        if (n == 0) break;
        total += n;
    }
    return total;
}

static int io_seek(uint64_t position, void *user_data)
{
    return fseek((FILE *)user_data, (long)position, SEEK_SET);
}

// Closes whatever is currently open (decoder + backing file) — shared by the
// "switch tracks" and "give up on this track" paths in open_track().
static void close_track(void)
{
    if (s_dec_open) {
        mp3dec_ex_close(&s_dec);
        s_dec_open = false;
    }
    if (s_file) {
        fclose(s_file);
        s_file = NULL;
    }
}

// Opens the file, lets minimp3 detect its format from the first frame (it
// always populates dec->info before honouring MP3D_DO_NOT_SCAN — see
// mp3dec_load_index() in minimp3_ex.h), opens/reconfigures the codec for that
// format, and publishes the new now-playing snapshot. On any failure the
// decoder/codec are left closed and the caller should try the next track.
static bool open_track(uint32_t flat_index)
{
    if (flat_index >= g_music_track_count) return false;
    const char *path = g_music_tracks[flat_index].path;
    if (!path) return false;

    close_track();

    s_file = fopen(path, "rb");
    if (!s_file) {
        ESP_LOGW(TAG, "Failed to open %s (errno=%d/%s)", path, errno, strerror(errno));
        return false;
    }

    s_io.read      = io_read;
    s_io.read_data = s_file;
    s_io.seek      = io_seek;
    s_io.seek_data = s_file;

    // MP3D_DO_NOT_SCAN: skip minimp3's whole-file duration scan — slow on SD,
    // and unneeded since INDEX.JSN already gives us duration and seek-by-drag
    // is out of scope.
    int rc = mp3dec_ex_open_cb(&s_dec, &s_io, MP3D_DO_NOT_SCAN);
    if (rc != 0) {
        ESP_LOGW(TAG, "Failed to decode %s (mp3dec rc=%d)", path, rc);
        fclose(s_file);
        s_file = NULL;
        return false;
    }
    s_dec_open = true;

    int hz       = s_dec.info.hz       > 0 ? s_dec.info.hz       : 44100;
    int channels = s_dec.info.channels > 0 ? s_dec.info.channels : 2;
    if (!open_codec(hz, channels)) {
        close_track();
        return false;
    }

    set_now_playing_track(flat_index);
    return true;
}

// Decodes and streams one chunk. Returns false at EOF or on an unrecoverable
// decode error (mp3dec_ex_read returns 0 either way) — the caller advances.
static bool decode_and_play_chunk(void)
{
    size_t n = mp3dec_ex_read(&s_dec, s_pcm_buf, PCM_CHUNK_SAMPLES);
    if (n == 0) return false;

    // Push into the ring buffer rather than writing the codec directly — see
    // MUSIC_PCM_RING_BYTES for why. Bounded wait: long enough to ride out a
    // momentarily-full buffer (music_feed_task draining slower than we're
    // producing — should be rare at ~0.74s of headroom), but short enough that
    // a wedged feeder can't hang the decode loop forever; if it's still full
    // after that, drop the chunk and keep decoding rather than stalling here
    // and risking starving the feeder of upcoming audio entirely.
    size_t bytes = n * sizeof(int16_t);
    size_t sent  = xStreamBufferSend(s_pcm_stream, s_pcm_buf, bytes, pdMS_TO_TICKS(500));
    if (sent < bytes) {
        ESP_LOGW(TAG, "PCM ring buffer full — dropping %u bytes", (unsigned)(bytes - sent));
    }

    int channels = s_dec.info.channels > 0 ? s_dec.info.channels : 1;
    int hz       = s_dec.info.hz       > 0 ? s_dec.info.hz       : 44100;
    set_position((uint32_t)(s_dec.cur_sample / (uint64_t)((uint32_t)channels * (uint32_t)hz)));
    return true;
}

// Sequential by default; a random pick (never repeating the current track)
// when shuffle is on — applies to forward EOF-advance, CMD_NEXT and CMD_PREV
// alike (no play-history is kept, so "shuffle prev" re-rolls rather than
// rewinding — acceptable for the basic shuffle this app targets).
static void advance_track(int direction)
{
    if (g_music_track_count == 0) return;

    bool     shuffle;
    uint32_t cur;
    portENTER_CRITICAL(&s_mux);
    shuffle = s_shuffle;
    cur     = s_now.flat_track_index;
    portEXIT_CRITICAL(&s_mux);

    uint32_t next;
    if (shuffle && g_music_track_count > 1) {
        do {
            next = esp_random() % g_music_track_count;
        } while (next == cur);
    } else if (direction >= 0) {
        next = (cur + 1) % g_music_track_count;
    } else {
        next = (cur == 0) ? g_music_track_count - 1 : cur - 1;
    }

    if (open_track(next)) {
        set_playing(true);
    } else {
        // Couldn't open the next pick either — stop rather than spin through
        // a damaged library forever burning CPU/SD bandwidth.
        flush_pcm_stream();
        close_codec();
        set_playing(false);
    }
}

static void handle_command(const player_cmd_t *cmd)
{
    switch (cmd->type) {
    case CMD_PLAY:
        // Discard whatever's still queued for the old track before swapping —
        // otherwise the listener hears its tail end bleed into the new pick.
        flush_pcm_stream();
        if (open_track(cmd->arg)) set_playing(true);
        break;

    case CMD_TOGGLE_PLAY_PAUSE: {
        bool active, now_playing;
        portENTER_CRITICAL(&s_mux);
        active = s_now.active;
        if (active) s_now.playing = !s_now.playing;
        now_playing = s_now.playing;
        portEXIT_CRITICAL(&s_mux);
        if (active) {
            // Fully open/close the codec across pause/resume rather than just
            // muting — an open-but-muted codec was left driving the I2S/DAC
            // with no fresh PCM behind it, which on this hardware comes out
            // as an audible high-pitched noise. The codec should only be on
            // while audio is actually flowing.
            if (now_playing) {
                int hz       = s_dec.info.hz       > 0 ? s_dec.info.hz       : 44100;
                int channels = s_dec.info.channels > 0 ? s_dec.info.channels : 2;
                open_codec(hz, channels);
            } else {
                // Drop whatever's still queued so resume doesn't open with a
                // burst of stale pre-pause audio before catching up live.
                flush_pcm_stream();
                close_codec();
            }
        }
        break;
    }

    case CMD_NEXT:
        flush_pcm_stream();
        advance_track(+1);
        break;

    case CMD_PREV:
        flush_pcm_stream();
        advance_track(-1);
        break;
    }
}

static void music_player_task(void *arg)
{
    (void)arg;
    s_pcm_buf = (int16_t *)heap_caps_malloc(PCM_CHUNK_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!s_pcm_buf) {
        ESP_LOGE(TAG, "PCM buffer allocation failed — playback task exiting");
        vTaskDeleteWithCaps(NULL);
        return;
    }

    for (;;) {
        bool playing_now;
        portENTER_CRITICAL(&s_mux);
        playing_now = s_now.active && s_now.playing;
        portEXIT_CRITICAL(&s_mux);

        // Block indefinitely while idle/paused (nothing to decode — avoids
        // burning CPU); poll between chunks while playing so pause/skip land
        // within a frame or two.
        TickType_t wait = playing_now ? 0 : portMAX_DELAY;
        player_cmd_t cmd;
        if (xQueueReceive(s_cmd_queue, &cmd, wait) == pdTRUE) {
            handle_command(&cmd);
            continue;
        }
        if (!playing_now) continue; // spurious wakeup while idle/paused

        if (!s_dec_open || !decode_and_play_chunk()) {
            advance_track(+1); // EOF (or decode error) — move on
        }
    }
}

static bool ensure_engine_started(void)
{
    if (s_task) return true;
    if (!ensure_sd_mounted()) return false;
    if (music_catalog_load(BSP_SD_MOUNT_POINT) != ESP_OK) {
        ESP_LOGW(TAG, "Cannot start playback engine — no usable catalog");
        return false;
    }
    s_spk = audio_alert_acquire_speaker();
    if (!s_spk) return false;

#if CONFIG_PM_ENABLE
    if (!s_no_ls_lock) {
        // See s_no_ls_lock's comment — must exist before open_codec() can ever
        // run, so create it up front rather than lazily inside that hot path.
        (void)esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "music_player", &s_no_ls_lock);
        if (!s_no_ls_lock) {
            ESP_LOGW(TAG, "Failed to create no-light-sleep lock — playback may glitch with display off");
        }
    }
#endif

    s_cmd_queue = xQueueCreate(CMD_QUEUE_LEN, sizeof(player_cmd_t));
    if (!s_cmd_queue) goto fail;

    // Order matters: each of these is depended on by the task spawned right
    // after it (music_feed_task needs s_pcm_stream/s_feed_buf; music_player_task
    // needs s_pcm_stream too, via decode_and_play_chunk), so build bottom-up
    // and unwind top-down on failure.
    s_pcm_stream = xStreamBufferCreateWithCaps(MUSIC_PCM_RING_BYTES, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_pcm_stream) goto fail;

    s_flush_done = xSemaphoreCreateBinary();
    if (!s_flush_done) goto fail;

    s_feed_buf = (int16_t *)heap_caps_malloc(MUSIC_FEED_CHUNK_BYTES, MALLOC_CAP_SPIRAM);
    if (!s_feed_buf) goto fail;

    if (xTaskCreateWithCaps(music_feed_task, "music_feed", MUSIC_FEED_TASK_STACK,
                            NULL, MUSIC_FEED_TASK_PRIO, &s_feed_task,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start feed task");
        s_feed_task = NULL;
        goto fail;
    }

    if (xTaskCreateWithCaps(music_player_task, "music_player", MUSIC_PLAYER_TASK_STACK,
                            NULL, MUSIC_PLAYER_TASK_PRIO, &s_task,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start playback task");
        s_task = NULL;
        goto fail;
    }
    return true;

fail:
    if (s_feed_task) { vTaskDeleteWithCaps(s_feed_task); s_feed_task = NULL; }
    if (s_feed_buf)  { heap_caps_free(s_feed_buf); s_feed_buf = NULL; }
    if (s_flush_done) { vSemaphoreDelete(s_flush_done); s_flush_done = NULL; }
    if (s_pcm_stream) { vStreamBufferDeleteWithCaps(s_pcm_stream); s_pcm_stream = NULL; }
    if (s_cmd_queue) { vQueueDelete(s_cmd_queue); s_cmd_queue = NULL; }
    s_task = NULL;
    return false;
}

// ── public transport — enqueue and return; state changes land asynchronously

void music_player_play(uint32_t flat_track_index)
{
    if (flat_track_index >= g_music_track_count) return;
    if (!ensure_engine_started()) return;
    player_cmd_t cmd = { .type = CMD_PLAY, .arg = flat_track_index };
    (void)xQueueSend(s_cmd_queue, &cmd, 0);
}

void music_player_toggle_play_pause(void)
{
    if (!s_task) return;
    player_cmd_t cmd = { .type = CMD_TOGGLE_PLAY_PAUSE };
    (void)xQueueSend(s_cmd_queue, &cmd, 0);
}

void music_player_next(void)
{
    if (!s_task) return;
    player_cmd_t cmd = { .type = CMD_NEXT };
    (void)xQueueSend(s_cmd_queue, &cmd, 0);
}

void music_player_prev(void)
{
    if (!s_task) return;
    player_cmd_t cmd = { .type = CMD_PREV };
    (void)xQueueSend(s_cmd_queue, &cmd, 0);
}
