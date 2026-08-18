# imu_manager

Owns the QMI8658C IMU: its power state and a **software step counter**.

## Important: this die is the QMI8658**C**, not the **A**

The board's IMU is a **QMI8658C** (schematic U5). The QMI8658**A** datasheet
describes a hardware Pedometer/Tap/Motion engine block ("Motion Co-Processor"),
but on the **C** that block is the **AttitudeEngine** (sensor fusion) — there is
**no hardware pedometer** (the C's CTRL9 command set has no `CONFIGURE_PEDOMETER`).
So step counting is done in software here. (Both datasheets are in `doc/`.)

## Power behaviour

The QMI8658C is hardwired to VCC3V3 (always-on — cannot be ALDO-gated by
`power_manager`) and boots active (~270 µA). When step counting is off,
`imu_manager_idle()` drops it to Power-Down (~6 µA). When on, the accelerometer
runs at 31.25 Hz feeding the FIFO.

## Step counting (software, FIFO-fed)

- The accelerometer streams into the chip's **hardware FIFO** (Stream mode, 128
  samples ≈ 4 s at 31.25 Hz, accel-only = 6 bytes/sample).
- A `task_coordinator` subscriber (`step_sampler`, ~1 Hz on / 2 s off) **drains the
  FIFO** via the CTRL9 `REQ_FIFO` (0x05) read protocol and feeds every buffered
  sample to a peak detector on |a| (slow EMA baseline removes gravity; threshold +
  hysteresis + min-interval debounce counts steps).
- **Why the FIFO:** the accel samples at 31 Hz in hardware while the CPU stays
  asleep between drains — no 31 Hz host polling, so it's light-sleep friendly.
- The FIFO read buffer (768 B) is allocated in **PSRAM**.

Gated by the `step_counter_enabled` setting. The live count here is a software
variable (zeroed on enable). **No driver fork** — all register/FIFO access is via the
Waveshare driver's generic `qmi8658_read_register`/`write_register`.

Tuning knobs (in `imu_manager.c`): `STEP_TH_MG`, `STEP_RESET_MG`, `STEP_MIN_GAP`,
`STEP_EMA_ALPHA`.

Aggregation/persistence — lifetime/daily/rolling-week totals, best-day/best-week
records, reboot persistence, and SD backup — lives in the **`step_tracker`**
component, which subscribes via `imu_manager_set_step_cb()`. `imu_manager` only
detects and reports per-batch step deltas; it knows nothing about dates or storage.

## Step telemetry (`STEPLOG`)

The sampler logs `STEPLOG steps=… fifo_bytes=… last_mag=… baseline=…` **only while
step counting is on AND SD logging is enabled** — silent in normal use, captured to
`/sdcard/logs/` (via `sd_logger`) when SD logging is on, so a real walk can be
recorded untethered for tuning. (`last_mag` ≈ 1000 mg at rest confirms the FIFO read
is healthy.) Same pattern as power_manager's `PWRLOG`.

## Usage

```c
imu_manager_set_step_counting(settings_get_step_counter_enabled()); // at boot
imu_manager_set_step_counting(true);   // enable (Settings toggle handler)
uint32_t steps; imu_manager_get_steps(&steps);
imu_manager_reset_steps();              // zero the count
```

## Dependencies

`esp32_s3_touch_amoled_2_06` (BSP — I2C bus), `waveshare__qmi8658` (raw register
driver), `settings` (SD-logging gate for STEPLOG), `task_coordinator` (drain sampler).
