#include "ntp_sync.h"
#include "wifi_manager.h"
#include "rtc_lib.h"
#include "settings.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_sntp.h"
#include "esp_timer.h"   // esp_timer_get_time (monotonic attempt clock)
#include "task_coordinator.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>
#include <string.h>

static void wifi_release_task(void *arg)
{
    (void)arg;
    wifi_manager_release();
    vTaskDelete(NULL);
}

static const char *TAG = "NTP_SYNC";
static time_t s_last_sync = 0;
// Guards s_last_sync + s_last_attempt_us: written from the SNTP/lwIP callback
// (on_sntp_sync) and the attempt paths, read from the periodic check on
// task_coordinator's task. Mirrors rtc_lib.c's s_time_mux pattern for
// cached-time scalars.
static portMUX_TYPE s_sync_mux = portMUX_INITIALIZER_UNLOCKED;

// Retry throttle + failure watchdog. An NTP-initiated radio wake that never
// produces a sync (away from the saved network, AP down, server unreachable)
// must (a) not be retried more than once per NTP_RETRY_BACKOFF_US and (b) not
// leave the radio powered — without the watchdog a failed attempt kept WiFi on
// indefinitely, silently draining the battery whenever the watch was away from
// home. Attempt bookkeeping uses esp_timer_get_time() (monotonic since boot)
// so it is immune to the very clock jumps NTP itself causes.
//
// The watchdog is a DEADLINE CHECKED BY A task_coordinator SUBSCRIBER (the
// house pattern — same as alarm_manager's auto-silence), not a standalone
// timer: s_attempt_deadline_us is set when an attempt starts, cleared on sync,
// and ntp_watchdog_cb fires the release once the deadline passes. The
// subscriber runs in both display states (an attempt can outlive display-on).
#define NTP_RETRY_BACKOFF_US   (6LL * 60 * 60 * 1000000)  // ≥6 h between attempts
#define NTP_ATTEMPT_TIMEOUT_US (45LL * 1000000)           // connect+DNS+SNTP window
// Boot-quiet window: no NTP radio wake during early boot. The boot tone
// streams MP3 from SPIFFS with only the ~33 ms I2S DMA as buffer, right when
// WiFi bring-up, the sync's NVS commit (flash-cache stall — freezes every
// flash-resident task incl. the decoder), and esp_wifi_stop would all land —
// audible cutouts. The clock is already correct from the battery-backed RTC,
// so deferring the first sync to the first ntp_sync_check (~30 s in) costs
// nothing.
#define NTP_BOOT_QUIET_US      (15LL * 1000000)
static int64_t s_last_attempt_us     = 0;  // 0 = never attempted (guarded by s_sync_mux)
static int64_t s_attempt_deadline_us = 0;  // 0 = no attempt pending (guarded by s_sync_mux)

// Coordinator subscriber: no sync arrived within the attempt window — shut the
// radio back off (wifi_manager_release defers on its own if e.g. SignalK holds
// it). esp_wifi_stop()'s deep, log-chatty teardown stays off the coordinator
// task; reuse the one-shot release task (same reason as the SNTP-callback path
// below).
static void ntp_watchdog_cb(void *user)
{
    (void)user;
    int64_t now_us = esp_timer_get_time();
    portENTER_CRITICAL(&s_sync_mux);
    bool expired = (s_attempt_deadline_us != 0) && (now_us >= s_attempt_deadline_us);
    if (expired) s_attempt_deadline_us = 0;
    portEXIT_CRITICAL(&s_sync_mux);
    if (!expired) return;
    ESP_LOGW(TAG, "no NTP sync within %d s — releasing WiFi (next attempt in %d h)",
             (int)(NTP_ATTEMPT_TIMEOUT_US / 1000000),
             (int)(NTP_RETRY_BACKOFF_US / 3600000000LL));
    xTaskCreate(wifi_release_task, "wifi_rel", 4096, NULL, 5, NULL);
}

// Mirror the freshly-synced system time to the hardware RTC, then release the
// radio. Runs on a one-shot task, NOT in the SNTP/lwIP callback: rtc_set_time
// is an I2C write plus an NVS flash commit — stalling the TCP/IP task for a
// flash commit is the same class of problem as calling esp_wifi_stop() from
// there. No argument needed: SNTP applies the new time via settimeofday()
// BEFORE the notify callback fires, so time(NULL) here is the synced value.
static void ntp_apply_sync_task(void *arg)
{
    (void)arg;
    time_t now = time(NULL);
    struct tm t;
    gmtime_r(&now, &t);
    rtc_set_time(&t);
    ESP_LOGI(TAG, "RTC synced from NTP: %04d-%02d-%02d %02d:%02d:%02d UTC",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);
    wifi_manager_release();
    vTaskDelete(NULL);
}

static void on_sntp_sync(struct timeval *tv)
{
    (void)tv;
    // SNTP has already set the system time; validate it before bookkeeping.
    time_t now = time(NULL);
    struct tm t;
    gmtime_r(&now, &t);
    // Sanity check — ignore if year is clearly wrong
    if (t.tm_year < 125 || t.tm_year > 199) {
        ESP_LOGW(TAG, "SNTP gave suspicious year %d, ignoring", t.tm_year + 1900);
        return;
    }
    portENTER_CRITICAL(&s_sync_mux);
    s_last_sync = now;
    // Disarm the failure watchdog (no-op if it wasn't armed, e.g. an
    // opportunistic sync on a SignalK-initiated connection).
    s_attempt_deadline_us = 0;
    portEXIT_CRITICAL(&s_sync_mux);

    // Defer the heavy work (RTC mirror: I2C + NVS commit; then esp_wifi_stop)
    // to a one-shot task — neither belongs in the SNTP/lwIP callback context
    // (esp_wifi_stop tears down lwIP from within lwIP; the NVS commit stalls
    // the TCP/IP task). 4096, not 2048: esp_wifi_stop() is a deep, log-chatty
    // call chain, and with SD logging on every ESP_LOG runs through
    // sd_log_vprintf's hook (malloc+vsnprintf+stream-send+free) — that extra
    // per-call depth is what overflowed this task at 2048.
    //
    // Fallback matters: if the task can't spawn, mirror the RTC inline anyway
    // — rtc_minute_sync re-syncs the system clock FROM the PCF85063A every
    // minute, so leaving the PCF stale would regress the just-synced clock.
    if (xTaskCreate(ntp_apply_sync_task, "ntp_apply", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGW(TAG, "ntp_apply task create failed — mirroring RTC inline");
        rtc_set_time(&t);
        xTaskCreate(wifi_release_task, "wifi_rel", 4096, NULL, 5, NULL);
    }
}

static void wifi_connected_cb(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    (void)arg; (void)base; (void)id; (void)data;
    ESP_LOGI(TAG, "WiFi connected — triggering NTP sync");
    ntp_sync_now();
}

esp_err_t ntp_sync_init(void)
{
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, settings_get_ntp_server());
    sntp_set_time_sync_notification_cb(on_sntp_sync);
    // Don't call esp_sntp_init() yet — wait until WiFi is up.
    esp_event_handler_register(WIFI_MANAGER_EVENT_BASE,
                               WIFI_MGR_EVT_CONNECTED,
                               wifi_connected_cb, NULL);
    // Failure-watchdog deadline check on the shared coordinator (pre-start
    // subscription: ntp_sync_init runs in app_main, task_coord_start() comes
    // later via ui_task → display_manager_init). Runs display-off too — an
    // attempt window can span a display sleep.
    task_coord_subscribe("ntp_watchdog", ntp_watchdog_cb, NULL,
                         /*on*/ 1000, /*off*/ 2000);
    ESP_LOGI(TAG, "Initialized (server: %s)", settings_get_ntp_server());
    return ESP_OK;
}

// Wake the radio and try a saved-network connect for an NTP sync, throttled to
// one attempt per NTP_RETRY_BACKOFF_US, with the failure watchdog armed so an
// unsuccessful attempt releases the radio instead of leaving it on. Shared by
// the boot-time connect (main.cpp) and the periodic ntp_sync_check().
esp_err_t ntp_sync_attempt(void)
{
    if (!settings_get_wifi_enabled()) return ESP_ERR_INVALID_STATE;

    int64_t now_us = esp_timer_get_time();
    // Boot-quiet: don't wake the radio while the boot tone / UI bring-up are
    // still running (see NTP_BOOT_QUIET_US). Doesn't record an attempt, so the
    // first check after the window proceeds normally.
    if (now_us < NTP_BOOT_QUIET_US) return ESP_ERR_NOT_FINISHED;

    portENTER_CRITICAL(&s_sync_mux);
    bool throttled = (s_last_attempt_us != 0) &&
                     (now_us - s_last_attempt_us) < NTP_RETRY_BACKOFF_US;
    if (!throttled) {
        s_last_attempt_us     = now_us;
        s_attempt_deadline_us = now_us + NTP_ATTEMPT_TIMEOUT_US;  // arm watchdog
    }
    portEXIT_CRITICAL(&s_sync_mux);
    if (throttled) return ESP_ERR_NOT_FINISHED;

    ESP_LOGI(TAG, "attempting NTP sync (waking WiFi)");
    esp_err_t err = wifi_manager_wake();
    if (err == ESP_OK) err = wifi_manager_auto_connect();
    if (err != ESP_OK) {
        // Immediate failure (no saved networks / driver error) — don't wait the
        // full watchdog window with the radio on.
        ESP_LOGW(TAG, "sync attempt failed immediately (%s) — releasing WiFi",
                 esp_err_to_name(err));
        portENTER_CRITICAL(&s_sync_mux);
        s_attempt_deadline_us = 0;                                // disarm watchdog
        portEXIT_CRITICAL(&s_sync_mux);
        xTaskCreate(wifi_release_task, "wifi_rel", 4096, NULL, 5, NULL);
    }
    return err;
    // Async failures (association/DHCP/SNTP never completing) are handled by
    // ntp_watchdog_cb; success disarms the deadline in on_sntp_sync.
}

esp_err_t ntp_sync_now(void)
{
    if (!wifi_manager_is_connected()) return ESP_ERR_INVALID_STATE;
    // Update server name in case it changed since init
    esp_sntp_setservername(0, settings_get_ntp_server());
    if (!esp_sntp_enabled()) {
        esp_sntp_init();
    } else {
        sntp_restart();
    }
    return ESP_OK;
}

const char *ntp_sync_get_server(void) { return settings_get_ntp_server(); }

void ntp_sync_set_server(const char *server)
{
    settings_set_ntp_server(server);
    esp_sntp_setservername(0, server);
}

void ntp_sync_check(void)
{
    // Fresh sync needed when: the clock is plainly invalid (pre-2025 — dead RTC
    // backup), we've never synced, or the last sync is >24 h old. (Previously an
    // invalid clock returned early here, so a watch with a dead RTC never got a
    // periodic retry at all.)
    time_t now = time(NULL);
    if (now >= (time_t)1735689600) {
        portENTER_CRITICAL(&s_sync_mux);
        time_t last_sync = s_last_sync;
        portEXIT_CRITICAL(&s_sync_mux);
        if (last_sync > 0 && (now - last_sync) < 86400) return;
    }
    // Respect the user's WiFi permission — never wake radio if disabled.
    if (!settings_get_wifi_enabled()) return;
    if (wifi_manager_is_connected()) return; // already up, sync fires via event
    // Throttled to one radio wake per 6 h; failure watchdog releases the radio.
    (void)ntp_sync_attempt();
}
