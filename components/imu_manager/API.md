# imu_manager API

All functions are I2C-blocking; call from task context (e.g. the UI/LVGL task or
boot). State is held in module statics; no handle is exposed.

### `imu_manager_idle`
```c
void imu_manager_idle(void);
```
Put the IMU in Power-Down (~6 µA). Initialises the driver on first call. Safe at boot.

---

### `imu_manager_set_step_counting`
```c
void imu_manager_set_step_counting(bool enable);
```
`enable=true`: start the accelerometer (31.25 Hz, ±8 g) streaming into the FIFO and
zero the software step count. `enable=false`: stop and return the IMU to Power-Down.
Idempotent.

---

### `imu_manager_get_steps`
```c
esp_err_t imu_manager_get_steps(uint32_t *out);
```
Return the software step count (no I2C — it's a module variable updated by the FIFO
drain). Returns `ESP_OK`; `*out = 0` if step counting is disabled.

---

### `imu_manager_reset_steps`
```c
void imu_manager_reset_steps(void);
```
Zero the software step count and detector state.

---

### `imu_manager_set_step_cb`
```c
void imu_manager_set_step_cb(void (*cb)(uint32_t new_steps));
```
Register a single callback invoked from the FIFO-drain task with the number of **new**
steps detected in each batch (`new_steps > 0` only). `step_tracker` uses this to
accumulate lifetime/daily/weekly totals + records. Pass `NULL` to clear.

---

### `imu_manager_test_read_accel_g`
```c
esp_err_t imu_manager_test_read_accel_g(float *x, float *y, float *z);
```
Test hook (on-device test runner): wake the IMU, read one accel sample in g, then
restore the prior state (Power-Down, or re-establish the accel/FIFO if step counting
was running). ~50 ms blocking.

---

## Implementation notes (software step counter)

This die is the **QMI8658C**, which has **no hardware pedometer** (the A's pedometer
isn't in the C — its CTRL9 set has no `CONFIGURE_PEDOMETER`; "Motion Co-Processor" =
the AttitudeEngine sensor-fusion block). So steps are counted in software:

- Accel → hardware **FIFO** (`FIFO_CTRL`=0x0E: Stream mode, 128-sample depth),
  drained by a `task_coordinator` subscriber (~1 Hz) so the CPU sleeps between drains.
- **FIFO read** (§5.9.5 `REQ_FIFO`): write `0x05` to `CTRL9`(0x0A) → poll
  `STATUS1.bit0` (CmdDone — note: bit0 on the C, not the A's `STATUSINT.bit7`) →
  burst-read `FIFO_DATA`(0x17) in ≤240-byte chunks → clear `FIFO_RD_MODE`. Samples are
  6-byte accel triples (AX_L,AX_H,…AZ_H), signed 16-bit, ±8 g (1000/4096 mg/LSB).
- **Detector:** slow EMA of |a| removes gravity; a peak crossing `STEP_TH_MG` with
  hysteresis (`STEP_RESET_MG`) and a `STEP_MIN_GAP`-sample debounce counts a step.
- The FIFO buffer is in PSRAM. No driver fork.
