#include "settings.h"
#include "esp_log.h"
#include "esp_err.h"
#include "sd_manager.h"
#include "bsp/display.h"
#include "bsp/esp32_s3_touch_amoled_2_06.h"
#include "bsp_board_extra.h"
#include "rtc_lib.h"
#include "wifi_manager.h"
#include <time.h>
#include <stdio.h>
#include <sys/stat.h>
#include "esp_spiffs.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "cJSON.h"

static const char *TAG = "SETTINGS";

#define DEFAULT_BRIGHTNESS       25
#define DEFAULT_DISPLAY_TIMEOUT  10000
#define DEFAULT_SOUND_ENABLED    true
#define DEFAULT_NOTIFY_VOLUME    100
#define DEFAULT_NTP_SERVER       "pool.ntp.org"
#define DEFAULT_TZ               "UTC0"
#define DEFAULT_TIME_24H         true
#define DEFAULT_WIFI_ENABLED     false
#define DEFAULT_WATCHFACE_STYLE  0
#define DEFAULT_WATCHFACE_BG     0
#define DEFAULT_SIGNALK_HOST     ""
#define DEFAULT_SIGNALK_PORT     3000
#define DEFAULT_SD_LOGGING_ENABLED false

static uint8_t brightness = DEFAULT_BRIGHTNESS;
static uint32_t display_timeout_ms = DEFAULT_DISPLAY_TIMEOUT;
static bool sound_enabled = DEFAULT_SOUND_ENABLED;
static uint8_t notify_volume = DEFAULT_NOTIFY_VOLUME;
static char ntp_server[64] = DEFAULT_NTP_SERVER;
static char tz[40] = DEFAULT_TZ;        // POSIX TZ string; applied via setenv/tzset
static char signalk_host[64] = DEFAULT_SIGNALK_HOST;  // empty = unconfigured
static uint16_t signalk_port = DEFAULT_SIGNALK_PORT;
static bool time_24h = DEFAULT_TIME_24H;
static bool wifi_enabled = DEFAULT_WIFI_ENABLED;
static bool sd_logging_enabled = DEFAULT_SD_LOGGING_ENABLED;
static int watchface_style = DEFAULT_WATCHFACE_STYLE;
static int watchface_bg = DEFAULT_WATCHFACE_BG;
static bool spiffs_ready = false;

// Apply the current `tz` value to the libc localtime machinery.
static void apply_tz(void) {
    setenv("TZ", tz, 1);
    tzset();
}

// Debounced save timer (to limit flash writes when sliders change)
static TimerHandle_t s_save_timer = NULL;
// Forward declarations for internal JSON IO
static bool settings_write_json(void);
static bool settings_read_json(void);
static void save_timer_cb(TimerHandle_t xTimer)
{
    (void)xTimer;
    (void)settings_write_json();
}
static void schedule_save(void)
{
    const TickType_t delay_ticks = pdMS_TO_TICKS(10000); // 10 seconds
    if (!s_save_timer) {
        s_save_timer = xTimerCreate("settings_save", delay_ticks, pdFALSE, NULL, save_timer_cb);
    }
    if (!s_save_timer) return;
    // Restart (or start) timer with 10s
    (void)xTimerStop(s_save_timer, 0);
    (void)xTimerChangePeriod(s_save_timer, delay_ticks, 0);
    (void)xTimerStart(s_save_timer, 0);
}

#define SETTINGS_PARTITION "storage"
#define SETTINGS_FILE      "/spiffs/settings.json"

static bool settings_mount_spiffs(void)
{
    if (spiffs_ready) return true;
    // First try to mount without formatting
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = SETTINGS_PARTITION,
        .max_files = 4,
        .format_if_mount_failed = false,
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret == ESP_OK) {
        size_t total = 0, used = 0;
        if (esp_spiffs_info(SETTINGS_PARTITION, &total, &used) == ESP_OK) {
            ESP_LOGI(TAG, "SPIFFS mounted: %u/%u bytes used", (unsigned)used, (unsigned)total);
        }
        spiffs_ready = true;
        return true;
    }
    // If mount failed, inform user and retry with format
    ESP_LOGW(TAG, "SPIFFS mount failed (%s). Formatting...", esp_err_to_name(ret));
    // Show a simple full-screen message while formatting, if LVGL is ready
    lv_obj_t* overlay = NULL;
    if (lv_disp_get_default() != NULL && bsp_display_lock(100)) {
        overlay = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(overlay);
        lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
        lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
        lv_obj_t* label = lv_label_create(overlay);
        lv_label_set_text(label, "Initializing smartwatch...\nFormatting storage...");
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(label);
        lv_refr_now(NULL);
        bsp_display_unlock();
    }

    // Unregister before retrying — a failed mount may have left the VFS entry
    // in a partial state, which would cause the second register to return
    // ESP_ERR_INVALID_STATE and skip the format entirely.
    esp_vfs_spiffs_unregister(SETTINGS_PARTITION);
    conf.format_if_mount_failed = true;
    ret = esp_vfs_spiffs_register(&conf);
    if (overlay && bsp_display_lock(100)) {
        lv_obj_del(overlay);
        overlay = NULL;
        lv_refr_now(NULL);
        bsp_display_unlock();
    }
    if (ret == ESP_OK) {
        size_t total = 0, used = 0;
        (void)esp_spiffs_info(SETTINGS_PARTITION, &total, &used);
        ESP_LOGI(TAG, "SPIFFS formatted and mounted: %u/%u bytes used", (unsigned)used, (unsigned)total);
        spiffs_ready = true;
        return true;
    }
    ESP_LOGE(TAG, "Failed to format+mount SPIFFS (%s)", esp_err_to_name(ret));
    return false;
}

// Builds a cJSON object from the in-memory settings. Caller owns/deletes it.
// Shared by SPIFFS save and SD backup so the field list lives in one place.
static cJSON *settings_to_json(void)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON_AddNumberToObject(root, "brightness", brightness);
    cJSON_AddNumberToObject(root, "display_timeout_ms", (double)display_timeout_ms);
    cJSON_AddBoolToObject(root, "sound_enabled", sound_enabled);
    cJSON_AddNumberToObject(root, "notify_volume", (double)notify_volume);
    cJSON_AddStringToObject(root, "ntp_server", ntp_server);
    cJSON_AddStringToObject(root, "tz", tz);
    cJSON_AddBoolToObject(root, "time_24h", time_24h);
    cJSON_AddBoolToObject(root, "wifi_enabled", wifi_enabled);
    cJSON_AddNumberToObject(root, "watchface_style", watchface_style);
    cJSON_AddNumberToObject(root, "watchface_bg", watchface_bg);
    cJSON_AddStringToObject(root, "signalk_host", signalk_host);
    cJSON_AddNumberToObject(root, "signalk_port", (double)signalk_port);
    cJSON_AddBoolToObject(root, "sd_logging_enabled", sd_logging_enabled);
    return root;
}

// Applies fields from a parsed JSON object onto the in-memory settings, then
// deletes it (takes ownership either way — even on a NULL/garbage object).
// Shared by SPIFFS load and SD restore: each field is looked up and type-
// checked individually, so a field that's absent or the wrong type is simply
// skipped, leaving whatever value is already in memory. This is what makes
// loading/restoring an old (or newer) settings file safe — missing fields
// don't get zeroed, and unknown extra fields are silently ignored. Clamp
// numerics before narrowing — a corrupted JSON with brightness:300 would
// otherwise wrap to 44 and silently behave wrong.
static bool settings_from_json(cJSON *root)
{
    if (!root) return false;
    cJSON *j;
    j = cJSON_GetObjectItem(root, "brightness");
    if (cJSON_IsNumber(j)) {
        double v = j->valuedouble;
        brightness = (v < 0) ? 0 : (v > 100) ? 100 : (uint8_t)v;
    }
    j = cJSON_GetObjectItem(root, "display_timeout_ms");
    if (cJSON_IsNumber(j)) {
        double v = j->valuedouble;
        // Accept only the whitelisted values matching settings_set_display_timeout
        uint32_t to = (uint32_t)v;
        if (to == 10000 || to == 20000 || to == 30000 || to == 60000) {
            display_timeout_ms = to;
        }
    }
    j = cJSON_GetObjectItem(root, "sound_enabled");
    if (cJSON_IsBool(j)) sound_enabled = cJSON_IsTrue(j);
    j = cJSON_GetObjectItem(root, "notify_volume");
    if (cJSON_IsNumber(j)) {
        double v = j->valuedouble;
        notify_volume = (v < 0) ? 0 : (v > 100) ? 100 : (uint8_t)v;
    }
    j = cJSON_GetObjectItem(root, "ntp_server");
    if (cJSON_IsString(j) && j->valuestring[0]) {
        strncpy(ntp_server, j->valuestring, sizeof(ntp_server) - 1);
        ntp_server[sizeof(ntp_server) - 1] = '\0';
    }
    j = cJSON_GetObjectItem(root, "tz");
    if (cJSON_IsString(j) && j->valuestring[0]) {
        strncpy(tz, j->valuestring, sizeof(tz) - 1);
        tz[sizeof(tz) - 1] = '\0';
    }
    j = cJSON_GetObjectItem(root, "time_24h");
    if (cJSON_IsBool(j)) time_24h = cJSON_IsTrue(j);
    j = cJSON_GetObjectItem(root, "wifi_enabled");
    if (cJSON_IsBool(j)) wifi_enabled = cJSON_IsTrue(j);
    j = cJSON_GetObjectItem(root, "watchface_style");
    if (cJSON_IsNumber(j)) watchface_style = (int)cJSON_GetNumberValue(j);
    j = cJSON_GetObjectItem(root, "watchface_bg");
    if (cJSON_IsNumber(j)) watchface_bg = (int)cJSON_GetNumberValue(j);
    j = cJSON_GetObjectItem(root, "signalk_host");
    if (cJSON_IsString(j)) {
        // Empty string is valid — clears the setting.
        strncpy(signalk_host, j->valuestring, sizeof(signalk_host) - 1);
        signalk_host[sizeof(signalk_host) - 1] = '\0';
    }
    j = cJSON_GetObjectItem(root, "signalk_port");
    if (cJSON_IsNumber(j)) {
        int p = (int)j->valuedouble;
        if (p >= 1 && p <= 65535) signalk_port = (uint16_t)p;
    }
    j = cJSON_GetObjectItem(root, "sd_logging_enabled");
    if (cJSON_IsBool(j)) sd_logging_enabled = cJSON_IsTrue(j);
    cJSON_Delete(root);
    // Apply to hardware where relevant
    bsp_display_brightness_set(brightness);
    ESP_LOGI(TAG, "Settings applied: br=%u, to=%u, sound=%d", (unsigned)brightness, (unsigned)display_timeout_ms, (int)sound_enabled);
    return true;
}

static bool settings_write_json(void)
{
    if (!settings_mount_spiffs()) return false;
    cJSON *root = settings_to_json();
    if (!root) return false;

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) return false;

    FILE *f = fopen(SETTINGS_FILE, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s for write", SETTINGS_FILE);
        cJSON_free(json_str);
        return false;
    }
    size_t expected = strlen(json_str);
    size_t n = fwrite(json_str, 1, expected, f);
    fclose(f);
    cJSON_free(json_str);
    if (n != expected) {
        ESP_LOGE(TAG, "Partial write to %s (%u/%u bytes) — removing corrupt file",
                 SETTINGS_FILE, (unsigned)n, (unsigned)expected);
        remove(SETTINGS_FILE);
        return false;
    }
    ESP_LOGI(TAG, "Settings saved to %s", SETTINGS_FILE);
    return true;
}

static bool settings_read_json(void)
{
    if (!settings_mount_spiffs()) return false;
    struct stat st;
    if (stat(SETTINGS_FILE, &st) != 0 || st.st_size <= 0) {
        ESP_LOGW(TAG, "Settings file not found; using defaults");
        return false;
    }
    FILE *f = fopen(SETTINGS_FILE, "r");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s for read", SETTINGS_FILE);
        return false;
    }
    char *buf = (char*)malloc(st.st_size + 1);
    if (!buf) { fclose(f); return false; }
    size_t n = fread(buf, 1, st.st_size, f);
    fclose(f);
    buf[n] = '\0';
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON settings");
        return false;
    }
    if (!settings_from_json(root)) return false;
    ESP_LOGI(TAG, "Settings loaded from %s", SETTINGS_FILE);
    return true;
}

// ── SD-card backup/restore ──────────────────────────────────────────────────
// Manual only (driven from the Backup & Restore screen) — a single snapshot
// file at the SD card root, overwritten each time you back up; there's only
// ever one. Filename respects the firmware's 8.3 short-name constraint
// (FF_USE_LFN == 0): "settings" (8 chars) + "bak" (3 chars).
#define SD_BACKUP_FILE BSP_SD_MOUNT_POINT "/settings.bak"

bool settings_sd_backup_exists(void)
{
    if (sd_manager_acquire() != ESP_OK) return false;
    struct stat st;
    bool exists = (stat(SD_BACKUP_FILE, &st) == 0 && st.st_size > 0);
    sd_manager_release();
    return exists;
}

bool settings_backup_to_sd(void)
{
    if (sd_manager_acquire() != ESP_OK) return false;

    cJSON *root = settings_to_json();
    if (root) {
        // Folded in here (not in settings_to_json) so SPIFFS persistence is
        // unaffected — saved Wi-Fi networks live in wifi_manager's own NVS
        // namespace, not in the regular settings store. Without this, restoring
        // a backup after a reflash would still mean re-typing every password.
        cJSON *nets = wifi_manager_export_networks();
        if (nets) cJSON_AddItemToObject(root, "wifi_networks", nets);
    }
    char *json_str = root ? cJSON_PrintUnformatted(root) : NULL;
    if (root) cJSON_Delete(root);
    if (!json_str) {
        sd_manager_release();
        return false;
    }

    bool ok = false;
    FILE *f = fopen(SD_BACKUP_FILE, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s for write", SD_BACKUP_FILE);
    } else {
        size_t expected = strlen(json_str);
        size_t n = fwrite(json_str, 1, expected, f);
        fclose(f);
        ok = (n == expected);
        if (!ok) {
            ESP_LOGE(TAG, "Partial write to %s — removing corrupt file", SD_BACKUP_FILE);
            remove(SD_BACKUP_FILE);
        }
    }
    cJSON_free(json_str);
    sd_manager_release();
    if (ok) ESP_LOGI(TAG, "Settings backed up to %s", SD_BACKUP_FILE);
    return ok;
}

bool settings_restore_from_sd(void)
{
    if (sd_manager_acquire() != ESP_OK) return false;

    struct stat st;
    cJSON *root = NULL;
    if (stat(SD_BACKUP_FILE, &st) == 0 && st.st_size > 0) {
        FILE *f = fopen(SD_BACKUP_FILE, "r");
        if (f) {
            char *buf = (char*)malloc(st.st_size + 1);
            if (buf) {
                size_t n = fread(buf, 1, st.st_size, f);
                buf[n] = '\0';
                root = cJSON_Parse(buf);
                free(buf);
            }
            fclose(f);
        }
    }
    sd_manager_release();

    if (!root) {
        ESP_LOGW(TAG, "No usable settings backup at %s", SD_BACKUP_FILE);
        return false;
    }

    // settings_from_json() takes ownership of root (always deletes it) — pull
    // wifi_networks out first so it survives to be applied separately below.
    cJSON *nets = cJSON_DetachItemFromObject(root, "wifi_networks");
    if (!settings_from_json(root)) {
        cJSON_Delete(nets);
        return false;
    }
    if (nets) {
        wifi_manager_import_networks(nets);
        cJSON_Delete(nets);
    }

    ESP_LOGI(TAG, "Settings restored from %s", SD_BACKUP_FILE);
    // Apply immediately so a manual restore takes visible effect right away
    // (not just after a reboot), and persist to SPIFFS so it survives one.
    apply_tz();
    settings_write_json();
    return true;
}

void settings_init(void) {
    ESP_LOGI(TAG, "Settings init: mount + load JSON");
    (void)settings_load();  // applies brightness internally if it reads a value
    // Apply timezone NOW (before rtc_start), so any time-conversion done
    // during boot uses the right TZ.
    apply_tz();

    // Start RTC first — this reads from chip and falls back to NVS if the
    // chip lost power. The default-time check below only fires if neither
    // the chip nor NVS had a valid time.
    rtc_start();

    struct tm time;
    if (rtc_get_time(&time) == ESP_OK) {
        if (time.tm_year < 125) { // tm_year is years since 1900; 125 = 2025
            ESP_LOGI(TAG, "No valid time in chip or NVS, setting default");
            struct tm default_time = {
                .tm_year = 125, // 2025
                .tm_mon = 0,    // January
                .tm_mday = 1,
                .tm_hour = 12,
                .tm_min = 0,
                .tm_sec = 0
            };
            rtc_set_time(&default_time);
        }
    }
}

void settings_set_brightness(uint8_t level) {
    brightness = level;
    bsp_display_brightness_set(brightness);
    schedule_save();
}

uint8_t settings_get_brightness(void) {
    return brightness;
}

void settings_set_display_timeout(uint32_t timeout) {
    if (timeout == 10000 || timeout == 20000 || timeout == 30000 || timeout == 60000) {
        display_timeout_ms = timeout;
        schedule_save();
    }
}

uint32_t settings_get_display_timeout(void) {
    return display_timeout_ms;
}

void settings_set_sound(bool enabled) {
    sound_enabled = enabled;
    ESP_LOGI(TAG, "Sound %s", enabled ? "enabled" : "disabled");
    schedule_save();
}

bool settings_get_sound(void) {
    return sound_enabled;
}

void settings_set_notify_volume(uint8_t vol_percent)
{
    if (vol_percent > 100) vol_percent = 100;
    notify_volume = vol_percent;
    schedule_save();
}

uint8_t settings_get_notify_volume(void)
{
    return notify_volume;
}

bool settings_save(void) {
    return settings_write_json();
}

bool settings_load(void) {
    return settings_read_json();
}


void settings_set_ntp_server(const char *server)
{
    if (!server || !server[0]) return;
    strncpy(ntp_server, server, sizeof(ntp_server) - 1);
    ntp_server[sizeof(ntp_server) - 1] = '\0';
    schedule_save();
}

const char *settings_get_ntp_server(void)
{
    return ntp_server;
}

void settings_set_tz(const char *posix_tz)
{
    if (!posix_tz || !posix_tz[0]) return;
    strncpy(tz, posix_tz, sizeof(tz) - 1);
    tz[sizeof(tz) - 1] = '\0';
    apply_tz();
    schedule_save();
}

const char *settings_get_tz(void)
{
    return tz;
}

void settings_set_signalk_host(const char *host)
{
    if (!host) host = "";
    strncpy(signalk_host, host, sizeof(signalk_host) - 1);
    signalk_host[sizeof(signalk_host) - 1] = '\0';
    schedule_save();
}

const char *settings_get_signalk_host(void)
{
    return signalk_host;
}

void settings_set_signalk_port(uint16_t port)
{
    if (port == 0) return;
    signalk_port = port;
    schedule_save();
}

uint16_t settings_get_signalk_port(void)
{
    return signalk_port;
}

static void apply_defaults(void)
{
    brightness = DEFAULT_BRIGHTNESS;
    display_timeout_ms = DEFAULT_DISPLAY_TIMEOUT;
    sound_enabled = DEFAULT_SOUND_ENABLED;
    notify_volume = DEFAULT_NOTIFY_VOLUME;
    strncpy(ntp_server, DEFAULT_NTP_SERVER, sizeof(ntp_server) - 1);
    ntp_server[sizeof(ntp_server) - 1] = '\0';
    strncpy(tz, DEFAULT_TZ, sizeof(tz) - 1);
    tz[sizeof(tz) - 1] = '\0';
    time_24h = DEFAULT_TIME_24H;
    wifi_enabled = DEFAULT_WIFI_ENABLED;
    watchface_style = DEFAULT_WATCHFACE_STYLE;
    watchface_bg = DEFAULT_WATCHFACE_BG;
    strncpy(signalk_host, DEFAULT_SIGNALK_HOST, sizeof(signalk_host) - 1);
    signalk_host[sizeof(signalk_host) - 1] = '\0';
    signalk_port = DEFAULT_SIGNALK_PORT;
    sd_logging_enabled = DEFAULT_SD_LOGGING_ENABLED;
}

bool settings_reset_defaults(void)
{
    apply_defaults();
    // Apply immediate effects
    bsp_display_brightness_set(brightness);
    return settings_write_json();
}

void settings_set_time_24h(bool enabled)
{
    if (time_24h == enabled) return;
    time_24h = enabled;
    schedule_save();
}

bool settings_get_time_24h(void)
{
    return time_24h;
}

void settings_set_watchface_style(int style)
{
    if (watchface_style == style) return;
    watchface_style = style;
    schedule_save();
}

void settings_set_wifi_enabled(bool enabled)
{
    if (wifi_enabled == enabled) return;
    wifi_enabled = enabled;
    schedule_save();
}

bool settings_get_wifi_enabled(void)
{
    return wifi_enabled;
}

void settings_set_sd_logging_enabled(bool enabled)
{
    if (sd_logging_enabled == enabled) return;
    sd_logging_enabled = enabled;
    schedule_save();
}

bool settings_get_sd_logging_enabled(void)
{
    return sd_logging_enabled;
}

int settings_get_watchface_style(void)
{
    return watchface_style;
}

void settings_set_watchface_bg(int bg)
{
    if (watchface_bg == bg) return;
    watchface_bg = bg;
    schedule_save();
}

int settings_get_watchface_bg(void)
{
    return watchface_bg;
}

bool settings_format_spiffs(void)
{
    // Unregister if mounted
    if (spiffs_ready) {
        esp_vfs_spiffs_unregister(SETTINGS_PARTITION);
        spiffs_ready = false;
    }
    // Show formatting overlay to avoid white screen
    lv_obj_t* overlay = NULL;
    if (lv_disp_get_default() != NULL && bsp_display_lock(100)) {
        overlay = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(overlay);
        lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
        lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
        lv_obj_t* label = lv_label_create(overlay);
        lv_label_set_text(label, "Formatting storage...");
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(label);
        lv_refr_now(NULL);
        bsp_display_unlock();
    }
    esp_err_t r = esp_spiffs_format(SETTINGS_PARTITION);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS format failed: %s", esp_err_to_name(r));
        if (overlay && bsp_display_lock(100)) { lv_obj_del(overlay); lv_refr_now(NULL); bsp_display_unlock(); }
        return false;
    }
    // Remount and write defaults
    bool ok = settings_mount_spiffs() && settings_write_json();
    if (overlay && bsp_display_lock(100)) { lv_obj_del(overlay); lv_refr_now(NULL); bsp_display_unlock(); }
    return ok;
}
