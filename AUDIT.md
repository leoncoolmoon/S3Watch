# Code Audit: S3Watch Project

**Audited:** 2026-06-05  
**IDF version:** 5.5.4  
**Target:** ESP32-S3, LVGL 9.3.0  
**Scope:** All project-local components + `main/`. BSP managed component has its own `managed_components/esp32_s3_touch_amoled_2_06/AUDIT.md`.

---

## Summary

| Severity | Count |
|----------|-------|
| High     | 3     |
| Medium   | 6     |
| Low      | 6     |
| **Total**| **15**|

---

## High

### F1 — `bsp_rtc_init` drops I2C device-registration failure → NULL-handle crash
**File:** [components/bsp_extra/src/bsp_board_extra.c](components/bsp_extra/src/bsp_board_extra.c) line 40

```c
i2c_master_bus_add_device(bus_handle, &dev_config, &rtc_dev_handle);
return ESP_OK;  // always succeeds even if add failed
```

`rtc_dev_handle` stays `NULL` on failure. Every subsequent `rtc_register_read` / `rtc_register_write` then calls `i2c_master_transmit_receive(NULL, …)` — guaranteed crash.  
**Fix:** `ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(…), TAG, "RTC device add");`

---

### F2 — `gmtime()` (non-thread-safe) in `nvs_load_time` instead of `gmtime_r`
**File:** [components/bsp_extra/src/rtc_lib.c](components/bsp_extra/src/rtc_lib.c) line 50

```c
struct tm *lt = gmtime(&t_val);
*out = *lt;
```

`gmtime()` returns a pointer to a process-global static buffer. If another task calls any time function concurrently, the buffer is overwritten before the copy. The rest of rtc_lib.c correctly uses `gmtime_r`.  
**Fix:** `if (!gmtime_r(&t_val, out)) return false;`

---

### ST1 — `portMAX_DELAY` in `signalk_client_stop()` can freeze the task coordinator
**File:** [components/signalk_client/src/signalk_client.c](components/signalk_client/src/signalk_client.c) line 433

`on_display_pre_off()` → `signalk_client_stop()` → `xSemaphoreTake(s_lifecycle_mux, portMAX_DELAY)`. This runs on the task-coordinator task. If `open_ws()` holds the lifecycle mutex while waiting for `esp_websocket_client_start()` (up to `network_timeout_ms = 5000 ms`), the coordinator is frozen: no `wake_button_cb`, no `inactivity_check_cb`, no subscribers run at all.  
**Fix:** Use `pdMS_TO_TICKS(300)` and proceed on timeout.

---

## Medium

### F3 — `pcf85063a_init()` called twice on boot
`bsp_extra_init()` calls `pcf85063a_init()` (bsp_board_extra.c:121), then `settings_init()` → `rtc_start()` → `pcf85063a_init()` is called again. Remove the call from `bsp_extra_init()` — `rtc_start()` owns RTC initialization.

### F4 — `ESP_ERROR_CHECK` inside `wifi_manager_connect()` panics device on failure
**File:** [components/wifi_manager/src/wifi_manager.c](components/wifi_manager/src/wifi_manager.c) line 170  
Replace `ESP_ERROR_CHECK(esp_wifi_set_config(…))` with `ESP_RETURN_ON_ERROR`.

### F5 — SPIFFS mount-failure retry without intermediate unregister
**File:** [components/settings/settings.c](components/settings/settings.c) line 103  
If the first `esp_vfs_spiffs_register` call partially registers the VFS before returning an error, the second call (with `format_if_mount_failed=true`) fails with `ESP_ERR_INVALID_STATE`. Call `esp_vfs_spiffs_unregister(SETTINGS_PARTITION)` before retrying.

### F6 — WAV chunk-size overflow before bounds check
**File:** [components/audio_alert/src/audio_alert.c](components/audio_alert/src/audio_alert.c) line 129  
`data_offset += 8 + sz` can overflow `uint32_t` before `if (data_offset + 8 > st.st_size)` is checked. Check bounds before adding.

### ST2 — Race: coordinator task starts before all subscribers registered
**File:** [components/task_coordinator/src/task_coordinator.c](components/task_coordinator/src/task_coordinator.c) line 78  
`task_coord_init()` creates the task, which can immediately run on Core 1 while `display_manager_init()` is still calling `task_coord_subscribe()` on Core 0. No mutex protects `s_subs[]` / `s_n_subs`.  
**Fix:** Split into `task_coord_init()` (setup) and `task_coord_start()` (task creation), called after all subscribes.

### ST3 — `current_time` struct shared between tasks without a lock
**File:** [components/bsp_extra/src/rtc_lib.c](components/bsp_extra/src/rtc_lib.c)  
`current_time` (9-word `struct tm`) is written by the task coordinator (`rtc_sync_from_hw`, `rtc_refresh_now`) and read by the LVGL task (`rtc_get_hour` etc.) without synchronization. On dual-core ESP32-S3 a partial read is possible.  
**Fix:** Guard writes and reads with a `portMUX_TYPE` spinlock.

### ST4 — 16 KB static buffer permanently occupies internal SRAM
**File:** [components/audio_alert/src/audio_alert.c](components/audio_alert/src/audio_alert.c) line 209  
`static int16_t buf[8192]` consumes 16,384 bytes of `.bss` permanently. Boot log showed 26 KB minimum free internal heap — this is 63% of that headroom.  
**Fix:** Allocate from PSRAM with `heap_caps_malloc(N * sizeof(int16_t), MALLOC_CAP_SPIRAM)` and free after use.

---

## Low

### S1 — WiFi credentials stored in plaintext NVS *(accepted — embedded system limitation)*
### S2 — SignalK uses unencrypted `ws://` *(accepted — trusted vessel LAN)*

### S3 — Unaligned pointer casts in WAV header parser (UB)
**File:** [components/audio_alert/src/audio_alert.c](components/audio_alert/src/audio_alert.c) line 134  
`*(uint16_t*)(hdr + 20)` etc. Use `memcpy` into properly-aligned locals.

### R1 — Unused deprecated `#include "driver/i2c.h"`
**File:** [components/bsp_extra/src/bsp_board_extra.c](components/bsp_extra/src/bsp_board_extra.c) line 7  
Nothing in the file uses the legacy I2C API. Remove.

### R2 — 17-line commented-out LVGL logger in `main.cpp`
**File:** [main/main.cpp](main/main.cpp) lines 26–42  
Dead code; remove or wrap in `#if CONFIG_LV_USE_LOG`.

### R3 — `apply_defaults()` duplicates static initializers in settings.c
**File:** [components/settings/settings.c](components/settings/settings.c) line 384  
Default values for settings defined in two places; must be kept in sync manually.

### R4 — `bsp_display_brightness_set()` called twice in `settings_init()`
**File:** [components/settings/settings.c](components/settings/settings.c) lines 244 and 253

### R5 — `3.14159265f` literal instead of `M_PI`
**File:** [components/audio_alert/src/audio_alert.c](components/audio_alert/src/audio_alert.c) line 242

### R6 — Trivial "read/write function using new API" comments
**File:** [components/bsp_extra/src/bsp_board_extra.c](components/bsp_extra/src/bsp_board_extra.c) lines 45, 55

---

## What's Working Well

- **Power management:** PM lock / light-sleep lifecycle, ALDO gating on display sleep.
- **NTP/WiFi lifecycle:** connect-sync-release pattern keeps radio off most of the time; correct one-shot task to avoid tearing down lwIP from within lwIP callback.
- **Settings debounce timer:** 10 s delay before flash write minimizes wear.
- **SignalK alert cache:** slot eviction, insertion-sort copy under lock, no heap allocation in critical section.
- **RTC fallback chain:** hardware → NVS → default 2025-01-01, minimum plausible timestamp gate.
- **Display dim/off cycle:** uses `lv_disp_get_inactive_time(NULL)` as single source of truth — all screens/tiles get correct inactivity tracking automatically.
- **task_coordinator:** clean pub/sub model eliminates ad-hoc timers scattered across components.

---

## Fix Priority Order

1. **[F1]** `bsp_rtc_init` ignored return → NULL crash
2. **[F2]** `gmtime` → `gmtime_r` in nvs_load_time
3. **[ST1]** `portMAX_DELAY` in signalk stop → coordinator freeze
4. **[F4]** `ESP_ERROR_CHECK` in wifi connect → device panic
5. **[ST2]** Coordinator task/subscriber race → split init/start
6. **[ST3]** `current_time` spinlock
7. **[ST4]** 16 KB static audio buffer → PSRAM alloc
8. **[F3]** Double `pcf85063a_init`
9. **[F5]** SPIFFS unregister before retry
10. **[F6]** WAV chunk-size overflow guard
11. Low items (R1–R6, S3)
