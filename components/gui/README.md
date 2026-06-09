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
- **App tile** (column 2, row 2): similar pattern; used for full apps (music player, SignalK dashboard, stopwatch, etc.).
- **Dynamic subtile** (column 3, row 1): secondary content to the right of the dynamic tile.

## Screens and apps

| Screen / App | Purpose |
|-------------|---------|
| `watchface` | Main watch face (digital/analog, configurable) |
| `alarm_clock` | Alarm set/manage |
| `music_app` | Music player UI (catalog browse, now-playing) |
| `signalk_dashboard` | Live marine instrument gauges |
| `signalk_alerts` | Active SignalK alarm list |
| `stopwatch` | Stopwatch |
| `world_clock` | World clock |
| `calendar_app` | Calendar view |
| `storage_file_explorer` | Browse SD card files |
| `settings_*` | Settings screens (brightness, sound, timeout, timezone, WiFi, SignalK, backup, etc.) |
| `setting_ondev_test_screen` | On-device test runner UI |

## Initialization

`ui_task()` runs as a dedicated FreeRTOS task. It calls `bsp_display_start()`, creates all tile structure, calls `display_manager_init()`, then enters the LVGL port event loop.

## Dependencies

`display_manager`, `power_manager`, `settings`, `audio_alert`, `music_player`, `signalk_client`, `ntp_sync`, `wifi_manager`, `sensors`, `ondev_test`, `bsp_extra`
