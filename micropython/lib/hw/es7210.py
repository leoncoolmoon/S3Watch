"""
ES7210 四通道麦克风 ADC 驱动 —— 移植自 turfptax/ESP32Watch 项目，
I2C 访问层同样换成 i2c.I2C.Device，寄存器序列保留不变
（来自 Espressif esp-adf 参考驱动，MCLK=4.096MHz, Fs=16kHz）。

注意：MCLK 是通过 PWM 在 GPIO16 上生成的，ES8311 复用同一路 MCLK，
所以初始化顺序必须是 ES7210 先启动（负责起振 MCLK），ES8311 后初始化。
"""
import time
from machine import Pin, PWM

I2C_ADDR = const(0x40)

_REG_RESET = const(0x00)
_REG_CLK_OFF = const(0x01)
_REG_MAINCLK = const(0x02)
_REG_LRCK_DIVH = const(0x04)
_REG_LRCK_DIVL = const(0x05)
_REG_POWER_DOWN = const(0x06)
_REG_OSR = const(0x07)
_REG_TIME_CTL0 = const(0x09)
_REG_TIME_CTL1 = const(0x0A)
_REG_SDP_IF1 = const(0x11)
_REG_SDP_IF2 = const(0x12)
_REG_ADC1_DVOL = const(0x1B)
_REG_ADC2_DVOL = const(0x1C)
_REG_ADC34_HPF2 = const(0x20)
_REG_ADC34_HPF1 = const(0x21)
_REG_ADC12_HPF2 = const(0x22)
_REG_ADC12_HPF1 = const(0x23)
_REG_ANALOG = const(0x40)
_REG_MIC12_BIAS = const(0x41)
_REG_MIC34_BIAS = const(0x42)
_REG_MIC1_GAIN = const(0x43)
_REG_MIC2_GAIN = const(0x44)
_REG_MIC3_GAIN = const(0x45)
_REG_MIC4_GAIN = const(0x46)
_REG_MIC1_PWR = const(0x47)
_REG_MIC2_PWR = const(0x48)
_REG_MIC3_PWR = const(0x49)
_REG_MIC4_PWR = const(0x4A)
_REG_MIC12_PWR = const(0x4B)
_REG_MIC34_PWR = const(0x4C)

_GAIN_TABLE = {
    0: 0x00, 3: 0x01, 6: 0x02, 9: 0x03,
    12: 0x04, 15: 0x05, 18: 0x06, 21: 0x07,
    24: 0x08, 27: 0x09, 30: 0x0A, 33: 0x0B,
    34: 0x0C, 36: 0x0D, 37: 0x0E,
}

_CLK_ADC_DIV = 0x01
_CLK_DLL = 1
_CLK_DOUBLER = 1
_CLK_OSR = 0x20
_CLK_LRCK_H = 0x01
_CLK_LRCK_L = 0x00

_AUDIO_MCLK_FREQ = 4_096_000


class ES7210:
    """ES7210 四通道麦克风 ADC 驱动"""

    def __init__(self, device, mclk_pin=16, mic_gain_db=24):
        """
        device: i2c.I2C.Device 实例
        mclk_pin: MCLK 引脚号（官方引脚表：GPIO16）
        mic_gain_db: 麦克风 PGA 增益，默认 24dB
        """
        self.dev = device
        self._mclk_pin = mclk_pin
        self._mclk_pwm = None
        self._mic_gain_db = mic_gain_db
        self._powered = False

    def _read_reg(self, reg):
        return self.dev.read_mem(reg, 1)[0]

    def _write_reg(self, reg, val):
        self.dev.write_mem(reg, bytes([val & 0xFF]))

    def init(self):
        self._mclk_pwm = PWM(Pin(self._mclk_pin), freq=_AUDIO_MCLK_FREQ, duty_u16=32768)
        time.sleep_ms(20)  # NOQA
        print(f"ES7210: MCLK 已在 GPIO{self._mclk_pin} 启动，频率 {_AUDIO_MCLK_FREQ}Hz")

        self._write_reg(_REG_RESET, 0xFF)
        time.sleep_ms(10)  # NOQA
        self._write_reg(_REG_RESET, 0x32)
        time.sleep_ms(10)  # NOQA

        self._write_reg(_REG_TIME_CTL0, 0x30)
        self._write_reg(_REG_TIME_CTL1, 0x30)

        self._write_reg(_REG_ADC12_HPF1, 0x2A)
        self._write_reg(_REG_ADC12_HPF2, 0x0A)
        self._write_reg(_REG_ADC34_HPF1, 0x2A)
        self._write_reg(_REG_ADC34_HPF2, 0x0A)

        self._write_reg(_REG_SDP_IF1, 0x60)
        self._write_reg(_REG_SDP_IF2, 0x00)

        self._write_reg(_REG_ANALOG, 0xC3)

        self._write_reg(_REG_MIC12_BIAS, 0x70)
        self._write_reg(_REG_MIC34_BIAS, 0x70)

        gain_code = 0x10 | _GAIN_TABLE.get(self._mic_gain_db, 0x08)
        self._write_reg(_REG_MIC1_GAIN, gain_code)
        self._write_reg(_REG_MIC2_GAIN, gain_code)
        self._write_reg(_REG_MIC3_GAIN, gain_code)
        self._write_reg(_REG_MIC4_GAIN, gain_code)

        self._write_reg(_REG_MIC1_PWR, 0x08)
        self._write_reg(_REG_MIC2_PWR, 0x08)
        self._write_reg(_REG_MIC3_PWR, 0x08)
        self._write_reg(_REG_MIC4_PWR, 0x08)

        self._write_reg(_REG_OSR, _CLK_OSR)
        reg02 = _CLK_ADC_DIV | (_CLK_DOUBLER << 6) | (_CLK_DLL << 7)
        self._write_reg(_REG_MAINCLK, reg02)
        self._write_reg(_REG_LRCK_DIVH, _CLK_LRCK_H)
        self._write_reg(_REG_LRCK_DIVL, _CLK_LRCK_L)

        self._write_reg(_REG_POWER_DOWN, 0x04)
        self._write_reg(_REG_MIC12_PWR, 0x0F)
        self._write_reg(_REG_MIC34_PWR, 0x0F)

        self._write_reg(_REG_RESET, 0x71)
        time.sleep_ms(10)  # NOQA
        self._write_reg(_REG_RESET, 0x41)
        time.sleep_ms(50)  # NOQA

        self._write_reg(_REG_CLK_OFF, 0x00)

        self._powered = True
        print("ES7210: 初始化完成（16kHz, 16-bit, I2S slave）")

    def set_mic_gain(self, db):
        valid = sorted(_GAIN_TABLE.keys())
        best = valid[0]
        for v in valid:
            if v <= db:
                best = v
        code = 0x10 | _GAIN_TABLE[best]
        self._write_reg(_REG_MIC1_GAIN, code)
        self._write_reg(_REG_MIC2_GAIN, code)

    def standby(self):
        if not self._powered:
            return
        self._write_reg(_REG_MIC12_PWR, 0xFF)
        self._write_reg(_REG_MIC34_PWR, 0xFF)
        self._write_reg(_REG_CLK_OFF, 0xFF)
        if self._mclk_pwm:
            self._mclk_pwm.deinit()
            self._mclk_pwm = None
        self._powered = False

    @property
    def is_powered(self):
        return self._powered
