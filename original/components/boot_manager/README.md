# boot_manager

Owns the entire power-on sequence and the boot UX, then passes off control:

**power on → boot splash + boot sound → watchface / normal operation**

`main.cpp` is a thin shim that calls `boot_manager_run()`; everything that used
to live in `app_main` is staged here.

## Stages

| Stage | What | Notes |
|-------|------|-------|
| 0 | Core HW | cJSON→PSRAM hooks, NVS init + corruption recovery, event loop, I2C, AXP2101, `power_manager_init` (rails + PM lock + task_coord init), `audio_manager_init` |
| 1 | Display + splash | `bsp_display_start()` → `boot_splash_show()` (gui): wordmark + spinner + firmware version, rendered synchronously so it's visible ~immediately |
| 2 | Settings + tone | `settings_init()` (sound flag/volume, TZ, RTC), then the boot tone starts **during the splash** with a completion semaphore (`audio_alert_play_startup(done)`) |
| 3 | Services | alarm, sd_manager/sd_logger, wifi/ntp/signalk init, radio released (first NTP sync ~30 s in via `ntp_sync_check`'s boot-quiet gate), IMU + step_tracker |
| 4 | UI build | `ui_task` builds the tileview **behind the splash** (no auto-load) and gives a ready semaphore |
| 5 | Handoff | wait for UI-ready **and** tone-done **and** ≥1.2 s splash minimum (10 s safety cap on each wait), then `boot_splash_handoff()` slides the watchface up over the splash |

## Why the tone plays during the splash

The boot tone streams MP3 from SPIFFS with only the ~33 ms I2S DMA as buffer —
it used to fire after the watchface was live, mid-boot-storm, and stuttered.
In the staged sequence it plays in the calm window: radio off, no flash
writes, watchface not yet rendering. Sound disabled → the semaphore is given
immediately and the splash just shows for its 1.2 s minimum.

## Ordering contracts honored

- Every `task_coordinator` subscriber registers before `task_coord_start()`
  (stage 2: settings_save; stage 3: alarm_check, ntp_watchdog, step_sampler;
  stage 4: the display/UI subscribers, registered inside `display_manager_init`
  immediately before it starts the coordinator).
- `settings_init` precedes every settings consumer; `step_tracker_init` follows
  settings + the imu_manager setup.
- The splash is boot-only — wake-from-sleep goes through display_manager's
  pre-show hook and never sees it.

## Dependencies

`gui` (splash + ui_task), `audio_alert` (tone), and everything `app_main`
historically initialised: power/audio/alarm/settings/sd/wifi/ntp/signalk/imu/
step_tracker/bsp_extra/BSP.
