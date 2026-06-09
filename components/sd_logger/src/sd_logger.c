// Mirrors ESP_LOG output to /sdcard/logs/s3watch_NNNNN.log for the lifetime
// of the boot session. Gated by settings_get_sd_logging_enabled() — fully
// inert (no SD card access, no hook installed) when the setting is off.
//
// Producer side (the esp_log_set_vprintf hook, called from whichever task is
// logging — LVGL, WiFi, etc.) must never block: it formats the line and
// hands it to a stream buffer with a zero timeout, dropping it on overflow.
// A single low-priority writer task drains the buffer and is the only thing
// that ever touches the file/SD card.
//
// The hook also must not grow the CALLING task's stack by much: it runs on
// whichever task is logging, including small system-internal tasks sized for
// the original lightweight vprintf — so the format buffer is heap-allocated
// rather than a local array (a stack-resident SD_LOG_LINE_MAX buffer here
// once overflowed the 2KB "wifi_rel" task's stack and caused a bootloop).

#include "sd_logger.h"
#include "settings.h"
#include "sd_manager.h"
#include "bsp/esp32_s3_touch_amoled_2_06.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_log_write.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <ctype.h>

// fsync(2) is implemented by the VFS/FATFS layer (reaches f_sync on the FAT
// volume) but esp-idf's bundled <unistd.h> doesn't declare it for this
// target — declare it ourselves so the durability flush below is real.
extern int fsync(int fd);

static const char *TAG = "SD_LOG";

#define SD_LOG_DIR             BSP_SD_MOUNT_POINT "/logs"
// This firmware builds FatFs with FF_USE_LFN == 0 (CONFIG_FATFS_LFN_NONE) —
// only legacy 8.3 short names are accepted (max 8 chars + a 3-char
// extension), so the name portion must stay within "logNNNNN" (8 chars).
#define SD_LOG_NAME_FMT        SD_LOG_DIR "/log%05u.log"
#define SD_LOG_NAME_SCAN_FMT   "log%05u.log"
#define SD_LOG_STREAM_BUF_SIZE (8 * 1024)
#define SD_LOG_LINE_MAX        (512)
#define SD_LOG_FLUSH_THRESHOLD (256)

static FILE *s_log_file = NULL;
static StreamBufferHandle_t s_stream = NULL;
static vprintf_like_t s_orig_vprintf = NULL;

// ── vprintf hook — runs on the caller's task, must be O(memcpy) ─────────────

static int sd_log_vprintf(const char *fmt, va_list args)
{
    // This hook is installed globally, so it runs on whatever task happens to
    // be logging — including small system-internal tasks (e.g. "wifi_rel" has
    // only a 2KB stack). A SD_LOG_LINE_MAX-byte array here would sit on THAT
    // task's stack on top of its normal usage and can overflow it (this is
    // exactly what caused the wifi_rel stack-overflow bootloop). Heap-allocate
    // instead so the only stack cost is the pointer + malloc/free frames.
    char *buf = (char *)malloc(SD_LOG_LINE_MAX);
    if (buf) {
        va_list args_copy;
        va_copy(args_copy, args);
        int len = vsnprintf(buf, SD_LOG_LINE_MAX, fmt, args_copy);
        va_end(args_copy);

        if (len > 0) {
            size_t n = (size_t)len < SD_LOG_LINE_MAX ? (size_t)len : SD_LOG_LINE_MAX - 1;
            // Zero timeout: a full buffer means the writer can't keep up —
            // drop this line from the file (UART still gets it via the call
            // below) rather than ever stalling the caller.
            xStreamBufferSend(s_stream, buf, n, 0);
        }
        free(buf);
    }
    // else: allocation failed (e.g. heap pressure) — just skip capturing this
    // line to the SD file; the caller still gets its normal log output below.

    return s_orig_vprintf(fmt, args);
}

// ── writer task — the only thing that touches the file/SD card ──────────────

static void sd_log_writer_task(void *arg)
{
    (void)arg;
    uint8_t chunk[SD_LOG_LINE_MAX];
    size_t pending = 0;

    for (;;) {
        size_t n = xStreamBufferReceive(s_stream, chunk, sizeof(chunk), pdMS_TO_TICKS(1000));
        if (n > 0) {
            fwrite(chunk, 1, n, s_log_file);
            pending += n;
        }
        // Flush on a size threshold and whenever the buffer goes idle, so a
        // crash loses at most a fraction of a second of log.
        if (pending >= SD_LOG_FLUSH_THRESHOLD || (pending > 0 && n == 0)) {
            fflush(s_log_file);
            fsync(fileno(s_log_file));
            pending = 0;
        }
    }
}

// ── filename selection — never overwrite an existing session's log ──────────

static unsigned sd_log_next_index(void)
{
    unsigned max_index = 0;
    DIR *dir = opendir(SD_LOG_DIR);
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            // FatFs always stores 8.3 short names as upper-case bytes on
            // disk, and with FF_USE_LFN == 0 (CONFIG_FATFS_LFN_NONE here)
            // get_fileinfo()'s non-LFN path copies those raw bytes straight
            // into d_name with no case conversion — so a file we created as
            // "log00001.log" reads back as "LOG00001.LOG". A case-sensitive
            // sscanf() against our lower-case scan format never matched, so
            // max_index stayed 0 forever and every boot re-opened (and, via
            // fopen's "w" mode, truncated) log00001.log instead of rotating.
            // Lower-case a copy before matching so this is robust regardless
            // of the case FatFs hands back.
            char name[16];
            size_t i = 0;
            for (; i < sizeof(name) - 1 && ent->d_name[i] != '\0'; i++) {
                name[i] = (char)tolower((unsigned char)ent->d_name[i]);
            }
            name[i] = '\0';

            unsigned idx;
            if (sscanf(name, SD_LOG_NAME_SCAN_FMT, &idx) == 1 && idx > max_index) {
                max_index = idx;
            }
        }
        closedir(dir);
    }
    // Empty/missing directory (or scan failure) just starts the sequence —
    // the number is derived from the card itself, so it can't collide with
    // or overwrite anything that's already there.
    return max_index + 1;
}

// ── init ─────────────────────────────────────────────────────────────────────

void sd_logger_init(void)
{
    if (!settings_get_sd_logging_enabled()) {
        ESP_LOGI(TAG, "SD logging disabled in settings");
        return;
    }

    if (sd_manager_acquire() != ESP_OK) {
        ESP_LOGW(TAG, "SD card mount failed — file logging disabled");
        return;
    }

    if (mkdir(SD_LOG_DIR, 0775) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "Could not create %s (errno %d) — file logging disabled", SD_LOG_DIR, errno);
        sd_manager_release();
        return;
    }

    char path[96];
    snprintf(path, sizeof(path), SD_LOG_NAME_FMT, sd_log_next_index());

    s_log_file = fopen(path, "w");
    if (!s_log_file) {
        ESP_LOGW(TAG, "Could not open %s for writing — file logging disabled", path);
        sd_manager_release();
        return;
    }

    s_stream = xStreamBufferCreate(SD_LOG_STREAM_BUF_SIZE, 1);
    if (!s_stream) {
        ESP_LOGW(TAG, "Could not allocate log stream buffer — file logging disabled");
        fclose(s_log_file);
        s_log_file = NULL;
        sd_manager_release();
        return;
    }

    if (xTaskCreate(sd_log_writer_task, "sd_log_writer", 3072, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
        ESP_LOGW(TAG, "Could not start log writer task — file logging disabled");
        vStreamBufferDelete(s_stream);
        s_stream = NULL;
        fclose(s_log_file);
        s_log_file = NULL;
        sd_manager_release();
        return;
    }

    s_orig_vprintf = esp_log_set_vprintf(sd_log_vprintf);
    ESP_LOGI(TAG, "Logging to %s", path);
}
