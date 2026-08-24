"""
QMI8658 6-Axis IMU Driver wrapper for backwards compatibility
"""

import math
from lib.hw.qmi8658 import QMI8658 as _QMI8658

class QMI8658(_QMI8658):
    def __init__(self, i2c=None):
        super().__init__(i2c=i2c)
        self.steps = 0
        self.last_magnitude = 0.0
        self.peak_threshold = 1.2
        self.step_cooldown = 0

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
