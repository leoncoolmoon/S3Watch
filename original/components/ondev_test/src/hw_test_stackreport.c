// Diagnostic: dump per-task stack high-water marks + internal/PSRAM heap info
// to the serial log. Drives the internal-RAM right-sizing work — a large
// "free_stack" means that task's stack is over-allocated and can be trimmed;
// the heap_caps_print_heap_info dumps show the actual top internal consumers.
// Detail string is tiny, so the real output goes to the log (like coord_stats).

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "STACKREPORT";

bool hw_test_stackreport(char *detail, size_t n) {
#if (configUSE_TRACE_FACILITY == 1)
    UBaseType_t count = uxTaskGetNumberOfTasks();
    // Hold the snapshot in PSRAM so the measurement itself doesn't perturb
    // internal RAM (which is exactly what we're trying to measure).
    TaskStatus_t *arr = heap_caps_malloc((size_t)count * sizeof(TaskStatus_t),
                                         MALLOC_CAP_SPIRAM);
    if (!arr) { snprintf(detail, n, "alloc failed"); return false; }

    UBaseType_t got = uxTaskGetSystemState(arr, count, NULL);
    ESP_LOGI(TAG, "==== per-task stack high-water (min free bytes ever; lower = tighter) ====");
    size_t sum_free = 0;
    for (UBaseType_t i = 0; i < got; i++) {
        // usStackHighWaterMark = min free stack ever, in words (StackType_t = 4 B)
        unsigned free_b = (unsigned)arr[i].usStackHighWaterMark * sizeof(StackType_t);
        sum_free += free_b;
        ESP_LOGI(TAG, "  %-16s free_stack=%5u B", arr[i].pcTaskName, free_b);
    }
    heap_caps_free(arr);

    ESP_LOGI(TAG, "==== heap_caps_print_heap_info(INTERNAL) ====");
    heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "==== heap_caps_print_heap_info(SPIRAM) ====");
    heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);

    snprintf(detail, n, "%u tasks, sum free-stack=%uKB; dumped to log",
             (unsigned)got, (unsigned)(sum_free / 1024));
    return true;
#else
    (void)TAG;
    snprintf(detail, n, "needs CONFIG_FREERTOS_USE_TRACE_FACILITY=y");
    return false;
#endif
}
