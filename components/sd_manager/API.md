# sd_manager API

## Functions

### `sd_manager_init`
```c
void sd_manager_init(void);
```
Create the internal mutex and zero the refcount. Must be called once from `app_main()` before any other `sd_manager_*` function.

---

### `sd_manager_acquire`
```c
esp_err_t sd_manager_acquire(void);
```
Increment the mount refcount. Calls `bsp_sdcard_mount()` when the count transitions from 0 → 1. Subsequent calls just increment — no second mount is attempted.

Returns `ESP_OK` on success. On mount failure the refcount is left unchanged and the BSP error code is returned; the caller should treat the SD card as unavailable.

---

### `sd_manager_release`
```c
void sd_manager_release(void);
```
Decrement the mount refcount. Calls `bsp_sdcard_unmount()` when the count transitions from 1 → 0. No-op if the count is already zero.

---

### `sd_manager_is_mounted`
```c
bool sd_manager_is_mounted(void);
```
Returns `true` when the refcount is greater than zero (card is mounted and at least one holder is active). Safe to call from any task.
