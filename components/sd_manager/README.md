# sd_manager

Centralises SD card mount/unmount lifetime behind a reference-counted API so multiple components can safely share the card without coordinating directly.

## Problem it solves

The SD card has no double-mount guard in the BSP. Before this component, each consumer (`sd_logger`, `music_player`, `settings`) independently checked `bsp_sdcard == NULL` before calling `bsp_sdcard_mount()`. That pattern is not thread-safe and leaks mount state across components — if any two ran concurrently the card would be mounted twice or unmounted while another consumer still had files open.

## How it works

A mutex-protected refcount tracks how many callers have acquired the card:

- First `acquire()` calls `bsp_sdcard_mount()` and increments the count.
- Subsequent `acquire()` calls just increment — no second mount.
- Each `release()` decrements. When the count reaches zero the card is unmounted.
- A failed mount leaves the count at zero and returns the BSP error to the caller.

## Usage

### Initialisation

Call once from `app_main()` before any component touches the SD card:

```c
settings_init();
sd_manager_init();   // before sd_logger_init() and music_player use
sd_logger_init();
```

### Permanent hold (sd_logger, music_player)

Acquire once at init and never release — the card stays mounted for the boot session:

```c
if (sd_manager_acquire() != ESP_OK) {
    // card absent or unreadable — handle gracefully
    return;
}
// ... card available for the rest of boot
```

### Transient access (settings backup/restore)

Acquire around each operation:

```c
if (sd_manager_acquire() != ESP_OK) return false;
// ... read or write the backup file ...
sd_manager_release();
```

If another component already holds the card the refcount just goes up and back down — no extra mount or unmount occurs.

## Dependencies

`esp32_s3_touch_amoled_2_06` (BSP — `bsp_sdcard_mount` / `bsp_sdcard_unmount`)
