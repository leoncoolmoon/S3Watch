"""
QMI8658 六轴 IMU 驱动 —— 跟 AXP2101/PCF85063 一样走 i2c.I2C.Device，
共用同一条 i2c_bus。

置信度说明：
- 寄存器地址（WHO_AM_I/CTRL1~CTRL3/CTRL7/数据起始地址 0x35）来自 QST 官方
  QMI8658C 数据手册，标准可信。
- 初始化的具体配置值 CTRL1=0x60, CTRL2=0x23, CTRL3=0x43, CTRL7=0x03，
  来自另一个用在同类 Waveshare 板子上（ESP32-S3-Touch-LCD-2）的开源项目，
  是已经跑通验证过的一组值，不是我自己拼凑的默认值。
- 加速度计量程解出来是 ±8G（CTRL2=0x23 的高4位 0x20），换算成 g 的公式
  （raw/4096）是按这个量程推的，置信度高。
- 陀螺仪量程（CTRL3=0x43 对应多少 °/s）我没能从手册里对上具体数值，
  所以这版先只返回陀螺仪原始 16bit 值，不做单位换算，等你需要精确角速度
  数值时我们再对着数据手册的 CTRL3 表核实。
"""

I2C_ADDR = const(0x6B)  # 常见地址；如果 SA0 接法不同可能是 0x6A，以 i2c 扫描结果为准

_REG_WHO_AM_I = const(0x00)
_REG_REVISION_ID = const(0x01)
_REG_CTRL1 = const(0x02)
_REG_CTRL2 = const(0x03)
_REG_CTRL3 = const(0x04)
_REG_CTRL7 = const(0x08)
_REG_STATUS0 = const(0x2E)
_REG_DATA_START = const(0x35)  # 连续 12 字节: ax,ay,az,gx,gy,gz 各 2 字节小端

_EXPECTED_WHO_AM_I = const(0x05)

_ACCEL_SCALE_8G = 4096.0  # LSB/g，对应 CTRL2=0x23 的 ±8G 量程


class QMI8658:
    def __init__(self, device):
        """
        device: 一个 i2c.I2C.Device 实例，用法跟 AXP2101/PCF85063 一样：
            dev = i2c.I2C.Device(i2c_bus, dev_id=qmi8658.I2C_ADDR, reg_bits=8)
            imu = QMI8658(dev)
        """
        self.dev = device

        who_am_i = self.dev.read_mem(_REG_WHO_AM_I, 1)[0]
        if who_am_i != _EXPECTED_WHO_AM_I:
            raise RuntimeError(
                f"QMI8658 WHO_AM_I 读到 0x{who_am_i:02X}，期望 0x{_EXPECTED_WHO_AM_I:02X}，"
                f"八成是 I2C 地址不对（试试 0x6A）或者接线有问题"
            )

        # 已验证可用的初始化配置（来自同类板子的开源项目）：
        # CTRL1=0x60: 使能内部 2MHz 振荡器 + 地址自增等基础配置
        # CTRL2=0x23: 加速度计 ±8G 量程，1000Hz 输出速率
        # CTRL3=0x43: 陀螺仪量程+速率配置（具体 °/s 量程待核实）
        # CTRL7=0x03: 同时使能加速度计(bit0)和陀螺仪(bit1)
        self.dev.write_mem(_REG_CTRL1, bytes([0x60]))
        self.dev.write_mem(_REG_CTRL2, bytes([0x23]))
        self.dev.write_mem(_REG_CTRL3, bytes([0x43]))
        self.dev.write_mem(_REG_CTRL7, bytes([0x03]))

        # 陀螺仪零点偏移量，标定之前默认是 0（不校准）
        self._gyro_offset = (0, 0, 0)

    def calibrate_gyro(self, num_samples=200, sample_delay_ms=5):
        """
        陀螺仪静态校准：设备保持静止，采样求平均，消除零点漂移。
        陀螺仪静止时理论上应该读到 (0,0,0)，实际由于芯片制造误差会有偏移，
        这个偏移跟朝向无关（不像加速度计那样跟重力方向绑定），所以只做
        陀螺仪校准，不做加速度计校准。

        调用之后，read_gyro_calibrated() 会自动减掉这个偏移量。
        """
        import time
        sum_x = sum_y = sum_z = 0
        for _ in range(num_samples):
            gx, gy, gz = self.read_gyro_raw()
            sum_x += gx
            sum_y += gy
            sum_z += gz
            time.sleep_ms(sample_delay_ms)  # NOQA
        self._gyro_offset = (
            sum_x / num_samples,
            sum_y / num_samples,
            sum_z / num_samples,
        )
        return self._gyro_offset

    def read_gyro_calibrated(self):
        """陀螺仪读数减掉校准偏移量。校准前跟 read_gyro_raw() 一样（偏移量是0）"""
        gx, gy, gz = self.read_gyro_raw()
        ox, oy, oz = self._gyro_offset
        return (gx - ox, gy - oy, gz - oz)

    @staticmethod
    def _to_signed16(low, high):
        val = (high << 8) | low
        if val >= 0x8000:
            val -= 0x10000
        return val

    def read_raw(self):
        """返回 (ax, ay, az, gx, gy, gz) 六个原始 16bit 有符号整数"""
        buf = self.dev.read_mem(_REG_DATA_START, 12)
        ax = self._to_signed16(buf[0], buf[1])
        ay = self._to_signed16(buf[2], buf[3])
        az = self._to_signed16(buf[4], buf[5])
        gx = self._to_signed16(buf[6], buf[7])
        gy = self._to_signed16(buf[8], buf[9])
        gz = self._to_signed16(buf[10], buf[11])
        return ax, ay, az, gx, gy, gz

    def read_accel_g(self):
        """加速度，单位 g（重力加速度），基于 ±8G 量程换算，置信度高"""
        ax, ay, az, _, _, _ = self.read_raw()
        return (ax / _ACCEL_SCALE_8G, ay / _ACCEL_SCALE_8G, az / _ACCEL_SCALE_8G)

    def read_gyro_raw(self):
        """陀螺仪原始值，暂时不做 °/s 换算（量程待核实），需要精确角速度时再补"""
        _, _, _, gx, gy, gz = self.read_raw()
        return (gx, gy, gz)
