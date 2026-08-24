"""
ES8311 Audio DAC / Speaker Driver for MicroPython
Target Board: Waveshare ESP32-S3-Touch-AMOLED-2.06
"""

try:
    import machine
    HARDWARE_AVAILABLE = True
except ImportError:
    HARDWARE_AVAILABLE = False

from myboard import I2C_ADDR_AUDIO_OUT, AUDIO_PA_ENABLE

class ES8311:
    I2C_ADDR = I2C_ADDR_AUDIO_OUT

    REG_RESET = 0x00
    REG_CLK_MANAGER = 0x01
    REG_SYSTEM = 0x02
    REG_ADC_CONTROL = 0x09
    REG_DAC_CONTROL = 0x12
    REG_VOLUME = 0x14
    REG_GPIO = 0x44

    def __init__(self, i2c=None, pa_pin=AUDIO_PA_ENABLE):
        self.i2c = i2c
        self.pa_pin = machine.Pin(pa_pin, machine.Pin.OUT) if HARDWARE_AVAILABLE and pa_pin is not None else None
        self._volume = 0 # 0 dB
        self.init()

    def init(self):
        self.init_playback()

    def init_playback(self):
        if not self.i2c:
            return
        try:
            # Soft reset & init playback registers
            self.i2c.writeto_mem(self.I2C_ADDR, self.REG_RESET, bytes([0x1F]))
            self.i2c.writeto_mem(self.I2C_ADDR, self.REG_RESET, bytes([0x00]))
            self.i2c.writeto_mem(self.I2C_ADDR, self.REG_CLK_MANAGER, bytes([0x30]))
            self.i2c.writeto_mem(self.I2C_ADDR, self.REG_SYSTEM, bytes([0x00]))
            self.set_volume(self._volume)
            self.enable_speaker(True)
        except Exception as e:
            print(f"ES8311 init error: {e}")

    def init_mic(self):
        if not self.i2c:
            return
        try:
            self.mute_dac(True)
            self.i2c.writeto_mem(self.I2C_ADDR, self.REG_ADC_CONTROL, bytes([0x00]))
        except Exception:
            pass

    def enable_speaker(self, enable=True):
        if self.pa_pin:
            self.pa_pin.value(1 if enable else 0)

    def set_volume(self, level=0):
        """
        level: -128 ~ 0 dB (0 is max volume)
        """
        self._volume = max(-128, min(0, level))
        if not self.i2c:
            return
        try:
            # Convert dB to register value (0x00 = 0dB, 0xFF = -127.5dB or muted)
            reg_val = int((-self._volume) * 2) & 0xFF
            self.i2c.writeto_mem(self.I2C_ADDR, self.REG_VOLUME, bytes([reg_val]))
        except Exception:
            pass

    def get_volume(self):
        if not self.i2c:
            return self._volume
        try:
            val = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_VOLUME, 1)[0]
            db = -float(val) / 2.0
            return db
        except Exception:
            return self._volume

    def set_mic_gain(self, db):
        if not self.i2c:
            return
        try:
            val = max(0, min(0x0F, int(db)))
            self.i2c.writeto_mem(self.I2C_ADDR, self.REG_ADC_CONTROL, bytes([val]))
        except Exception:
            pass

    def mute(self, enable=True):
        self.mute_dac(enable)
        self.enable_speaker(not enable)

    def mute_dac(self, enable=True):
        if not self.i2c:
            return
        try:
            curr = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_DAC_CONTROL, 1)[0]
            val = (curr | 0x20) if enable else (curr & ~0x20)
            self.i2c.writeto_mem(self.I2C_ADDR, self.REG_DAC_CONTROL, bytes([val]))
        except Exception:
            pass

    def dump_regs(self):
        if not self.i2c:
            print("ES8311: No I2C bus connected")
            return
        print("--- ES8311 Registers Dump ---")
        for reg in range(0x00, 0x50):
            try:
                val = self.i2c.readfrom_mem(self.I2C_ADDR, reg, 1)[0]
                print(f"Reg 0x{reg:02X}: 0x{val:02X}")
            except Exception:
                pass
