#include <stdio.h>
#include <stdbool.h>
#include "esp_heap_caps.h"

bool hw_test_heap(char *detail, size_t n) {
    size_t int_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t int_min  = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    size_t int_lrg  = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    size_t psr_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    snprintf(detail, n, "int=%uKB free, min=%uKB, largest=%uKB; PSRAM=%uKB free",
             (unsigned)(int_free / 1024),
             (unsigned)(int_min  / 1024),
             (unsigned)(int_lrg  / 1024),
             (unsigned)(psr_free / 1024));
    // Thresholds tuned to the steady-state baseline observed at runtime.
    return int_free > 80 * 1024 && int_lrg > 16 * 1024;
}
