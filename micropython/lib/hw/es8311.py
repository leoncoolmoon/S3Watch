"""
ES8311 音频编解码器驱动 —— 修复版
- 默认 init() 为扬声器播放模式（DAC输出）
- init_mic() 为麦克风输入模式（ADC输入）
- 修复了播放模式的关键配置
- 添加了音量控制
"""

import time
from machine import Pin

I2C_ADDR = const(0x18)

# ─── 寄存器定义 ───────────────────────────────────────────────
_REG_RESET = const(0x00)
_REG_CLK01 = const(0x01)
_REG_CLK02 = const(0x02)
_REG_CLK03 = const(0x03)
_REG_CLK04 = const(0x04)
_REG_CLK05 = const(0x05)
_REG_CLK06 = const(0x06)
_REG_CLK07 = const(0x07)
_REG_CLK08 = const(0x08)
_REG_SDP_IN = const(0x09)    # DAC 数据端口
_REG_SDP_OUT = const(0x0A)   # ADC 数据端口
_REG_SYS0B = const(0x0B)
_REG_SYS0C = const(0x0C)
_REG_SYS0D = const(0x0D)
_REG_SYS0E = const(0x0E)
_REG_SYS10 = const(0x10)
_REG_SYS11 = const(0x11)
_REG_SYS12 = const(0x12)
_REG_SYS13 = const(0x13)
_REG_SYS14 = const(0x14)
_REG_ADC15 = const(0x15)
_REG_ADC16 = const(0x16)
_REG_ADC17 = const(0x17)
_REG_ALC1B = const(0x1B)
_REG_ALC1C = const(0x1C)
_REG_DAC31 = const(0x31)
_REG_DAC32 = const(0x32)    # DAC 音量寄存器
_REG_DAC37 = const(0x37)
_REG_GPIO44 = const(0x44)
_REG_GP45 = const(0x45)
_REG_CHD1 = const(0xFD)
_REG_CHD2 = const(0xFE)
_REG_CHVER = const(0xFF)

# ─── 时钟配置（MCLK=4.096MHz, Fs=16kHz）────────────────────
_COEFF_PRE_DIV = 1
_COEFF_PRE_MULTI = 0
_COEFF_ADC_DIV = 1
_COEFF_DAC_DIV = 1
_COEFF_FS_MODE = 0
_COEFF_LRCK_H = 0x00
_COEFF_LRCK_L = 0xFF
_COEFF_BCLK_DIV = 4
_COEFF_ADC_OSR = 0x10
_COEFF_DAC_OSR = 0x10

_MIC_GAIN_TABLE = {
    0: 0x00, 6: 0x01, 12: 0x02, 18: 0x03,
    24: 0x04, 30: 0x05, 36: 0x06, 42: 0x07,
}


class ES8311:
    """ES8311 编解码器驱动（默认：扬声器播放模式）"""

    def __init__(self, device, pa_pin=None, mic_gain_db=24):
        """
        device: i2c.I2C.Device 实例
        pa_pin: 扬声器功放使能引脚号（通常是 GPIO46）
        mic_gain_db: 麦克风 PGA 增益，默认 24dB
        """
        self.dev = device
        self._pa_pin_num = pa_pin
        self._pa_pin = None
        self._mic_gain_db = mic_gain_db
        self._powered = False
        self._mode = None  # 'playback' or 'mic'

    def _read_reg(self, reg):
        return self.dev.read_mem(reg, 1)[0]

    def _write_reg(self, reg, val):
        self.dev.write_mem(reg, bytes([val & 0xFF]))

    def _update_reg(self, reg, mask, val):
        """读-改-写：清除 mask 中的位，设置 val 中的位"""
        cur = self._read_reg(reg)
        self._write_reg(reg, (cur & ~mask) | (val & mask))

    def _config_clocks(self):
        """配置时钟（MCLK=4.096MHz, Fs=16kHz）"""
        self._write_reg(_REG_CLK01, 0x3F)
        reg02 = self._read_reg(_REG_CLK02)
        reg02 &= 0x07
        reg02 |= ((_COEFF_PRE_DIV - 1) << 5)
        reg02 |= (_COEFF_PRE_MULTI << 3)
        self._write_reg(_REG_CLK02, reg02)
        self._write_reg(_REG_CLK03, (_COEFF_FS_MODE << 6) | _COEFF_ADC_OSR)
        self._write_reg(_REG_CLK04, _COEFF_DAC_OSR)
        self._write_reg(_REG_CLK05, ((_COEFF_ADC_DIV - 1) << 4) | (_COEFF_DAC_DIV - 1))
        reg06 = self._read_reg(_REG_CLK06)
        reg06 &= 0xE0
        reg06 |= (_COEFF_BCLK_DIV & 0x1F)
        self._write_reg(_REG_CLK06, reg06)
        self._write_reg(_REG_CLK07, _COEFF_LRCK_H)
        self._write_reg(_REG_CLK08, _COEFF_LRCK_L)

    def _config_format(self):
        """配置 I2S 格式：从机模式、标准 I2S、16-bit"""
        reg00 = self._read_reg(_REG_RESET)
        reg00 &= 0xBF  # bit6=0 → 从机模式
        self._write_reg(_REG_RESET, reg00)
        # DAC 和 ADC 都配置为 I2S 标准、16-bit
        self._write_reg(_REG_SDP_OUT, 0x0C)
        self._write_reg(_REG_SDP_IN, 0x0C)

    # ─── 公共初始化方法 ───────────────────────────────────────────

    def init(self):
        """
        初始化扬声器播放模式（DAC 输出）
        - 这是默认模式，适用于播放音频
        - DAC 取消静音，输出使能
        """
        return self.init_playback()

    def init_playback(self):
        """
        扬声器播放模式初始化（DAC 输出）
        关键：DAC 取消静音，SDP_IN bit6=0
        """
        print("ES8311: 初始化播放模式...")
        
        # 1. PA 引脚初始化为低（关闭功放）
        if self._pa_pin_num is not None:
            self._pa_pin = Pin(self._pa_pin_num, Pin.OUT, value=0)
        time.sleep_ms(10)

        # 2. 基础初始化序列
        self._write_reg(_REG_GPIO44, 0x08)
        self._write_reg(_REG_CLK01, 0x30)
        self._write_reg(_REG_CLK02, 0x00)
        self._write_reg(_REG_CLK03, 0x10)
        self._write_reg(_REG_CLK04, 0x10)
        self._write_reg(_REG_CLK05, 0x00)
        self._write_reg(_REG_SYS0B, 0x00)
        self._write_reg(_REG_SYS0C, 0x00)
        self._write_reg(_REG_SYS10, 0x1F)
        self._write_reg(_REG_SYS11, 0x7F)
        self._write_reg(_REG_RESET, 0x80)
        time.sleep_ms(20)

        # 3. 配置时钟
        self._config_clocks()

        # 4. 配置 I2S 格式
        self._config_format()

        # 5. 播放模式专用配置
        self._write_reg(_REG_SYS13, 0x10)      # HP/输出驱动使能
        self._write_reg(_REG_ALC1B, 0x0A)
        self._write_reg(_REG_ALC1C, 0x6A)
        self._write_reg(_REG_SYS0D, 0x01)      # 模拟部分上电
        time.sleep_ms(50)

        # ★★★ 关键：DAC 取消静音 ★★★
        self._write_reg(_REG_SYS12, 0x00)      # DAC 电源使能
        self._write_reg(_REG_SDP_IN, 0x0C)     # bit6=0 → DAC 不静音！
        self._write_reg(_REG_DAC31, 0x00)      # DAC 音量寄存器，0=满量程
        
        # 辅助配置
        self._write_reg(_REG_GP45, 0x00)
        self._write_reg(_REG_GPIO44, 0x58)
        self._write_reg(_REG_ADC17, 0xBF)

        # 修正：_mode 标记要在 set_volume() 之前设置，不然 set_volume() 内部
        # 检查 self._mode != 'playback' 时标记还没设上，会打印多余的假警告
        self._powered = True
        self._mode = 'playback'

        # 6. 设置默认音量为最大（0dB）
        self.set_volume(0)

        print("ES8311: 播放模式初始化完成（16kHz DAC，从机模式）")
        return self

    def init_mic(self):
        """
        麦克风输入模式初始化（ADC 输入）
        用于录音场景，DAC 会被静音
        """
        print("ES8311: 初始化麦克风模式...")
        
        if self._pa_pin_num is not None:
            self._pa_pin = Pin(self._pa_pin_num, Pin.OUT, value=0)
        time.sleep_ms(10)

        self._write_reg(_REG_GPIO44, 0x08)
        self._write_reg(_REG_CLK01, 0x30)
        self._write_reg(_REG_CLK02, 0x00)
        self._write_reg(_REG_CLK03, 0x10)
        self._write_reg(_REG_CLK04, 0x10)
        self._write_reg(_REG_CLK05, 0x00)
        self._write_reg(_REG_SYS0B, 0x00)
        self._write_reg(_REG_SYS0C, 0x00)
        self._write_reg(_REG_SYS10, 0x1F)
        self._write_reg(_REG_SYS11, 0x7F)
        self._write_reg(_REG_RESET, 0x80)
        time.sleep_ms(20)

        self._config_clocks()
        self._config_format()

        self._write_reg(_REG_SYS13, 0x10)
        self._write_reg(_REG_ALC1B, 0x0A)
        self._write_reg(_REG_ALC1C, 0x6A)
        self._write_reg(_REG_SDP_OUT, 0x0C)    # ADC 输出不静音
        self._write_reg(_REG_ADC17, 0xBF)
        self._write_reg(_REG_SYS0E, 0x02)
        self._write_reg(_REG_SYS12, 0x00)
        self._write_reg(_REG_SYS14, 0x1A)
        self._write_reg(_REG_SYS0D, 0x01)
        time.sleep_ms(50)
        self._write_reg(_REG_ADC15, 0x40)
        self._write_reg(_REG_DAC37, 0x08)
        self._write_reg(_REG_GP45, 0x00)
        self._write_reg(_REG_GPIO44, 0x58)

        self.set_mic_gain(self._mic_gain_db)

        # ★★★ 麦克风模式：DAC 静音 ★★★
        self._write_reg(_REG_DAC31, 0x00)
        self._write_reg(_REG_SDP_IN, 0x4C)     # bit6=1 → DAC 静音

        self._powered = True
        self._mode = 'mic'
        print("ES8311: 麦克风模式初始化完成（16kHz ADC，从机模式）")
        return self

    # ─── 功放控制 ─────────────────────────────────────────────────

    def enable_speaker(self, enable=True):
        """
        打开/关闭扬声器功放（PA_CTRL 引脚）
        注意：只有在播放模式下才有意义
        """
        if self._pa_pin is not None:
            self._pa_pin.value(1 if enable else 0)
            print(f"ES8311: PA {'ON' if enable else 'OFF'}")
        else:
            print("ES8311: 警告 - 未配置 PA 引脚")

    # ─── 音量控制 ─────────────────────────────────────────────────

    def set_volume(self, level=0):
        """
        设置 DAC 音量（播放模式）
        level: -128 ~ 0 (dB)
        0 = 满量程（最大音量）
        -128 = 静音
        常用值：-6, -12, -18, -24 等
        """
        if self._mode != 'playback':
            print("ES8311: 警告 - 当前不是播放模式，音量设置可能无效")
        
        # REG32 寄存器：0x00=静音，0xBF=0dB，范围 0x00~0xBF
        # level 是负值，我们映射到 0xBF + level
        val = max(0x00, min(0xBF, 0xBF + level))
        self._write_reg(_REG_DAC32, val)
        print(f"ES8311: DAC 音量设置为 {level}dB (reg=0x{val:02X})")

    def get_volume(self):
        """获取当前 DAC 音量寄存器值"""
        return self._read_reg(_REG_DAC32)

    # ─── 麦克风控制 ──────────────────────────────────────────────

    def set_mic_gain(self, db):
        """设置麦克风 PGA 增益（0, 6, 12, 18, 24, 30, 36, 42 dB）"""
        valid = sorted(_MIC_GAIN_TABLE.keys())
        best = valid[0]
        for v in valid:
            if v <= db:
                best = v
        self._write_reg(_REG_ADC16, _MIC_GAIN_TABLE[best])
        print(f"ES8311: 麦克风增益设置为 {best}dB")

    def set_adc_volume(self, vol):
        """设置 ADC 数字音量（0x00=最小, 0xBF=0dB, 0xFF=最大）"""
        self._write_reg(_REG_ADC17, vol)

    # ─── 静音控制 ─────────────────────────────────────────────────

    def mute(self, enable=True):
        """
        ADC 输出静音控制（影响录音/麦克风输入）
        不影响 DAC 播放
        """
        if enable:
            self._update_reg(_REG_SDP_OUT, 0x40, 0x40)
        else:
            self._update_reg(_REG_SDP_OUT, 0x40, 0x00)

    def mute_dac(self, enable=True):
        """
        DAC 输出静音控制（影响播放）
        """
        if enable:
            self._update_reg(_REG_SDP_IN, 0x40, 0x40)
        else:
            self._update_reg(_REG_SDP_IN, 0x40, 0x00)

    # ─── 状态查询 ─────────────────────────────────────────────────

    @property
    def is_powered(self):
        return self._powered

    @property
    def mode(self):
        return self._mode

    # ─── 调试 ─────────────────────────────────────────────────

    def dump_regs(self):
        """打印所有寄存器状态（调试用）"""
        regs = [
            0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
            0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
            0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
            0x1B, 0x1C,
            0x31, 0x32, 0x37,
            0x44, 0x45,
            0xFD, 0xFE, 0xFF
        ]
        print("ES8311 寄存器转储:")
        for r in regs:
            try:
                val = self._read_reg(r)
                print(f"  REG{hex(r)[2:].upper():02s}: 0x{val:02X}")
            except:
                print(f"  REG{hex(r)[2:].upper():02s}: ERROR")