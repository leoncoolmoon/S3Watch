# power_manager

Central coordinator for system sleep/wake, ESP-IDF PM locks, and AXP2101 ALDO rail gating. Every other component that needs to influence power state goes through this component.

## Responsibilities

- Manages the single `ESP_PM_NO_LIGHT_SLEEP` lock with a reference count, so multiple subsystems can independently hold it without fighting over the same underlying lock.
- **Sole owner of all ALDO rails** — the only code that calls `bsp_power_rail_enable`. Consumers just hold/release a lock per rail group; a rail is powered iff some client holds it (toggled on the 0↔1 refcount edge). Clients: `PM_RAIL_CLIENT_AUDIO` → ALDO3 (held by audio while a codec is open), `PM_RAIL_CLIENT_DISPLAY` → ALDO1/2/4 (held by `display_manager` while the display is on; the three panel rails switch as one group).
- Fires `PM_EVT_PREPARE_SLEEP` and `PM_EVT_WOKE_UP` events to up to 8 registered listeners.
- Registers display hardware ops (`pm_display_ops_t`) so `display_manager` can hook into the sleep/wake sequence without a circular dependency.
- Polls the PMU power button to trigger wake from sleep.
- **Battery power telemetry** — a 60 s `task_coordinator` subscriber logs `PWRLOG up=<s> awake=<0/1> soc=<%> vbat=<mV> aldo=<1234>` **when on battery** (skipped on USB). The AXP2101 has no current ADC, so power is measured from SOC%/VBAT drain over time; `sd_logger` mirrors the lines to `/sdcard/logs/` when SD logging is enabled. See `doc/power-rails.md` → "Measuring battery power".
- Configures `esp_pm_configure` (DFS + tickless idle) and initialises `task_coordinator`.

## ALDO rail mapping

From the board schematic (`bsp/doc/power-rails.md`):

| Rail | Lock client | Consumers / on when |
|------|-----------|-----------|
| ALDO3 → A3V3 | `PM_RAIL_CLIENT_AUDIO` | ES8311 AVDD/DACVREF/ADCVREF, ES7210 VDDA/VDDM — on only while audio is playing |
| ALDO1/2/4 | `PM_RAIL_CLIENT_DISPLAY` | AMOLED J3 pixel/IO supplies — on only while the display is on; cut in display-off (panel is reinited on wake) |

## Sleep sequence

1. Fire `PM_EVT_PREPARE_SLEEP` (NOTIFY audio releases its AUDIO lock → ALDO3 drops unless music/alarm hold it).
2. Call `on_sleep()` — `display_manager` sends DCS sleep-in, then releases the DISPLAY lock → cuts ALDO1/2/4.
3. Release the system-awake no-sleep hold. (power_manager touches no rails directly — all rail state flows from the locks.)

## Wake sequence

1. Acquire no-sleep hold.
2. Call `on_wake()` — `display_manager` re-holds the DISPLAY lock (powers ALDO1/2/4), then full panel reinit (`bsp_display_wake_from_gated`), I2C recover, LVGL resume, backlight.
3. Fire `PM_EVT_WOKE_UP` to all listeners. (ALDO3 stays whatever audio dictates.)

## Initialization

```c
bsp_extra_init();       // AXP2101
power_manager_init();   // PM lock, DFS, ALDO rails, task_coordinator
audio_manager_init();
bsp_display_start();
```

## Dependencies

`esp32_s3_touch_amoled_2_06` (BSP), `task_coordinator`
