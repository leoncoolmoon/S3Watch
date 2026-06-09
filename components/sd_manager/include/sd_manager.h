#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialise the SD manager. Must be called once before any other sd_manager_*
// function, before the RTOS scheduler starts or from the main task early in boot.
void sd_manager_init(void);

// Increment the mount refcount. Mounts the SD card on the first acquire.
// Returns ESP_OK on success, or the BSP mount error on failure (refcount unchanged).
esp_err_t sd_manager_acquire(void);

// Decrement the mount refcount. Unmounts the SD card when the count reaches zero.
void sd_manager_release(void);

// True when the refcount is > 0 (card is mounted and at least one holder active).
bool sd_manager_is_mounted(void);

#ifdef __cplusplus
}
#endif
