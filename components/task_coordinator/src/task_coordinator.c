#include "task_coordinator.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "TASK_COORD";

#define MAX_SUBS 20

typedef struct {
    const char    *name;
    task_coord_cb_t cb;
    void           *user;
    uint32_t        period_on_ms;
    uint32_t        period_off_ms;
    uint32_t        last_ms;
    // Diagnostics (always tracked; reading is cheap, no atomics needed since
    // updates only happen on the coordinator task).
    uint32_t        invocations;
    uint64_t        total_us;
    uint32_t        max_us;
} sub_t;

static sub_t                 s_subs[MAX_SUBS];
static int                   s_n_subs = 0;
static uint32_t              s_on_period_ms  = 50;
static uint32_t              s_off_period_ms = 1000;
static task_coord_active_fn  s_active_fn     = NULL;
static bool                  s_started       = false;

static void task_coord_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "task_coordinator started: on=%ums off=%ums subs=%d",
             (unsigned)s_on_period_ms, (unsigned)s_off_period_ms, s_n_subs);

    while (1) {
        bool on = s_active_fn ? s_active_fn() : true;
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        for (int i = 0; i < s_n_subs; i++) {
            uint32_t period = on ? s_subs[i].period_on_ms : s_subs[i].period_off_ms;
            if (period == 0) continue;
            if ((now - s_subs[i].last_ms) >= period) {
                int64_t t0 = esp_timer_get_time();
                s_subs[i].cb(s_subs[i].user);
                uint32_t elapsed = (uint32_t)(esp_timer_get_time() - t0);
                s_subs[i].invocations++;
                s_subs[i].total_us += elapsed;
                if (elapsed > s_subs[i].max_us) s_subs[i].max_us = elapsed;
                s_subs[i].last_ms = now;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(on ? s_on_period_ms : s_off_period_ms));
    }
}

void task_coord_init(uint32_t on_period_ms, uint32_t off_period_ms,
                     task_coord_active_fn active_state_fn)
{
    if (s_started) {
        ESP_LOGW(TAG, "already initialized");
        return;
    }
    if (on_period_ms == 0)  on_period_ms = 50;
    if (off_period_ms == 0) off_period_ms = 1000;
    s_on_period_ms  = on_period_ms;
    s_off_period_ms = off_period_ms;
    s_active_fn     = active_state_fn;
    s_started       = true;
    // Task creation deferred to task_coord_start() so the caller can finish
    // registering all subscribers before the task runs on the other core.
}

void task_coord_start(void)
{
    // 8 KB stack: subscribers may drive an LVGL render via lv_refr_now()
    // (e.g. wake_button_cb → display_manager_turn_on → synchronous repaint
    // before backlight). The full LVGL render pipeline + flush callback runs on
    // this task (wake path calls lv_refr_now), so keep generous headroom.
    // Right-sized from measured high-water (~4.4 KB peak incl. a wake render) —
    // re-measure (Run Tests Stack/heap report) before shrinking. Must stay
    // INTERNAL: runs rtc_minute_sync NVS commit.
    BaseType_t rc = xTaskCreate(task_coord_task, "task_coord", 7168, NULL, 4, NULL);
    if (rc != pdPASS) {
        ESP_LOGE(TAG, "task creation failed");
    }
}

void task_coord_subscribe(const char *name,
                          task_coord_cb_t cb, void *user,
                          uint32_t period_when_on_ms,
                          uint32_t period_when_off_ms)
{
    if (!cb) {
        ESP_LOGE(TAG, "subscribe(%s): null callback", name ? name : "?");
        return;
    }
    if (s_n_subs >= MAX_SUBS) {
        ESP_LOGE(TAG, "subscribe(%s): MAX_SUBS=%d reached", name ? name : "?", MAX_SUBS);
        return;
    }
    s_subs[s_n_subs] = (sub_t){
        .name          = name,
        .cb            = cb,
        .user          = user,
        .period_on_ms  = period_when_on_ms,
        .period_off_ms = period_when_off_ms,
        .last_ms       = 0,
    };
    ESP_LOGI(TAG, "subscribed[%d] %s (on=%ums off=%ums)",
             s_n_subs, name ? name : "?",
             (unsigned)period_when_on_ms, (unsigned)period_when_off_ms);
    s_n_subs++;
}

void task_coord_dump_stats(void)
{
    ESP_LOGI(TAG, "--- task_coordinator subscriber stats ---");
    ESP_LOGI(TAG, "%-20s %-7s %-7s %-9s %-9s %-9s",
             "name", "on_ms", "off_ms", "calls", "avg_us", "max_us");
    for (int i = 0; i < s_n_subs; i++) {
        uint32_t avg = s_subs[i].invocations
                     ? (uint32_t)(s_subs[i].total_us / s_subs[i].invocations)
                     : 0;
        ESP_LOGI(TAG, "%-20s %-7u %-7u %-9u %-9u %-9u",
                 s_subs[i].name ? s_subs[i].name : "?",
                 (unsigned)s_subs[i].period_on_ms,
                 (unsigned)s_subs[i].period_off_ms,
                 (unsigned)s_subs[i].invocations,
                 (unsigned)avg,
                 (unsigned)s_subs[i].max_us);
    }
}
