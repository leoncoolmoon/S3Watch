// Alarm engine — runs a 1 Hz time-match (registered at boot, before
// task_coord_start) even while the display is off, so a set alarm fires without
// the app being opened and survives reboot (state is persisted by `settings`).
//
// On a match it wakes the display, starts the looping alarm sound
// (AM_CLIENT_ALARM — keeps playing through display sleep), and lets the screen
// time out normally. The ring stops on dismiss or after the configurable
// auto-silence timeout (settings_get_alarm_timeout_min). One-shot: firing /
// dismiss / auto-silence disarms it.
//
// This component is UI-agnostic. When it fires while the display is already on
// it calls the registered fire callback so gui can navigate to the alarm app;
// the display-off case is handled by the display-wake hook, which polls
// alarm_manager_is_firing().

#include "alarm_manager.h"
#include "settings.h"
#include "audio_alert.h"
#include "display_manager.h"
#include "power_manager.h"
#include "task_coordinator.h"
#include "esp_log.h"
#include <time.h>

static const char *TAG = "alarm_mgr";

static bool            s_firing    = false;
static time_t          s_fire_time = 0;   // when the current ring started (auto-silence)
static alarm_fire_cb_t s_fire_cb   = NULL;

static void stop_ring(void)
{
    if (!s_firing) return;
    s_firing = false;
    audio_alert_alarm_stop();
}

static void alarm_tick_cb(void *user)
{
    (void)user;

    time_t now_t = time(NULL);
    struct tm now;
    localtime_r(&now_t, &now);

    // Auto-silence a ringing alarm once the configured timeout elapses.
    if (s_firing) {
        int timeout_s = settings_get_alarm_timeout_min() * 60;
        if ((now_t - s_fire_time) >= timeout_s) {
            ESP_LOGI(TAG, "alarm auto-silenced after %d min",
                     settings_get_alarm_timeout_min());
            stop_ring();
            settings_set_alarm_enabled(false);  // one-shot
        }
    }

    // Fire on an exact minute match when armed and not already ringing.
    if (settings_get_alarm_enabled() && !s_firing &&
        now.tm_hour == settings_get_alarm_hour() &&
        now.tm_min  == settings_get_alarm_min()) {
        s_firing    = true;
        s_fire_time = now_t;
        ESP_LOGI(TAG, "alarm firing at %02d:%02d",
                 settings_get_alarm_hour(), settings_get_alarm_min());

        bool was_awake = power_manager_is_awake();
        power_manager_request_wake(PM_WAKE_ALARM);  // no-op if already awake
        display_manager_reset_timer();              // start a fresh normal timeout
        audio_alert_alarm_start(settings_get_alarm_sound());
        // Wake-from-sleep navigates via the display-wake hook; for the already-
        // awake case, hand off to the UI via the registered callback.
        if (was_awake && s_fire_cb) s_fire_cb();
    }
}

void alarm_manager_init(void)
{
    s_firing = false;
    task_coord_subscribe("alarm_check", alarm_tick_cb, NULL,
                         /*on*/ 1000, /*off*/ 1000);
    ESP_LOGI(TAG, "alarm engine init: %02d:%02d %s",
             settings_get_alarm_hour(), settings_get_alarm_min(),
             settings_get_alarm_enabled() ? "ON" : "OFF");
}

bool alarm_manager_is_firing(void)
{
    return s_firing;
}

void alarm_manager_dismiss(void)
{
    if (!s_firing) return;
    stop_ring();
    settings_set_alarm_enabled(false);  // one-shot
}

void alarm_manager_set_hour(int hour) { settings_set_alarm_hour(hour); }
int  alarm_manager_get_hour(void)     { return settings_get_alarm_hour(); }
void alarm_manager_set_min(int min)   { settings_set_alarm_min(min); }
int  alarm_manager_get_min(void)      { return settings_get_alarm_min(); }

void alarm_manager_set_enabled(bool enabled)
{
    settings_set_alarm_enabled(enabled);
    if (!enabled) stop_ring();  // turning the alarm off also silences a current ring
}

bool alarm_manager_get_enabled(void)
{
    return settings_get_alarm_enabled();
}

void alarm_manager_set_fire_cb(alarm_fire_cb_t cb)
{
    s_fire_cb = cb;
}
