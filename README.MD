# S3Watch — Smartwatch Firmware for Waveshare ESP32-S3-Touch-AMOLED-2.06

Custom smartwatch firmware built on ESP-IDF 5.x and LVGL 9, targeting the [Waveshare ESP32-S3-Touch-AMOLED-2.06](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-2.06) development board.

Originally a general-purpose smartwatch; now also a passive viewer for boat data via [SignalK](https://signalk.org/) — swipe up for an app picker (SignalK dashboard, World Clock, Stopwatch, Alarm), swipe down for the boat's currently-active alarms/notifications.

<img src="ui_assets/s3watch_0.jpg" width="300"/> <img src="ui_assets/s3watch_1.jpg" width="300"/>

---

## Features

### Watch
- **Two watchfaces** — modular architecture under [components/gui/src/faces/](components/gui/src/faces/); each face is self-contained and adding face N is a single-file change.
- **Background selector** — swappable background images, configurable per session
- **12/24-hour mode** — AM/PM label shown/hidden automatically
- **POSIX time zones** — preset picker (US/Europe/Asia/Pacific/Australia); time is stored UTC and converted for display via `setenv("TZ", ...)` + `tzset()`
- **Hardware-backed RTC** — PCF85063A (UTC); ESP32 internal clock is the runtime time source (re-synced from the PCF on each wake), with NVS-backed persistence in case of power loss
- **Wi-Fi + NTP time sync** — connects on demand, syncs, then releases the radio; daily re-sync if Wi-Fi permission is enabled
- **Wi-Fi permission toggle** — master switch in Controls; when off, the radio never activates
- **Wi-Fi settings** — scan, connect via on-screen keyboard, NTP server entry

### App picker
- **App launcher tile** — swipe up from the watchface opens a 2×2 icon grid (background follows the active watchface background). Tapping an icon slides a dynamic full-screen app in from the right; swipe left or use Back to return to the picker. Currently four apps:
  - **SignalK** — the marine dashboard (below)
  - **World Clock** — scrollable list of cities with live local times and day-offset badges (handles DST correctly via POSIX TZ strings)
  - **Stopwatch** — start/stop/lap/reset with centisecond precision (`esp_timer_get_time()`), persists state across app open/close, up to 50 laps
  - **Alarm Clock** — single daily alarm with hour/minute rollers, on/off toggle, dismiss button; fires even while the display is asleep (wakes the display, plays the alert sound, repeats every 30 s until dismissed)

### SignalK marine extension
- **SignalK dashboard app** — launched from the app picker. 2×2 grid of live values in **American nautical units**:
  - HDG (°) — `navigation.headingMagnetic`
  - Depth (ft) — `environment.depth.belowTransducer`
  - SOG (kt) — `navigation.speedOverGround`
  - Wind (`45° P / 12 kt`) — `environment.wind.angle/speedApparent`
- **SignalK alerts tile** — swipe down from the watchface. Live list of currently-active `notifications.*` deltas, colour-coded by severity (`emergency`/`alarm`/`warn`/`alert`), sorted highest-first. Passive viewer only — the watch never originates alerts; the boat's alarm system is the source of truth.
- **Demand-only WiFi** — the radio is **off** in steady state. Entering the SignalK dashboard or the alerts tile brings WiFi up, sets `WIFI_PS_MIN_MODEM` (radio sleeps between beacons to extend battery), opens a WebSocket to the configured SignalK server, and subscribes. Leaving the tile **or** the display going to sleep tears everything down (`esp_wifi_stop`).
- **SignalK settings** — server address via four-roller IP picker (or hostname fallback via QWERTY keyboard), port via numeric keypad.

### Other UI
- **Settings menu** — brightness, display timeout, sound on/off, alert volume, set time (rollers), **time zone**, Wi-Fi, NTP server, **SignalK server/port**, time format, watchface style + background, storage, **Run Tests**, restore defaults
- **Control screen** — quick-access grid (brightness, silence, flashlight, battery, Wi-Fi, settings)
- **Flashlight** — full-screen white at 100% brightness; swipe right to exit
- **Audio alerts** — WAV/MP3 via ES8311 codec; synthesized fallback. Codec is closed between plays so it doesn't pin APB at 80 MHz.
- **Persistent settings** — JSON on a dedicated SPIFFS partition

---

## UI Navigation

The UI is a 2D tileview anchored on the watchface. All transitions are gesture-driven.

```
                       ┌────────────────────────────┐
                       │  SignalK Alerts (0,0)      │   swipe ↓ from watchface
                       └────────────────────────────┘
                                       ↑
                                  swipe down
                                       │
   ┌──────────────────────────────┐    ▼    ┌────────────────────────────┐
   │       Watchface (0,1)        │  swipe ▶│        Controls (1,1)      │   →  dynamic
   │           "home"             │ ◀──────│  brightness, silence, etc. │      Settings
   └──────────────────────────────┘  left   └────────────────────────────┘      tiles
                                       │
                                  swipe up
                                       ▼
                       ┌────────────────────────────┐         tap icon         ┌────────────────────────────┐
                       │     App Picker (0,2)       │ ───────────────────────▶ │   Dynamic App Tile (1,2)   │
                       │ SignalK · World Clock ·    │ ◀─────────────────────── │ SignalK / World Clock /    │
                       │ Stopwatch · Alarm          │   swipe left / Back      │ Stopwatch / Alarm Clock    │
                       └────────────────────────────┘                          └────────────────────────────┘
```

The app tile at (1,2) is created on demand (`ui_app_tile_acquire/show/close` in `ui_tileview.c`) and torn down via `lv_obj_del_async` whenever the tileview navigates away from it — mirroring the existing dynamic Settings sub-tile pattern at (2,1)/(3,1).

**Always return to the watchface on wake** — `display_manager`'s pre-show hook snaps the tileview back to (0,1) before the backlight comes on, so every wake starts at home.

---

## Power Management

The firmware maximises time in light sleep when the display is off, and lets dynamic frequency scaling (DFS) drop the CPU to 40 MHz between events when the display is on.

### Task coordinator

Rather than each subsystem running its own polling task/timer (which would wake the CPU at uncorrelated times and defeat light sleep), all periodic non-time-critical work runs as **subscribers on one shared task** — the `task_coordinator` component. It wakes on a single cadence, runs every due subscriber in one burst, then sleeps. Adding new periodic work is a one-line `task_coord_subscribe()` call.

Base cadence: **100 ms when the display is on, 1 s when off**. Current subscribers:

| Subscriber | Display ON | Display OFF | Job |
|---|---|---|---|
| `inactivity_check` | 100 ms | — | dim/off cycle (see below) |
| `wake_button` | — | 1 s | PMU power-key wake poll |
| `axp_state` | 3 s | 5 s | VBUS / charge / battery refresh + events |
| `rtc_minute_sync` | 60 s | — | PCF85063A → `settimeofday` → NVS checkpoint |
| `touch_idle` | 1 s | — | drop FT3168 to Monitor mode after 10 s idle |
| `pmu_back_btn` (gui) | 100 ms | — | PMU short-press → Back action |
| `ui_battery_refresh` (gui) | 30 s | — | refresh watchface battery indicator |
| `signalk_dash` (gui) | 1 s | — | pull cached SignalK values into the dashboard labels |
| `signalk_alerts` (gui) | 1 s | — | repaint the alerts list from the active-alert cache |
| `alarm_check` (gui) | 1 s | 1 s | alarm time-match check; wakes the display + plays the alert sound when due |

The task_coordinator stack is sized for LVGL render under the wake hook (8 KB) so subscribers that mutate widgets can do so directly (under the LVGL lock).

Event-driven (not polled): the **GPIO 0 back-button** uses an edge-triggered ISR + task notification — zero idle wakes. The **LVGL task** (`taskLVGL`) is suspended via `vTaskSuspend()` when the display is off and resumed on wake.

### Two-stage brightness dim

The display dims before it turns off, on one of two cycles measured against `lv_disp_get_inactive_time()`:

- **Fresh wake from off**: bright for T/2, dim (30% brightness) for T/2, off at T — where T = configured display timeout.
- **After any touch / button activity**: bright for 5 s, dim for 5 s, then off.

Any input restores full brightness and restarts the cycle.

### Pre-show / pre-off callbacks

`display_manager` exposes two hooks:

- **`display_manager_set_pre_show_cb`** — runs inside the LVGL lock during wake, between invalidate and `lv_refr_now()`. The gui uses this to (a) snap the tileview back to the watchface, (b) refresh the watchface time labels from the RTC, (c) read AXP state into the battery indicator. The synchronous render that follows means **the very first frame the user sees is already correct** — no flash of stale time or zero-percent battery.
- **`display_manager_set_pre_off_cb`** — runs at the very start of sleep, before the panel powers down. The signalk_client uses this to close the WS and stop the radio, so falling asleep on the dashboard fully releases WiFi.

### Sleep / frequency behaviour

CPU configured for DFS between 40 MHz and 240 MHz with light sleep enabled (`esp_pm_configure`). CPU is powered down in light sleep (`CONFIG_PM_POWER_DOWN_CPU_IN_LIGHT_SLEEP`). A single `ESP_PM_NO_LIGHT_SLEEP` lock — held only while the display is active — gates sleep.

**Measured behaviour** (via `CONFIG_PM_PROFILING`, off by default):

| State | Time in light sleep | Time at 40 MHz | Time at 240 MHz |
|---|---|---|---|
| Display OFF | **~100%** | — | <1% |
| Display ON, idle | 0% (lock held) | **~72%** | ~22% |

### Component-level power notes

- **Audio codec** — opened per playback and closed immediately after, so the ES8311/I2S `APB_FREQ_MAX` lock isn't held continuously.
- **I2S RX (microphone)** — left initialised but **not enabled** in the BSP fork; an enabled I2S channel holds an APB lock even when idle, and this firmware never records.
- **QMI8658 IMU** — put into Power-Down mode (`CTRL7=0`, `CTRL1.SensorDisable=1`, ~6 µA) at boot.
- **FT3168 touch** — dropped to Monitor mode (~30 µA vs ~1.5 mA Active) after 10 s of display-on idle, then to Sleep mode (~10 µA) when the display turns off. On wake, a hardware RESETB pulse (GPIO 9) exits Sleep mode before LVGL begins polling the IC.
- **WiFi** — fully off (`esp_wifi_stop`) outside of SignalK tile use or one-shot NTP sync. While the SignalK WS is open, the radio runs in `WIFI_PS_MIN_MODEM` (listens every beacon, ~100 ms typical).
- **Standby floor** — with the display off, the dominant remaining draw is PSRAM self-refresh (~1–2 mA); the rest of the system is in light sleep.

> **Note:** The AXP2101 INT pin (GPIO 35) is held LOW by the PMIC when USB power is connected. Level-triggered GPIO wakeup on this pin causes a sleep/wake spin loop and WDT reset. GPIO wakeup from the PMU is therefore not used; the coordinator polls the PMU power key at 1 s intervals while the display is off instead.

---

## Hardware

| Component | Part | Interface |
|---|---|---|
| MCU | ESP32-S3R8 (dual-core LX7 @ 240 MHz, 8 MB PSRAM) | — |
| Display | 2.06" AMOLED 410×502, SH8601 driver | QSPI |
| Touch | FT3168 capacitive controller (FT5x06-compatible) | I2C |
| IMU | QMI8658 6-axis (accel + gyro) | I2C |
| RTC | PCF85063A | I2C |
| PMU | AXP2101 (ALDO rails, charging, power key) | I2C |
| Audio | ES8311 codec + speaker | I2S |
| Flash | 32 MB external QSPI | — |
| Battery | 3.7 V Li-ion via MX1.25 header | — |

> **Note on ALDO rails:** The AXP2101 ALDO1–4 rails are disabled by default on cold boot. The firmware explicitly enables them before `bsp_display_start()` to power the display circuitry. The BSP cuts all four rails when the display sleeps and re-enables them on wake.

---

## Project Structure

```
S3Watch/
├── main/                        # app_main, NVS init, boot sequence
├── components/
│   ├── gui/                     # All LVGL screens and UI logic
│   │   ├── src/
│   │   │   ├── ui.c                            # UI task entry + orchestration only
│   │   │   ├── ui_screens.c                    # theme + active-screen tracking
│   │   │   ├── ui_tileview.c                   # tileview grid + dynamic sub-tiles
│   │   │   ├── ui_back_button.c                # GPIO 0 ISR + PMU short-press → back
│   │   │   ├── ui_power_events.c               # BSP power events + pre-show wake hook
│   │   │   ├── watchface.c                     # Registry + dispatcher
│   │   │   ├── faces/face_1.c                  # Stacked HH/MM + centered SS
│   │   │   ├── faces/face_2.c                  # Horizontal HH:MM + AM/PM + date
│   │   │   ├── app_picker.c                    # App launcher grid (swipe up, tile 0,2)
│   │   │   ├── world_clock.c                   # World Clock app
│   │   │   ├── stopwatch.c                     # Stopwatch app
│   │   │   ├── alarm_clock.c                   # Alarm Clock app
│   │   │   ├── signalk_dashboard.c             # SignalK app (launched from app picker)
│   │   │   ├── signalk_dashboard_fmt.c         # Pure formatters (testable)
│   │   │   ├── signalk_alerts.c                # SignalK alarms tile (swipe down)
│   │   │   ├── batt_screen.c, brightness_screen.c, ...
│   │   │   ├── settings_screen.c               # Control screen (quick-access grid)
│   │   │   ├── settings_menu_screen.c          # Settings list
│   │   │   ├── setting_time_screen.c           # Roller-based time/date picker
│   │   │   ├── setting_tz_screen.c             # POSIX timezone preset picker
│   │   │   ├── setting_signalk_screen.c        # IP picker / hostname / port numpad
│   │   │   ├── setting_ondev_test_screen.c     # Settings → Run Tests
│   │   │   ├── ip_picker.c, numpad.c           # Reusable input widgets
│   │   │   ├── wifi_screens.c, scroll_keyboard.c
│   │   │   └── storage_file_explorer.c, lvgl_spiffs_fs.c
│   │   ├── include/                            # public + internal headers
│   │   ├── font/                               # Pre-compiled LVGL fonts (26–160 px)
│   │   └── icons/                              # Pre-compiled images
│   ├── bsp_extra/                              # Board drivers not in upstream BSP
│   │   ├── src/bsp_board_extra.c               # I2C bus init/recovery, FT3168 power mode
│   │   ├── src/pcf85063a.c                     # PCF85063A RTC driver (BCD)
│   │   ├── src/rtc_lib.c                       # UTC contract, NVS persistence
│   │   └── include/utc_tm_to_epoch.h           # Pure header (host-testable)
│   ├── task_coordinator/                       # Shared periodic-work scheduler
│   ├── display_manager/                        # Display lifecycle + pre-show/pre-off hooks
│   ├── settings/                               # JSON on SPIFFS, debounced writes, range-clamped load
│   ├── wifi_manager/                           # Wi-Fi scan/connect/auto/release; event bus
│   ├── ntp_sync/                               # SNTP; fires on connect, releases on success
│   ├── audio_alert/                            # ES8311 init, WAV/MP3 decode, synth fallback
│   ├── sensors/                                # QMI8658 power-down + test-hook accel read
│   ├── signalk_client/                         # WebSocket client; value + alert caches
│   │   ├── src/signalk_client.c                #   parse_delta, WS event handler, lifecycle
│   │   └── include/signalk_alert_parse.h       #   Pure header (host-testable)
│   └── ondev_test/                             # On-device test runner (sw + hw suites)
├── tests/                                      # Host (native) unit tests
│   ├── test.h, test_main.c                     # Minimal CTest-able runner
│   ├── test_utc_tm.c                           # Days-from-civil vs host timegm
│   ├── test_ip_parse.c
│   └── test_signalk_alert.c
├── ui_assets/                                  # Source images and fonts (pre-conversion)
├── partitions.csv                              # app + storage SPIFFS
├── sdkconfig.defaults                          # Project Kconfig defaults
└── dependencies.lock                           # Component manager lock file
```

---

## Testing

Two layers of unit testing, both run on every change:

### Host (native) tests — `tests/`

Pure-logic helpers are extracted into header-only files with **no ESP-IDF dependencies** (see [tests/README.md](tests/README.md)) and compiled by the host clang/gcc:

| Header (also used by firmware) | Suite |
|---|---|
| [`utc_tm_to_epoch.h`](components/bsp_extra/include/utc_tm_to_epoch.h) | UTC tm → epoch via Howard Hinnant arithmetic |
| [`ip_parse.h`](components/gui/include/ip_parse.h) | IPv4 string parser |
| [`signalk_alert_parse.h`](components/signalk_client/include/signalk_alert_parse.h) | SignalK state-string → enum + severity ordering |

Build + run:
```bash
cmake -S tests -B tests/build
cmake --build tests/build
tests/build/s3watch_tests          # PASS — 0 total failures
```

### On-device tests — Settings → Run Tests

A live test runner reachable from the settings menu. Two categories, ~6–8 s total:

- **Software** (7 suites) — exercises real firmware code paths: re-runs the host suites, plus SignalK delta JSON parsing (via the `signalk_test_inject_delta` hook), alert-cache sort + eviction, dashboard formatters, settings save/load round-trip.
- **Hardware** (11 probes) — heap snapshot, I2C bus scan, AXP2101 read, RTC read, NVS round-trip, SPIFFS round-trip, QMI8658 wake + accel-magnitude check, audio output, brightness ramp, WiFi scan, task_coordinator stats dump.

Each test logs detail to serial (`[n/N] sw|hw <name>: PASS|FAIL — <detail>`) and updates a row on screen with a status dot. A single failure does not abort the suite — every test runs so the user sees the full picture in one go.

---

## Building and Flashing

### Prerequisites

- [Visual Studio Code](https://code.visualstudio.com/) with the [ESP-IDF Extension](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension)
- ESP-IDF **v5.5.4** (install via the extension wizard or manually)
- USB-C cable connected to the board's USB port

### Steps

1. **Install the ESP-IDF extension** — open VS Code, go to Extensions, search for `ESP-IDF`, install. Run `ESP-IDF: Configure ESP-IDF extension` and let it download ESP-IDF 5.5.4 + toolchain.
2. **Open the project** — `File → Open Folder...` → select the `S3Watch` directory.
3. **Set the target** — click `Set Target` in the status bar and choose `esp32s3`.
4. **Build** — click `Build Project`. First build takes several minutes as managed components are fetched.
5. **Select the port** — `ESP-IDF: Select Serial Port` and pick the device (e.g., `/dev/cu.usbmodem*` on macOS).
6. **Flash** — `ESP-IDF: Flash (UART)` or `ESP-IDF: Flash and Monitor`.

---

## Configuration

Key options (seeded from [`sdkconfig.defaults`](sdkconfig.defaults); a few power-tuning values are committed directly in `sdkconfig`):

| Option | Value | Purpose |
|---|---|---|
| `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240` | `y` | Max CPU 240 MHz (DFS scales down to 40 MHz) |
| `CONFIG_SPIRAM_MODE_OCT` | `y` | Octal PSRAM mode (8 MB) |
| `CONFIG_FREERTOS_HZ` | `200` | Lower tick rate to reduce idle overhead |
| `CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH` | `6144` | Extra stack for SPIFFS + cJSON in timer callbacks |
| `CONFIG_PM_ENABLE` | `y` | Dynamic frequency scaling + light sleep |
| `CONFIG_PM_DFS_INIT_AUTO` | `y` | Auto-init DFS at boot |
| `CONFIG_PM_POWER_DOWN_CPU_IN_LIGHT_SLEEP` | `y` | Power down CPU core during light sleep |
| `CONFIG_FREERTOS_IDLE_TIME_BEFORE_SLEEP` | `1` | Enter light sleep after 1 idle tick |
| `CONFIG_LV_DEF_REFR_PERIOD` | `33` | ~30 fps LVGL refresh |
| `CONFIG_ESP_WIFI_ENABLED` | `y` | Wi-Fi stack (NTP + SignalK) |
| `CONFIG_BT_ENABLED` | `n` | BLE stack disabled |
| `CONFIG_PM_PROFILING` | `n` | PM lock/sleep profiling |

---

## Partition Table

Defined in [`partitions.csv`](partitions.csv). Includes a dedicated `storage` SPIFFS partition used for settings JSON and audio assets.

---

## Dependencies

Managed via the ESP-IDF Component Manager:

| Component | Source | Purpose |
|---|---|---|
| `lvgl/lvgl` | Component registry | UI graphics library (v9.3.0) |
| `espressif/esp_lvgl_port` | Component registry | LVGL/ESP-IDF integration layer |
| `espressif/esp_websocket_client` | Component registry | WebSocket client (SignalK) |
| `esp32_s3_touch_amoled_2_06` | [`github.com/richcreations/esp32_s3_touch_amoled_2_06`](https://github.com/richcreations/esp32_s3_touch_amoled_2_06) | Board BSP |
| `waveshare/qmi8658` | Component registry | IMU driver |
| `espressif/esp_codec_dev` | Component registry | Audio codec abstraction |
| `minimp3` | Vendored in `audio_alert/third_party/` | Lightweight MP3 decoder |

---

## Known Limitations

- **No on-device push for SignalK** — the SignalK alerts tile is a passive viewer; the watch does not vibrate, badge the watchface, or originate alerts of its own for boat data. The boat's alarm system is the source of truth. (The Alarm Clock app is unrelated — it's a standalone daily alarm, see App picker above.)
- **No SignalK authentication** — v1 assumes the server allows anonymous read of `vessels.self`. Token auth is a future setting.
- **Wi-Fi radio released after NTP sync** — re-opening Wi-Fi settings, or entering a SignalK tile, wakes the radio on demand.
- **No VBAT backup for RTC** — the PCF85063A loses power when the main battery is fully drained; time is recovered from NVS on next boot but may be slightly stale.
- **PMU GPIO wakeup not used** — the AXP2101 INT pin (GPIO 35) is held LOW when USB power is connected, making level-triggered wakeup unsafe (causes WDT reset). The display manager polls instead.
- **Watchface width variability** — `font_numbers_120` has non-monospaced digits ('4' is 85 px wide vs '1' at 63 px). For digit combinations heavy in '4's, the time row may slightly overflow the 410 px screen width.
