"""
QMI8658 6-Axis Motion Sensor & Step Counter Driver for MicroPython
Reads accelerometer, gyroscope data, and processes software step counting.
"""

import math

class QMI8658:
    I2C_ADDR = 0x6B

    REG_WHO_AM_I = 0x00
    REG_CTRL1 = 0x02
    REG_CTRL2 = 0x03
    REG_CTRL7 = 0x0A
    REG_AX_L = 0x35

    def __init__(self, i2c=None):
        self.i2c = i2c
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
                # CTRL2: Accel 4g scale
                self.i2c.writeto_mem(self.I2C_ADDR, self.REG_CTRL2, bytes([0x23]))
        except Exception as e:
            print(f"QMI8658 init error: {e}")

    def read_accel(self):
        if not self.i2c:
            return (0.0, 0.0, 1.0)
        try:
            data = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_AX_L, 6)
            ax = (data[1] << 8 | data[0])
            ay = (data[3] << 8 | data[2])
            az = (data[5] << 8 | data[4])

            # Convert signed 16-bit
            if ax >= 32768: ax -= 65536
            if ay >= 32768: ay -= 65536
            if az >= 32768: az -= 65536

            # Scale to g (4g range -> 8192 LSB/g)
            scale = 8192.0
            return (ax / scale, ay / scale, az / scale)
        except Exception as e:
            print(f"QMI8658 read error: {e}")
            return (0.0, 0.0, 1.0)

    def read_pedometer(self):
        ax, ay, az = self.read_accel()
        magnitude = math.sqrt(ax*ax + ay*ay + az*az)

        if self.step_cooldown > 0:
            self.step_cooldown -= 1
        else:
            if magnitude > self.peak_threshold and self.last_magnitude <= self.peak_threshold:
                self.steps += 1
                self.step_cooldown = 3 # Prevent double counting

        self.last_magnitude = magnitude
        return self.steps

    def reset_step_counter(self):
        self.steps = 0
