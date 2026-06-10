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
    // `largest` is the canary that matters: the LCD SPI flush bounces its color
    // data through a contiguous internal DMA buffer of ~13 KB (410 *
    // BSP_DISPLAY_LVGL_BUF_HEIGHT=16 * 2) — the draw buffer is in PSRAM (BSP fork)
    // but PSRAM isn't esp_ptr_dma_capable(), so spi_master still bounces. >16 KB
    // keeps ~3 KB margin and fails *before* flushes start failing. `int_free`
    // (>30 KB) is the general-health gauge. (80 KB free is unreachable here.)
    return int_free > 30 * 1024 && int_lrg > 16 * 1024;
}
