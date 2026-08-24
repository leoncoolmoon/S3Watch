"""
PCF85063 RTC 驱动 —— 改成走项目自带的 i2c.I2C.Device，
跟 AXP2101/FT3168/QMI8658 共用同一个 i2c.I2C.Bus 对象。

寄存器定义是 PCF85063 数据手册里的标准布局，这部分置信度依然很高，
只是把底层 I2C 调用从 machine.I2C 换成了 i2c.I2C.Device 的 write_mem/read_mem。
"""

_ADDR = const(0x51)  # 传给 i2c.I2C.Device(dev_id=...) 用

_REG_CTRL1 = const(0x00)
_REG_SECONDS = const(0x04)   # 秒开始，之后依次是 分/时/日/星期/月/年，共7字节
_REG_SECONDS_OS_MASK = const(0x80)  # bit7=1 表示时钟曾经掉电，时间不可信


def _bcd2dec(b):
    return (b >> 4) * 10 + (b & 0x0F)


def _dec2bcd(d):
    return ((d // 10) << 4) | (d % 10)


class PCF85063:
    def __init__(self, device):
        """
        device: 一个 i2c.I2C.Device 实例，构造方式：
            import i2c
            bus = i2c.I2C.Bus(host=0, scl=14, sda=15, freq=400000, use_locks=False)
            dev = i2c.I2C.Device(bus, dev_id=pcf85063.I2C_ADDR, reg_bits=8)
            rtc = PCF85063(dev)
        """
        self.dev = device
        self.dev.write_mem(_REG_CTRL1, bytes([0x00]))  # 退出 stop 模式，24小时制

    def is_time_valid(self):
        """开机后建议先调用这个，掉电丢失过时间的话 datetime() 拿到的是垃圾数据"""
        sec_byte = self.dev.read_mem(_REG_SECONDS, 1)[0]
        return (sec_byte & _REG_SECONDS_OS_MASK) == 0


        
    def datetime(self, dt=None):
        """
        不传参数：读取当前时间，返回 (year, month, day, weekday, hour, minute, second)
        传 (year, month, day, weekday, hour, minute, second)：写入设置时间
        """
        if dt is None:
            buf = self.dev.read_mem(_REG_SECONDS, 7)
            second = _bcd2dec(buf[0] & 0x7F)
            minute = _bcd2dec(buf[1] & 0x7F)
            hour = _bcd2dec(buf[2] & 0x3F)
            day = _bcd2dec(buf[3] & 0x3F)
            weekday = buf[4] & 0x07
            month = _bcd2dec(buf[5] & 0x1F)
            year = 2000 + _bcd2dec(buf[6])
            return (year, month, day, weekday, hour, minute, second)
        else:
            year, month, day, weekday, hour, minute, second = dt
            buf = bytes([
                _dec2bcd(second) & 0x7F,   # 写的时候顺便清掉 OS 位，标记时间已重新设置
                _dec2bcd(minute),
                _dec2bcd(hour),
                _dec2bcd(day),
                weekday & 0x07,
                _dec2bcd(month),
                _dec2bcd(year - 2000),
            ])
            self.dev.write_mem(_REG_SECONDS, buf)

    def get_time(self, dt=None):
        year, month, day, weekday, hour, minute, second = self.datetime(dt)
        return {
            'year': year,
            'month': month,
            'day': day,
            'weekday': weekday,
            'hour': hour,
            'minute': minute,
            'second': second
        }
    
    def sync_to_machine_rtc(self):
        """把 PCF85063 的时间同步给 ESP32 内部 RTC，同步后 time.time() 等标准函数才准"""
        import machine
        y, mo, d, wd, h, mi, s = self.datetime()
        machine.RTC().datetime((y, mo, d, wd, h, mi, s, 0))


I2C_ADDR = _ADDR
