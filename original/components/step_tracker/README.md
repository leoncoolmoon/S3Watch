# step_tracker

Persisted step **statistics** on top of `imu_manager`'s software step counter:
lifetime / today / rolling-week totals, best-day and best-week records, local-midnight
rollover, and flash-wear-conscious persistence.

## How it fits

`imu_manager` detects steps (FIFO-fed peak detector) and reports **per-batch deltas**
through a registered callback. `step_tracker` registers that callback
(`imu_manager_set_step_cb`), so `imu_manager` stays a pure sensor driver with no
knowledge of dates, NVS, or settings.

```
imu_manager (drain task) ──delta──▶ step_tracker ──▶ NVS (working store)
                                          │
                                          └──daily──▶ settings.json (SD-backup-able)
step_app ◀──today/week/lifetime/records── step_tracker
```

## Stats model

- **today** — steps since local midnight. **today_day** is the local epoch-day it
  belongs to (`localtime_r`; the TZ is already applied by `settings`).
- **week** — `today` + the previous 6 local days (a 7-slot ring of completed days;
  missing days count as 0).
- **lifetime** — steps ever.
- **best_day / best_week** — record highs, refreshed whenever totals change.
- **Rollover** is *lazy*: applied at the start of every getter and every step
  callback, so an idle watch reports the correct "today" the instant the app reads
  it. A day boundary archives the finished day into the ring, zeroes `today`, and
  mirrors a snapshot to `settings.json`. Multi-day gaps (watch off) leave the missing
  days absent → counted as 0. **Forward-only:** a backward day change (NTP stepping
  a fast clock back across midnight, or a TZ change) does not roll — counting
  continues into the current day until the real date catches up.

## Persistence & flash wear (the key constraint)

Two stores, both written sparingly:

| Store | Holds | Written |
|-------|-------|---------|
| **NVS** blob (`step_trk/stats`, wear-leveled) | the live working struct | at a rollover, **and** a throttled commit when dirty + ≥ `SAVE_THROTTLE_MS` (15 min) since the last — a *handful* per active day |
| **`settings.json`** (SPIFFS) | a daily snapshot | once per day at rollover (`settings_set_step_stats` → the normal debounced save) |

No per-step writes. Worst-case loss on an abrupt power cut ≈ the 15-min throttle
window. On boot, `step_tracker_init()` loads NVS; if it's empty/stale (fresh flash or
a **restored SD backup**), it seeds from the `settings.json` snapshot. A factory
`settings_reset_defaults()` clears the snapshot; **Reset Stats** (Settings → Step
Counter) wipes everything via `step_tracker_reset_all()`.

The shared `step_stats_t` struct lives in **`settings.h`** (not here) so `settings`
has no dependency back on `step_tracker`. Backup is automatic — the step block is part
of `settings_to_json()`, which already feeds `settings_backup_to_sd()`.

## Usage

```c
step_tracker_init();   // boot, after settings_init() + imu_manager setup
uint32_t today = step_tracker_today();   // also _week / _lifetime / _best_day / _best_week
step_tracker_reset_all();                // wipe (Settings button)
```

## Dependencies

`imu_manager` (step callback), `settings` (snapshot + shared struct), `nvs_flash`
(working store). NVS must already be initialised at boot (it is, in `app_main`).
