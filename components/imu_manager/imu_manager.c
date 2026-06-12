// imu_manager — QMI8658C IMU power state + software step counter.
//
// The QMI8658C is hardwired to VCC3V3 (always on, can't be ALDO-gated) and powers
// up with accel+gyro enabled (~270 µA); a few I2C writes drop it to ~6 µA
// Power-Down. NOTE: this die is the QMI8658*C* (per the board schematic, U5),
// whose "Motion Co-Processor" is the AttitudeEngine (sensor fusion) — it has NO
// hardware pedometer/tap/motion engine (those live only in the QMI8658*A*). So
// step counting is done in software here: the accelerometer streams into the
// chip's hardware FIFO, and a task_coordinator subscriber drains it ~1 Hz and
// runs a peak detector on |a|. The FIFO lets the accel sample at ~31 Hz while the
// CPU stays asleep between drains (light-sleep friendly).
//
// All register access goes through the Waveshare driver's generic
// qmi8658_read_register / qmi8658_write_register — no driver fork.

#include "imu_manager.h"
#include "bsp/esp32_s3_touch_amoled_2_06.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "qmi8658.h"
#include "settings.h"
#include "task_coordinator.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

#define IMU_ADDR_HIGH QMI8658_ADDRESS_HIGH
#define IMU_ADDR_LOW  QMI8658_ADDRESS_LOW

// Registers not in the Waveshare driver's enum (QMI8658C datasheet §4.1/§5.5).
#define REG_FIFO_WTM_TH 0x13
#define REG_FIFO_CTRL   0x14
#define REG_FIFO_SMPL   0x15   // FIFO sample count LSB (in bytes)
#define REG_FIFO_STATUS 0x16   // bit4 NOT_EMPTY, bit5 OVFLOW, bits[1:0] count MSB
#define REG_FIFO_DATA   0x17
#define REG_STATUS1     0x2F   // bit0 = CmdDone (CTRL9 protocol bit) on the C
#define REG_RESET       0x60   // write 0xB0 → soft reset
#define REG_RESET_DONE  0x4D   // reads 0x80 after a successful reset
#define RESET_CMD       0xB0

#define CTRL9_REQ_FIFO  0x05   // request FIFO read access (§5.9.5)

// CTRL register values.
#define CTRL1_ACTIVE     0x60   // ADDR_AI=1, BE=1, SensorDisable=0 (driver default)
#define CTRL1_POWERDOWN  0x61   // + SensorDisable=1 (~6 µA)
#define CTRL2_ACCEL_CFG  0x28   // aFS=±8g (bits6:4=2, lsb_div=4096), aODR=31.25 Hz (bits3:0=8)
#define CTRL7_ACCEL_ON   0x01   // aEN=1, gyro off (accel-only → 6 bytes/FIFO sample)
// FIFO: Stream mode (circular, never blocks), 128-sample depth (~4 s at 31 Hz).
#define FIFO_CTRL_RUN    0x0E   // bits[3:2]=11 (128 samples), bits[1:0]=10 (Stream), RD_MODE=0

#define ACCEL_ODR_HZ     31.25f
#define ACCEL_MG_PER_LSB (1000.0f / 4096.0f)   // ±8g, lsb_div=4096
#define FIFO_BUF_BYTES   768                    // 128 samples × 6 bytes (accel-only)

// Step detector tuning (|a| in mg). Tunable from the STEPLOG telemetry.
#define STEP_TH_MG       150.0f   // |a|-baseline must exceed this to register a peak
#define STEP_RESET_MG     60.0f   // …then drop below this before the next peak (hysteresis)
#define STEP_EMA_ALPHA   (1.0f/32.0f)  // baseline tracker (high-passes out gravity)
#define STEP_MIN_GAP     8        // min samples between steps (~0.25 s → ≤4 steps/s)

static const char *TAG = "IMU_MGR";
static qmi8658_dev_t s_imu;
static bool s_inited        = false;
static bool s_step_counting = false;
static bool s_subscribed    = false;
static uint8_t *s_fifo_buf  = NULL;   // PSRAM scratch for FIFO drains
static void (*s_step_cb)(uint32_t) = NULL;  // notified of new-step deltas (step_tracker)

// Software step-detector state (persists across drains).
static uint32_t s_steps      = 0;
static float    s_baseline   = 0.0f;
static bool     s_baseline_ok = false;
static bool     s_peak_armed = true;  // ready to register the next peak
static uint32_t s_since_step = 0;
// last-drain diagnostics
static float    s_last_mag = 0.0f;
static uint16_t s_last_nbytes = 0;

// ── init ────────────────────────────────────────────────────────────────────

static void step_sampler_cb(void *user);  // defined below

static bool ensure_init(void) {
    if (s_inited) return true;
    if (bsp_i2c_init() != ESP_OK) { ESP_LOGE(TAG, "I2C not available"); return false; }
    if (qmi8658_init(&s_imu, bsp_i2c_get_handle(), IMU_ADDR_HIGH) != ESP_OK &&
        qmi8658_init(&s_imu, bsp_i2c_get_handle(), IMU_ADDR_LOW)  != ESP_OK) {
        ESP_LOGE(TAG, "QMI8658 not found at either address");
        return false;
    }
    if (!s_fifo_buf) s_fifo_buf = heap_caps_malloc(FIFO_BUF_BYTES, MALLOC_CAP_SPIRAM);
    s_inited = true;
    // Subscribe the FIFO-drain sampler once. The first ensure_init() runs at boot
    // from imu_manager_set_step_counting() — before task_coord_start() — so this is
    // a safe pre-start subscription. The callback self-gates on step counting.
    if (!s_subscribed) {
        task_coord_subscribe("step_sampler", step_sampler_cb, NULL,
                             /*on*/ 1000, /*off*/ 2000);
        s_subscribed = true;
    }
    return true;
}

// ── accel + FIFO configuration ──────────────────────────────────────────────

static void accel_fifo_start(void) {
    // Soft reset so the FIFO/accel start from a known state (the chip isn't reset
    // by an ESP reboot — it's on always-on power).
    qmi8658_write_register(&s_imu, REG_RESET, RESET_CMD);
    vTaskDelay(pdMS_TO_TICKS(50));
    qmi8658_write_register(&s_imu, QMI8658_CTRL1, CTRL1_ACTIVE);   // ADDR_AI+BE, active
    qmi8658_write_register(&s_imu, QMI8658_CTRL2, CTRL2_ACCEL_CFG);// ±8g, 31.25 Hz
    qmi8658_write_register(&s_imu, REG_FIFO_CTRL, FIFO_CTRL_RUN);  // Stream, 128 samples
    qmi8658_write_register(&s_imu, QMI8658_CTRL7, CTRL7_ACCEL_ON); // accel on → FIFO fills
    ESP_LOGI(TAG, "accel+FIFO running (31.25 Hz ±8g, software step counter)");
}

static void imu_powerdown(void) {
    qmi8658_write_register(&s_imu, REG_FIFO_CTRL, 0x00);           // FIFO bypass/off
    (void)qmi8658_enable_sensors(&s_imu, QMI8658_DISABLE_ALL);
    (void)qmi8658_write_register(&s_imu, QMI8658_CTRL1, CTRL1_POWERDOWN);
}

// ── step detector ───────────────────────────────────────────────────────────

static void step_process_sample(float mag) {
    if (!s_baseline_ok) { s_baseline = mag; s_baseline_ok = true; }
    s_baseline += (mag - s_baseline) * STEP_EMA_ALPHA;   // slow baseline ≈ gravity
    float diff = mag - s_baseline;                        // band-passed motion
    s_since_step++;
    if (s_peak_armed && diff > STEP_TH_MG) {
        s_peak_armed = false;
        if (s_since_step >= STEP_MIN_GAP) { s_steps++; s_since_step = 0; }
    } else if (!s_peak_armed && diff < STEP_RESET_MG) {
        s_peak_armed = true;                              // ready for the next peak
    }
}

// Drain the hardware FIFO and feed every buffered accel sample to the detector.
static void fifo_drain(void) {
    if (!s_fifo_buf) return;
    uint8_t cnt_lsb = 0, fstatus = 0;
    qmi8658_read_register(&s_imu, REG_FIFO_SMPL, &cnt_lsb, 1);
    qmi8658_read_register(&s_imu, REG_FIFO_STATUS, &fstatus, 1);
    uint16_t nbytes = ((uint16_t)(fstatus & 0x03) << 8) | cnt_lsb;
    if (nbytes > FIFO_BUF_BYTES) nbytes = FIFO_BUF_BYTES;
    nbytes -= nbytes % 6;                                 // whole accel samples only
    s_last_nbytes = nbytes;
    if (nbytes == 0) return;

    // REQ_FIFO: arm read mode, wait CmdDone (STATUS1.bit0 on the C), burst-read.
    qmi8658_write_register(&s_imu, QMI8658_CTRL9, CTRL9_REQ_FIFO);
    for (int i = 0; i < 50; i++) {                        // tolerant CmdDone poll
        uint8_t s = 0;
        if (qmi8658_read_register(&s_imu, REG_STATUS1, &s, 1) == ESP_OK && (s & 0x01)) break;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    uint16_t off = 0;
    while (off < nbytes) {                                // driver length is uint8_t → chunk
        uint8_t chunk = (nbytes - off) > 240 ? 240 : (uint8_t)(nbytes - off);
        qmi8658_read_register(&s_imu, REG_FIFO_DATA, s_fifo_buf + off, chunk);
        off += chunk;
    }
    qmi8658_write_register(&s_imu, REG_FIFO_CTRL, FIFO_CTRL_RUN); // clear FIFO_RD_MODE

    uint32_t before = s_steps;
    for (uint16_t i = 0; i + 6 <= nbytes; i += 6) {
        int16_t ax = (int16_t)(s_fifo_buf[i]   | (s_fifo_buf[i+1] << 8));
        int16_t ay = (int16_t)(s_fifo_buf[i+2] | (s_fifo_buf[i+3] << 8));
        int16_t az = (int16_t)(s_fifo_buf[i+4] | (s_fifo_buf[i+5] << 8));
        float fx = ax * ACCEL_MG_PER_LSB, fy = ay * ACCEL_MG_PER_LSB, fz = az * ACCEL_MG_PER_LSB;
        float mag = sqrtf(fx*fx + fy*fy + fz*fz);
        s_last_mag = mag;
        step_process_sample(mag);
    }
    if (s_step_cb && s_steps > before) s_step_cb(s_steps - before);  // new-step delta
}

// task_coordinator subscriber: drains the FIFO + counts steps. Logs STEPLOG when
// SD logging is on (captured to /sdcard/logs/ for untethered tuning; mirrors PWRLOG).
static void step_sampler_cb(void *user) {
    (void)user;
    if (!s_step_counting) return;
    fifo_drain();
    if (settings_get_sd_logging_enabled()) {
        ESP_LOGI(TAG, "STEPLOG steps=%lu fifo_bytes=%u last_mag=%.0f baseline=%.0f",
                 (unsigned long)s_steps, s_last_nbytes, s_last_mag, s_baseline);
    }
}

// ── public API ──────────────────────────────────────────────────────────────

void imu_manager_idle(void) {
    if (!ensure_init()) return;
    ESP_LOGI(TAG, "QMI8658 → Power-Down (step counting off)");
    imu_powerdown();
    s_step_counting = false;
}

void imu_manager_set_step_counting(bool enable) {
    if (!ensure_init()) return;
    if (enable) {
        // Fresh enable: zero the software detector state.
        s_steps = 0; s_baseline_ok = false; s_peak_armed = true; s_since_step = 0;
        accel_fifo_start();
        s_step_counting = true;
    } else {
        imu_manager_idle();
    }
}

esp_err_t imu_manager_get_steps(uint32_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = s_step_counting ? s_steps : 0;
    return ESP_OK;
}

void imu_manager_reset_steps(void) {
    s_steps = 0; s_baseline_ok = false; s_peak_armed = true; s_since_step = 0;
}

void imu_manager_set_step_cb(void (*cb)(uint32_t new_steps)) {
    s_step_cb = cb;
}

esp_err_t imu_manager_test_read_accel_g(float *x, float *y, float *z) {
    if (!x || !y || !z) return ESP_ERR_INVALID_ARG;
    if (!ensure_init()) return ESP_FAIL;
    esp_err_t err = qmi8658_write_register(&s_imu, QMI8658_CTRL1, CTRL1_ACTIVE);
    if (err != ESP_OK) return err;
    err = qmi8658_enable_sensors(&s_imu, QMI8658_ENABLE_ACCEL);
    if (err != ESP_OK) goto restore;
    vTaskDelay(pdMS_TO_TICKS(20));
    err = qmi8658_read_accel(&s_imu, x, y, z);
restore:
    if (s_step_counting) accel_fifo_start();   // re-establish FIFO (keeps the SW count)
    else imu_powerdown();
    return err;
}
