"""
FT3168 触摸驱动 —— 备用方案，走 i2c.I2C.Device。

寄存器表和复位时序来自 turfptax/ESP32Watch 项目里实际跑通的 ft3168.py，
是真实的 FT3168 寄存器（不是猜的 FT6x36 兼容），置信度高。

优先级：先试 boot_sequence_example.py 里内置的 ft6x36（省事，协议大部分兼容），
不行或者行为有差异（比如触摸区域偏移、灵敏度不对）再换这个。
"""
import time

I2C_ADDR = const(0x38)

_REG_TD_STATUS = const(0x02)   # 触摸点数量
_REG_P1_XH = const(0x03)
_REG_TH_GROUP = const(0x80)    # 触摸阈值
_REG_G_MODE = const(0xA4)      # 中断模式：0x00=轮询
_REG_CHIP_ID = const(0xA3)
_REG_FOCALTECH_ID = const(0xA8)

EVENT_PRESS_DOWN = const(0x00)
EVENT_LIFT_UP = const(0x01)
EVENT_CONTACT = const(0x02)


class FT3168:
    def __init__(self, device, rst_pin=None):
        """
        device: i2c.I2C.Device 实例
        rst_pin: machine.Pin(OUT) 对象，传了会在 init() 里做硬件复位
        """
        self.dev = device
        self._rst = rst_pin
        self._buf = bytearray(14)

    def init(self):
        if self._rst is not None:
            self._rst.value(0)
            time.sleep_ms(10)  # NOQA
            self._rst.value(1)
            time.sleep_ms(300)  # NOQA  turfptax 项目验证过的等待时间

        chip_id = self.dev.read_mem(_REG_CHIP_ID, 1)[0]
        vendor_id = self.dev.read_mem(_REG_FOCALTECH_ID, 1)[0]
        print(f"FT3168 chip_id=0x{chip_id:02X} vendor_id=0x{vendor_id:02X}")

        self.dev.write_mem(_REG_TH_GROUP, bytes([22]))   # 触摸阈值，数值越小越灵敏
        self.dev.write_mem(_REG_G_MODE, bytes([0x00]))   # 轮询模式

    def read(self):
        """返回 [(x, y, event), ...] 列表，没有触摸时是空列表"""
        data = self.dev.read_mem(_REG_TD_STATUS, 13)
        num_points = data[0] & 0x0F
        if num_points == 0 or num_points > 2:
            return []

        points = []
        for i in range(num_points):
            offset = 1 + i * 6
            event = (data[offset] >> 6) & 0x03
            x = ((data[offset] & 0x0F) << 8) | data[offset + 1]
            y = ((data[offset + 2] & 0x0F) << 8) | data[offset + 3]
            points.append((x, y, event))
        return points
