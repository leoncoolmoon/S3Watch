"""
TCA9554 I2C GPIO Expander Driver for MicroPython
Target Board: Waveshare ESP32-S3-Touch-AMOLED-2.06
"""

from myboard import I2C_ADDR_GPIO_EXPANDER

class TCA9554:
    I2C_ADDR = I2C_ADDR_GPIO_EXPANDER

    REG_INPUT = 0x00
    REG_OUTPUT = 0x01
    REG_POLARITY = 0x02
    REG_CONFIG = 0x03

    def __init__(self, i2c=None):
        self.i2c = i2c

    def set_all_output_high(self):
        if not self.i2c:
            return
        try:
            # Set all pins as output (Config 0x00) and drive all high (Output 0xFF)
            self.i2c.writeto_mem(self.I2C_ADDR, self.REG_CONFIG, bytes([0x00]))
            self.i2c.writeto_mem(self.I2C_ADDR, self.REG_OUTPUT, bytes([0xFF]))
        except Exception as e:
            print(f"TCA9554 set_all_output_high error: {e}")

    def read_status(self):
        """
        Returns (config, output, input) register bytes.
        """
        if not self.i2c:
            return (0x00, 0xFF, 0x00)
        try:
            cfg = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_CONFIG, 1)[0]
            out = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_OUTPUT, 1)[0]
            inp = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_INPUT, 1)[0]
            return (cfg, out, inp)
        except Exception:
            return (0x00, 0xFF, 0x00)
