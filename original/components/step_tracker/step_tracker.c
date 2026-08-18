// step_tracker — persisted step stats (lifetime/daily/weekly + records) on top of
// imu_manager's software step counter. See step_tracker.h.
//
// Flash-wear strategy: the live state is a small struct held in RAM. It is committed
// to NVS (wear-leveled) only (a) at a local-midnight rollover and (b) a throttled
// commit at most every SAVE_THROTTLE_MS while steps are accruing — so a handful of
// NVS writes per active day, never a per-step write. Separately, the rollover writes
// a daily snapshot into settings.json (one debounced SPIFFS write/day), which the
// existing Settings → Backup-to-SD carries. On a wiped-NVS boot (fresh flash /
// restored backup) we seed from that settings.json snapshot.

#include "step_tracker.h"
#include "imu_manager.h"
#include "settings.h"
#include "esp_log.h"
#include "nvs.h"
#include "utc_tm_to_epoch.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <time.h>
#include <string.h>

static const char *TAG = "STEP_TRK";

#define NVS_NS            "step_trk"
#define NVS_KEY           "stats"
#define STATS_MAGIC       0x53544B31u          // "STK1" — bump if struct layout changes
#define SAVE_THROTTLE_MS  (15 * 60 * 1000)     // ≥15 min between throttled NVS commits

static step_stats_t      s;                    // live working state
static bool              s_loaded   = false;
static bool              s_dirty    = false;
static TickType_t        s_last_save = 0;
static SemaphoreHandle_t s_lock     = NULL;

// Days since 1970 in LOCAL time (TZ already applied by settings' setenv/tzset).
// localtime_r gives the local civil date; running it back through the days-from-
// civil helper yields the local-midnight day number (newlib here has no tm_gmtoff).
static int32_t local_epoch_day(void) {
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    return (int32_t)(utc_tm_to_epoch(&lt) / 86400);
}

// today + the previous 6 local days held in the ring (missing days count as 0).
static uint32_t week_total(void) {
    uint32_t sum = s.today;
    for (int i = 0; i < STEP_HIST_DAYS; i++) {
        int32_t d = s.hist_day[i];
        if (d >= s.today_day - 6 && d <= s.today_day - 1) sum += s.hist_steps[i];
    }
    return sum;
}

static void refresh_records(void) {
    if (s.today > s.best_day) s.best_day = s.today;
    uint32_t wk = week_total();
    if (wk > s.best_week) s.best_week = wk;
}

// Write a snapshot to NVS. MUST run with s_lock RELEASED — an NVS commit can take
// tens of ms (longer on a page compaction), and holding s_lock across it would let a
// getter on the UI task (which holds the LVGL lock) block long enough to stall the
// wake path's lv_refr_now() on the task_coordinator task. So callers snapshot the
// struct under the lock, release, then call this.
static void save_nvs(const step_stats_t *snap) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_blob(h, NVS_KEY, snap, sizeof(*snap)) == ESP_OK) nvs_commit(h);
    nvs_close(h);
    ESP_LOGD(TAG, "NVS commit (lifetime=%lu today=%lu)",
             (unsigned long)snap->lifetime, (unsigned long)snap->today);
}

// Apply a pending local-midnight rollover — ARITHMETIC ONLY, no flash (caller holds
// s_lock). Returns true if the day changed. Persistence is the caller's job, done
// outside the lock. A rollover that isn't persisted before a power loss is harmless:
// the boot path re-derives it idempotently from the stored today_day.
//
// Forward-only: a BACKWARD day change (an NTP correction stepping a fast clock
// back across midnight, or a TZ change) must NOT roll — that would archive
// today under the wrong day and zero it. Instead keep accumulating into the
// current day; the real date catches up naturally and the next forward
// boundary rolls as normal.
static bool rollover_apply(void) {
    if (!s_loaded) return false;
    int32_t cur = local_epoch_day();
    if (cur <= s.today_day) return false;
    if (s.today_day != 0) {                    // archive the day that just ended
        s.hist_day[s.hist_head]   = s.today_day;
        s.hist_steps[s.hist_head] = s.today;
        s.hist_head = (uint8_t)((s.hist_head + 1) % STEP_HIST_DAYS);
    }
    s.today = 0;
    s.today_day = cur;
    refresh_records();
    s_dirty = true;
    ESP_LOGI(TAG, "day rollover → epoch-day %ld", (long)cur);
    return true;
}

// imu_manager step callback (runs on the FIFO-drain / task_coordinator task; delta
// > 0 only). Holds s_lock only for the arithmetic; any flash write happens after.
static void on_steps(uint32_t delta) {
    step_stats_t snap = {0};
    bool do_save = false, do_mirror = false;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool rolled = rollover_apply();
    s.today    += delta;
    s.lifetime += delta;
    refresh_records();
    s_dirty = true;
    if (rolled) {
        do_save = do_mirror = true;            // boundary: NVS + daily settings.json mirror
    } else if ((TickType_t)(xTaskGetTickCount() - s_last_save) >= pdMS_TO_TICKS(SAVE_THROTTLE_MS)) {
        do_save = true;                        // throttled checkpoint
    }
    if (do_save) { snap = s; s_last_save = xTaskGetTickCount(); s_dirty = false; }
    xSemaphoreGive(s_lock);

    if (do_save)   save_nvs(&snap);            // flash — OUTSIDE the lock
    if (do_mirror) settings_set_step_stats(&snap);
}

void step_tracker_init(void) {
    if (s_loaded) return;
    s_lock = xSemaphoreCreateMutex();

    // Prefer the NVS working store; fall back to the settings.json snapshot (a
    // restored SD backup or fresh flash), else start zeroed.
    bool ok = false;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(s);
        if (nvs_get_blob(h, NVS_KEY, &s, &len) == ESP_OK &&
            len == sizeof(s) && s.magic == STATS_MAGIC) {
            ok = true;
        }
        nvs_close(h);
    }
    if (!ok) {
        step_stats_t seed;
        settings_get_step_stats(&seed);
        if (seed.magic == STATS_MAGIC) {
            s = seed; ok = true;
            ESP_LOGI(TAG, "seeded step stats from settings.json");
        }
    }
    if (!ok) { memset(&s, 0, sizeof(s)); s.magic = STATS_MAGIC; }

    s_loaded = true;
    s_last_save = xTaskGetTickCount();
    // Finalize any day(s) passed while powered off. Single-threaded here (the step
    // callback isn't registered yet), so the flash writes need no snapshot dance.
    if (rollover_apply()) { save_nvs(&s); settings_set_step_stats(&s); }
    imu_manager_set_step_cb(on_steps);
    ESP_LOGI(TAG, "ready (lifetime=%lu today=%lu best_day=%lu best_week=%lu)",
             (unsigned long)s.lifetime, (unsigned long)s.today,
             (unsigned long)s.best_day, (unsigned long)s.best_week);
}

void step_tracker_reset_all(void) {
    if (!s_loaded) return;
    step_stats_t snap;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memset(&s, 0, sizeof(s));
    s.magic = STATS_MAGIC;
    s.today_day = local_epoch_day();
    snap = s;
    s_last_save = xTaskGetTickCount();
    s_dirty = false;
    xSemaphoreGive(s_lock);
    save_nvs(&snap);                           // flash OUTSIDE the lock
    settings_set_step_stats(&snap);
    ESP_LOGI(TAG, "all step stats reset");
}

// Getters — each lazily applies a pending rollover (arithmetic only — no flash under
// the lock, so a UI-task read never stalls the wake path) so "today" is correct on
// read. The rollover persists on the next step callback or is re-derived at boot.
#define GETTER(name, expr) \
    uint32_t name(void) { \
        if (!s_loaded) return 0; \
        xSemaphoreTake(s_lock, portMAX_DELAY); \
        rollover_apply(); \
        uint32_t v = (expr); \
        xSemaphoreGive(s_lock); \
        return v; \
    }
GETTER(step_tracker_today,     s.today)
GETTER(step_tracker_week,      week_total())
GETTER(step_tracker_lifetime,  s.lifetime)
GETTER(step_tracker_best_day,  s.best_day)
GETTER(step_tracker_best_week, s.best_week)
