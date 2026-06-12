# gui

All LVGL screens, apps, and UI navigation for S3Watch. Built on top of LVGL's tileview system with three distinct tile regions.

## Navigation model

The UI is a 2D tileview. At boot, the watchface tile is active. Swipe gestures navigate between tiles; some tiles are acquired/released dynamically.

```
[Watchface]  →  [Controls]   →  [Dynamic tile (settings/alerts/etc)]
                    ↓
              [App Picker]   →  [App tile (music, signalk, etc)]
```

- **Dynamic tile** (column 2, row 1): single-use; acquired with `ui_dynamic_tile_acquire()`, shown with `ui_dynamic_tile_show()`, cleaned up with `ui_dynamic_tile_close()`. Used for settings screens, alert lists, and other transient content.
- **App tile** (column 2, row 2): similar pattern; used for full apps (music player, SignalK dashboard, stopwatch, calculator, etc.).
- **Dynamic subtile** (column 3, row 1): secondary content to the right of the dynamic tile.

## Screens and apps

| Screen / App | Purpose |
|-------------|---------|
| `watchface` | Main watch face (digital/analog, configurable) |
| `alarm_clock` | Alarm set/manage |
| `music_app` | Music player UI (catalog browse, now-playing, image-based transport controls) |
| `signalk_dashboard` | Live marine instrument gauges |
| `signalk_alerts` | Active SignalK alarm list |
| `stopwatch` | Stopwatch |
| `world_clock` | World clock |
| `calendar_app` | Calendar view |
| `calculator_app` | Four-function calculator (iOS-style layout) |
| `step_app` | Step counter — today's count (big) plus week / lifetime totals and best-day / best-week records, from `step_tracker`; gated by the Settings toggle |
| `storage_file_explorer` | Browse SD card files |
| `settings_*` | Settings screens (brightness, sound, timeout, timezone, WiFi, SignalK, backup, etc.) |
| `setting_ondev_test_screen` | On-device test runner UI |

## Initialization

`ui_task()` runs as a dedicated FreeRTOS task spawned by `boot_manager` (stage 4): it builds the tile structure **behind the boot splash** (the main screen is deliberately not loaded), calls `display_manager_init()` (which starts the task_coordinator), wires power events + back button, gives the ready semaphore passed via `pvParameters`, and self-deletes. `boot_manager` then performs the splash→watchface handoff.

## Boot splash (`boot_splash.c`)

Boot-only screen shown by `boot_manager` between `bsp_display_start()` and the watchface: "S3Watch" wordmark, LVGL spinner, firmware version (`esp_app_get_description()`). `boot_splash_show()` renders it synchronously so it's visible the moment the backlight is up; `boot_splash_handoff()` slides the built tileview up over it (`LV_SCR_LOAD_ANIM_OVER_TOP`, 300 ms — the back-nav motion) and deletes the splash via a one-shot timer after the animation lands. Never appears on wake-from-sleep.

## Music app navigation

`music_app` manages a 4-level browse hierarchy (Artist → Album → Track → Now Playing) inside a single app tile. Key design notes:

- **In-app back button** — a `<` button in the header pops one browse level. Inset 50 px from the left edge to clear the AMOLED's rounded bezel corners. Hidden at the Artist root level. The hardware back gesture closes the whole app from any depth (playback continues regardless).
- **Browse row truncation** — row labels are pre-truncated to 24 characters before display to prevent long metadata tags from wrapping inside the fixed 56 px row height.
- **Transport controls** — Now Playing shows four 64×64 round buttons (Shuffle / Prev / Play-Pause / Next). Icons are compiled LVGL image descriptors (`LV_COLOR_FORMAT_RGB565A8`, 32×32 px) in `icons/image_ctrl_shuffle.c`, `image_ctrl_prev.c`, `image_ctrl_play.c`, `image_ctrl_pause.c`, `image_ctrl_next.c`. The play/pause button swaps its image source on each 500 ms refresh tick.

## Display geometry

The panel is a **480×480 round AMOLED**. The four corners of the pixel grid are physically obscured by the circular bezel — content placed there is clipped and untappable.

**Safe zone:** approximately a circle with radius ~230 px centred at (240, 240). The corners within roughly 50 px of each screen corner are dead.

Rules of thumb:
- Buttons at the left/right edge near the vertical midpoint (y ≈ 240) are safe — the equator is the widest point.
- Near the top and bottom edges, keep interactive elements at least ~50 px from the left and right edges.
- The music app back button (`LV_ALIGN_LEFT_MID, 50, 0`) is the established reference for edge-adjacent controls.
- Full-width flex rows spanning 100% are fine for mid-screen rows; add at least 8 px horizontal padding for rows near the top or bottom to keep content clear of the clipped corners.

## Dependencies

`display_manager`, `power_manager`, `settings`, `audio_alert`, `music_player`, `signalk_client`, `ntp_sync`, `wifi_manager`, `imu_manager`, `step_tracker`, `ondev_test`, `bsp_extra`
