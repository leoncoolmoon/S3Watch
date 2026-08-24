"""
TCA9554 I2C GPIO 扩展器驱动 —— 从"确认能点亮屏幕"的原始 bring-up 脚本
(test_display_bringup.py) 里挖出来的，我们之前完全不知道这个芯片存在。

原脚本的做法：全部引脚设为输出，全部拉高。不确定具体哪个引脚对应什么功能
（原脚本注释里也是"不知道具体是哪个，全开"），先照抄这个"全开"策略，
能点亮屏幕再回头细化成"只开必要的那个引脚"。

标准 TCA9554 寄存器布局：
  0x00 = Input Port
  0x01 = Output Port
  0x02 = Polarity Inversion
  0x03 = Configuration (0=output, 1=input)
"""

I2C_ADDR = const(0x40)  # 待确认——跟 ES7210 音频编解码器的地址冲突，
                         # 具体这颗芯片是不是真的在 0x40，需要上板用
                         # i2c.scan() 结果交叉验证

_REG_INPUT = const(0x00)
_REG_OUTPUT = const(0x01)
_REG_POLARITY = const(0x02)
_REG_CONFIG = const(0x03)


class TCA9554:
    def __init__(self, device):
        """
        device: i2c.I2C.Device 实例，用法跟其他驱动一样：
            dev = i2c.I2C.Device(i2c_bus, dev_id=tca9554.I2C_ADDR, reg_bits=8)
            expander = TCA9554(dev)
        """
        self.dev = device

    def _read8(self, reg):
        return self.dev.read_mem(reg, 1)[0]

    def _write8(self, reg, val):
        self.dev.write_mem(reg, bytes([val & 0xFF]))

    def set_all_output_high(self):
        """照抄原始 bring-up 脚本的做法：全部引脚设为输出并拉高"""
        self._write8(_REG_CONFIG, 0x00)   # 全部设为输出
        self._write8(_REG_OUTPUT, 0xFF)   # 全部拉高

    def read_status(self):
        """调试用：读出当前配置和输出状态"""
        cfg = self._read8(_REG_CONFIG)
        out = self._read8(_REG_OUTPUT)
        inp = self._read8(_REG_INPUT)
        return cfg, out, inp
