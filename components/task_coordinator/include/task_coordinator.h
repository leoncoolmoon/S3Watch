#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*task_coord_cb_t)(void *user);

/* Returns true when the coordinator should run subscribers at the "on" cadence
 * (typically: the display is on). Passed in at init time so the coordinator
 * stays free of any concrete display/manager dependency. */
typedef bool (*task_coord_active_fn)(void);

/* Initialize the coordinator parameters. Does NOT create the task yet.
 * Call ONCE during boot, before any task_coord_subscribe() calls.
 * After all subscribers are registered, call task_coord_start() to launch
 * the task — this avoids a race where the task runs before all subscribers
 * are registered on the other core.
 */
void task_coord_init(uint32_t on_period_ms, uint32_t off_period_ms,
                     task_coord_active_fn active_state_fn);

/* Create and start the coordinator task. Call after all task_coord_subscribe()
 * calls to guarantee the task sees the complete subscriber list on its first
 * iteration.
 */
void task_coord_start(void);

/* Register a periodic callback. period_when_on_ms=0 means "skip when display
 * is on", same for off. Periods are rounded up to the nearest base tick.
 *
 * Subscribers run sequentially on the coordinator task, in registration order.
 * Keep callbacks fast and non-blocking; if you need LVGL, dispatch via
 * lv_async_call(). The fixed subscriber slot count is small (see source);
 * subscriptions are permanent — there is no unsubscribe.
 *
 * NOT thread-safe to call after task_coord_init() — subscribe everything
 * during boot init.
 */
void task_coord_subscribe(const char *name,
                          task_coord_cb_t cb, void *user,
                          uint32_t period_when_on_ms,
                          uint32_t period_when_off_ms);

/* Dump per-subscriber stats (invocations, total CPU time, max single-call time)
 * to the ESP-IDF log. Cheap to call. Useful for tuning subscriber cadences and
 * catching slow callbacks.
 */
void task_coord_dump_stats(void);

#ifdef __cplusplus
}
#endif
