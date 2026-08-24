"""
QMI8658 6-Axis IMU Driver for MicroPython
Target Board: Waveshare ESP32-S3-Touch-AMOLED-2.06
"""

import time
import math

if not hasattr(time, "sleep_ms"):
    time.sleep_ms = lambda ms: time.sleep(ms / 1000.0)

from myboard import I2C_ADDR_IMU

class QMI8658:
    I2C_ADDR = I2C_ADDR_IMU

    REG_WHO_AM_I = 0x00
    REG_CTRL1 = 0x02
    REG_CTRL2 = 0x03
    REG_CTRL3 = 0x04
    REG_CTRL7 = 0x0A
    REG_AX_L = 0x35

    def __init__(self, i2c=None):
        self.i2c = i2c
        self.gyro_offset = (0, 0, 0)
        self.steps = 0
        self.last_magnitude = 0.0
        self.peak_threshold = 1.2 # g threshold for step detection
        self.step_cooldown = 0
        self.init_imu()

    def init_imu(self):
        if not self.i2c:
            return
        try:
            who = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_WHO_AM_I, 1)[0]
            if who in (0x05, 0x06):
                # Enable Accel & Gyro in CTRL7
                self.i2c.writeto_mem(self.I2C_ADDR, self.REG_CTRL7, bytes([0x03]))
                # CTRL2: Accel 8g scale
                self.i2c.writeto_mem(self.I2C_ADDR, self.REG_CTRL2, bytes([0x33]))
        except Exception:
            pass

    def read_raw(self):
        """
        Reads 16-bit raw values for accel and gyro.
        Returns: (ax, ay, az, gx, gy, gz)
        """
        if not self.i2c:
            return (0, 0, 16384, 0, 0, 0)
        try:
            data = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_AX_L, 12)
            ax = (data[1] << 8 | data[0])
            ay = (data[3] << 8 | data[2])
            az = (data[5] << 8 | data[4])
            gx = (data[7] << 8 | data[6])
            gy = (data[9] << 8 | data[8])
            gz = (data[11] << 8 | data[10])

            if ax >= 32768: ax -= 65536
            if ay >= 32768: ay -= 65536
            if az >= 32768: az -= 65536
            if gx >= 32768: gx -= 65536
            if gy >= 32768: gy -= 65536
            if gz >= 32768: gz -= 65536

            return (ax, ay, az, gx, gy, gz)
        except Exception:
            return (0, 0, 16384, 0, 0, 0)

    def read_accel_g(self):
        """
        Returns (ax, ay, az) in units of g (±8g scale, 4096 LSB/g)
        """
        raw = self.read_raw()
        scale = 4096.0 # ±8g range -> 4096 LSB/g
        return (raw[0] / scale, raw[1] / scale, raw[2] / scale)

    def read_accel(self):
        return self.read_accel_g()

    def read_pedometer(self):
        ax, ay, az = self.read_accel_g()
        magnitude = math.sqrt(ax * ax + ay * ay + az * az)

        if self.step_cooldown > 0:
            self.step_cooldown -= 1
        else:
            if magnitude > self.peak_threshold and self.last_magnitude <= self.peak_threshold:
                self.steps += 1
                self.step_cooldown = 3

        self.last_magnitude = magnitude
        return self.steps

    def reset_step_counter(self):
        self.steps = 0

    def read_gyro_raw(self):
        """
        Returns (gx, gy, gz) 16-bit raw gyroscope values
        """
        raw = self.read_raw()
        return (raw[3], raw[4], raw[5])

    def calibrate_gyro(self, num_samples=200, sample_delay_ms=5):
        """
        Calibrates gyroscope offset over num_samples.
        Saves offset internally and returns (off_x, off_y, off_z).
        """
        sum_x = sum_y = sum_z = 0
        samples = 0
        for _ in range(num_samples):
            gx, gy, gz = self.read_gyro_raw()
            sum_x += gx
            sum_y += gy
            sum_z += gz
            samples += 1
            time.sleep_ms(sample_delay_ms)

        if samples > 0:
            off_x = int(sum_x / samples)
            off_y = int(sum_y / samples)
            off_z = int(sum_z / samples)
            self.gyro_offset = (off_x, off_y, off_z)
        return self.gyro_offset

    def read_gyro_calibrated(self):
        """
        Returns (gx, gy, gz) with calibrated offset subtracted
        """
        gx, gy, gz = self.read_gyro_raw()
        ox, oy, oz = self.gyro_offset
        return (gx - ox, gy - oy, gz - oz)
