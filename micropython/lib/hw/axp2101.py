"""
AXP2101 电源管理驱动 —— 走 i2c.I2C.Device，共用 i2c_bus。

本版基于你提供的 turfptax/ESP32Watch 项目里 axp2101.py 的寄存器映射做了修正，
修正了一个严重错误：ALDO1 的开关寄存器不是 0x92（那是电压设置寄存器），
而是 0x90（_REG_LDO_ONOFF0），bit0 对应 ALDO1。之前那版一直在改电压值，
没有真正打开这路电源轨。

置信度：
- DCDC 开关寄存器 0x80、LDO 开关寄存器 0x90、芯片 ID 寄存器 0x03（期望 0x4B）、
  电池电量寄存器 0xA4，这些都来自 turfptax 项目里实际在跑的驱动，置信度高。
- 电压设置寄存器（ALDO1_VOL=0x92 等）先没用上，默认电压是否已经适合屏幕
  没有验证，如果开轨之后屏幕还是不亮，下一步怀疑对象是这里要不要显式设置电压。
"""

I2C_ADDR = const(0x34)

_REG_IC_TYPE = const(0x03)       # 芯片 ID，期望读到 0x4B
_REG_STATUS1 = const(0x00)
_REG_STATUS2 = const(0x01)

_REG_ADC_ENABLE = const(0x30)
_REG_VBAT_H = const(0x34)
_REG_VBAT_L = const(0x35)
_REG_BAT_PERCENT = const(0xA4)

# 电源开关寄存器 —— 这两个是"开关"，不是"电压"
_REG_DCDC_ONOFF = const(0x80)    # bit(channel-1) 对应 DCDC1~5
_REG_LDO_ONOFF0 = const(0x90)    # bit(channel-1) 对应 ALDO1~4

_EXPECTED_CHIP_ID = const(0x4A)  # 修正：0x4A 才是 AXP2101 官方数据手册规定的真实
                                    # 芯片 ID，之前的 0x4B 是从 AXP192/AXP202 那些
                                    # 老驱动模板抄错传下来的


class AXP2101:
    def __init__(self, device):
        """
        device: i2c.I2C.Device 实例，构造方式：
            dev = i2c.I2C.Device(i2c_bus, dev_id=axp2101.I2C_ADDR, reg_bits=8)
            pmu = AXP2101(dev)
        """
        self.dev = device

    def _read8(self, reg):
        return self.dev.read_mem(reg, 1)[0]

    def _write8(self, reg, val):
        self.dev.write_mem(reg, bytes([val & 0xFF]))

    def _set_bit(self, reg, bit, enable):
        val = self._read8(reg)
        if enable:
            val |= bit
        else:
            val &= ~bit
        self._write8(reg, val)

    def check_chip_id(self):
        """校验芯片 ID，接错板子或者地址不对的话这里会先报错，而不是后面莫名其妙黑屏"""
        chip_id = self._read8(_REG_IC_TYPE)
        if chip_id != _EXPECTED_CHIP_ID:
            print(f"⚠️ AXP2101 芯片 ID 读到 0x{chip_id:02X}，期望 0x{_EXPECTED_CHIP_ID:02X}")
        return chip_id == _EXPECTED_CHIP_ID

    def power_on_main_rail(self):
        """开主 3.3V 轨（DCDC1），一般最先调用"""
        self._set_bit(_REG_DCDC_ONOFF, 0x01, True)

    def power_on_display(self):
        """开屏幕面板供电轨。之前只开 ALDO1，现在改成跟"确认能跑通"的
        原始 bring-up 脚本一致：ALDO1-4 + BLDO1-2 全部打开，因为不确定
        面板具体接的是哪一路。"""
        self._set_bit(_REG_LDO_ONOFF0, 0x0F, True)  # ALDO1-4
        # BLDO1-2 使能寄存器 0x91
        self._set_bit(0x91, 0x03, True)

    def power_on_all_known(self):
        self.check_chip_id()
        self.power_on_main_rail()
        self.power_on_display()

    def enable_dcdc(self, channel, enable=True):
        """channel: 1-5"""
        if not 1 <= channel <= 5:
            raise ValueError("DCDC channel must be 1-5")
        self._set_bit(_REG_DCDC_ONOFF, 1 << (channel - 1), enable)

    def enable_aldo(self, channel, enable=True):
        """channel: 1-4"""
        if not 1 <= channel <= 4:
            raise ValueError("ALDO channel must be 1-4")
        self._set_bit(_REG_LDO_ONOFF0, 1 << (channel - 1), enable)

    # ─── 电池状态 ──────────────────────────────────────────────
    def enable_battery_adc(self):
        self._write8(_REG_ADC_ENABLE, 0x03)

    @property
    def battery_percent(self):
        return self._read8(_REG_BAT_PERCENT) & 0x7F

    @property
    def battery_voltage_raw(self):
        """原始 14-bit ADC 值。高字节(0x34)取低6位，低字节(0x35)8位全取，
        拼接成 14-bit：raw = (0x34的低6位 << 8) | 0x35"""
        h = self._read8(_REG_VBAT_H)
        l = self._read8(_REG_VBAT_L)
        raw = ((h & 0x3F) << 8) | l
        return raw

    @property
    def battery_voltage_mv(self):
        """电池电压，单位 mV。换算比例是 1 LSB = 1.0mV（不是之前用的 1.1）"""
        return self.battery_voltage_raw * 1.0

    @property
    def is_charging(self):
        return bool(self._read8(_REG_STATUS2) & 0x60)

    @property
    def is_vbus_present(self):
        return bool(self._read8(_REG_STATUS1) & 0x20)

    def get_battery_info(self):
        return {
            "voltage_mv": self.battery_voltage_mv,
            "percentage": self.battery_percent,
            "charging": self.is_charging,
            "vbus": self.is_vbus_present
        }