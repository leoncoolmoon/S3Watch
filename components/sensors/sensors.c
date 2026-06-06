// QMI8658 IMU power-down helper.
//
// Why this exists: the QMI8658 is hardwired to VCC3V3 (always on, can't be
// gated by ALDOs) and powers up with accel + gyro enabled, drawing ~270 µA
// continuously. A few I2C writes drop it to ~6 µA "Power-Down" mode (per the
// QMI8658A datasheet Rev A, Section 7.1). Step counting / motion
// classification was previously implemented here but has been removed.

#include "sensors.h"
#include "bsp/esp32_s3_touch_amoled_2_06.h"
#include "esp_log.h"
#include "qmi8658.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define IMU_ADDR_HIGH QMI8658_ADDRESS_HIGH
#define IMU_ADDR_LOW  QMI8658_ADDRESS_LOW

static const char *TAG = "SENSORS";
static qmi8658_dev_t s_imu;

void sensors_low_power_idle(void) {
  ESP_LOGI(TAG, "QMI8658: putting IMU in Power-Down (step counter disabled)");
  if (bsp_i2c_init() != ESP_OK) {
    ESP_LOGE(TAG, "I2C not available; cannot suspend IMU");
    return;
  }
  if (qmi8658_init(&s_imu, bsp_i2c_get_handle(), IMU_ADDR_HIGH) != ESP_OK &&
      qmi8658_init(&s_imu, bsp_i2c_get_handle(), IMU_ADDR_LOW)  != ESP_OK) {
    ESP_LOGE(TAG, "QMI8658 not found at either address");
    return;
  }
  // Disable accel + gyro: drops to "Power-On Default" mode (~15 µA).
  (void)qmi8658_enable_sensors(&s_imu, QMI8658_DISABLE_ALL);
  // CTRL1.SensorDisable=1 also disables the internal high-speed oscillator,
  // taking the chip to "Power-Down" mode (~6 µA). CTRL1 default is 0x20
  // (BE=1, all other bits 0); we OR in bit 0.
  (void)qmi8658_write_register(&s_imu, QMI8658_CTRL1, 0x21);
}

esp_err_t sensors_test_read_accel_g(float *x, float *y, float *z) {
  if (!x || !y || !z) return ESP_ERR_INVALID_ARG;
  if (bsp_i2c_init() != ESP_OK) return ESP_FAIL;
  // Wake: clear SensorDisable, restore default CTRL1 (BE=1).
  esp_err_t err = qmi8658_write_register(&s_imu, QMI8658_CTRL1, 0x20);
  if (err != ESP_OK) return err;
  // Enable accel; one ODR cycle (~5 ms at default ODR) before reading.
  err = qmi8658_enable_sensors(&s_imu, QMI8658_ENABLE_ACCEL);
  if (err != ESP_OK) goto restore;
  vTaskDelay(pdMS_TO_TICKS(20));  // let it settle a sample or two
  err = qmi8658_read_accel(&s_imu, x, y, z);
restore:
  (void)qmi8658_enable_sensors(&s_imu, QMI8658_DISABLE_ALL);
  (void)qmi8658_write_register(&s_imu, QMI8658_CTRL1, 0x21);
  return err;
}
