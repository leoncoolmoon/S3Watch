"""
ES7210 Audio ADC / Microphone Driver for MicroPython
Target Board: Waveshare ESP32-S3-Touch-AMOLED-2.06
"""

from myboard import I2C_ADDR_AUDIO_IN

class ES7210:
    I2C_ADDR = I2C_ADDR_AUDIO_IN

    REG_RESET = 0x00
    REG_MAIN_CLK = 0x01
    REG_LRCK_DIV = 0x02
    REG_ADC_CONTROL = 0x03
    REG_PGA_GAIN1 = 0x1E
    REG_PGA_GAIN2 = 0x1F

    def __init__(self, i2c=None, mic_gain_db=12):
        self.i2c = i2c
        self.mic_gain_db = mic_gain_db
        self.init()

    def init(self):
        if not self.i2c:
            return
        try:
            # Soft reset
            self.i2c.writeto_mem(self.I2C_ADDR, self.REG_RESET, bytes([0xFF]))
            self.i2c.writeto_mem(self.I2C_ADDR, self.REG_RESET, bytes([0x41]))

            # Clock & ADC Control
            self.i2c.writeto_mem(self.I2C_ADDR, self.REG_MAIN_CLK, bytes([0x20]))
            self.i2c.writeto_mem(self.I2C_ADDR, self.REG_ADC_CONTROL, bytes([0x00]))

            self.set_mic_gain(self.mic_gain_db)
        except Exception as e:
            print(f"ES7210 init error: {e}")

    def set_mic_gain(self, db):
        self.mic_gain_db = db
        if not self.i2c:
            return
        try:
            # Map db (-12 ~ +30 dB) to PGA gain register
            val = max(0, min(0x0F, int((db + 12) / 3)))
            self.i2c.writeto_mem(self.I2C_ADDR, self.REG_PGA_GAIN1, bytes([(val << 4) | val]))
            self.i2c.writeto_mem(self.I2C_ADDR, self.REG_PGA_GAIN2, bytes([(val << 4) | val]))
        except Exception:
            pass

    def standby(self):
        if not self.i2c:
            return
        try:
            # Enter standby mode
            self.i2c.writeto_mem(self.I2C_ADDR, self.REG_ADC_CONTROL, bytes([0xFF]))
        except Exception:
            pass
