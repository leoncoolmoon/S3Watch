# power_manager

Central coordinator for system sleep/wake, ESP-IDF PM locks, and AXP2101 ALDO rail gating. Every other component that needs to influence power state goes through this component.

## Responsibilities

- Manages the single `ESP_PM_NO_LIGHT_SLEEP` lock with a reference count, so multiple subsystems can independently hold it without fighting over the same underlying lock.
- Owns ALDO rail state for named clients (currently `PM_RAIL_CLIENT_AUDIO` → ALDO3/A3V3). Rails are gated in the sleep sequence only when no client holds a reference.
- Fires `PM_EVT_PREPARE_SLEEP` and `PM_EVT_WOKE_UP` events to up to 8 registered listeners.
- Registers display hardware ops (`pm_display_ops_t`) so `display_manager` can hook into the sleep/wake sequence without a circular dependency.
- Polls the PMU power button to trigger wake from sleep.
- Configures `esp_pm_configure` (DFS + tickless idle) and initialises `task_coordinator`.

## ALDO rail mapping

From the board schematic (`bsp/doc/power-rails.md`):

| Rail | PMU output | Consumers |
|------|-----------|-----------|
| ALDO3 → A3V3 | `PM_RAIL_CLIENT_AUDIO` | ES8311 AVDD/DACVREF/ADCVREF, ES7210 VDDA/VDDM |
| ALDO1/2/4 | (display, not gated) | AMOLED J3 pixel/IO supplies — left on during sleep to preserve DCS panel state |

## Sleep sequence

1. Fire `PM_EVT_PREPARE_SLEEP` to all listeners.
2. Call `on_sleep()` (DCS panel sleep, touch IC to Monitor mode) while ALDOs are still on.
3. Gate ALDO3 (audio) if `s_rail_refcount[ALDO3] == 0`.
4. Release the system-awake no-sleep hold.

## Wake sequence

1. Acquire no-sleep hold.
2. Re-enable all switchable ALDOs (ALDO3 restore + 5 ms settle).
3. Call `on_wake()` (Sleep-Out, I2C recover, LVGL resume, backlight).
4. Fire `PM_EVT_WOKE_UP` to all listeners.

## Initialization

```c
bsp_extra_init();       // AXP2101
power_manager_init();   // PM lock, DFS, ALDO rails, task_coordinator
audio_manager_init();
bsp_display_start();
```

## Dependencies

`esp32_s3_touch_amoled_2_06` (BSP), `task_coordinator`
