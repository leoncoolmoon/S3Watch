// Probe the SD card slot: attempt acquire, report present/absent.
// Always returns true (pass) — absence of an SD card is not a failure.

#include <stdio.h>
#include <stdbool.h>
#include "esp_err.h"
#include "sd_manager.h"

bool hw_test_sd(char *detail, size_t n) {
    esp_err_t err = sd_manager_acquire();
    if (err != ESP_OK) {
        snprintf(detail, n, "absent (0x%x)", (unsigned)err);
        return true;
    }
    sd_manager_release();
    snprintf(detail, n, "card present, mount OK");
    return true;
}
