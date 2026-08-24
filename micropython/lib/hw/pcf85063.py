"""
PCF85063 Real-Time Clock Driver for MicroPython
Target Board: Waveshare ESP32-S3-Touch-AMOLED-2.06
"""

import time
try:
    import machine
    HARDWARE_AVAILABLE = True
except ImportError:
    HARDWARE_AVAILABLE = False

from myboard import I2C_ADDR_RTC

def bcd2dec(bcd):
    return (bcd >> 4) * 10 + (bcd & 0x0F)

def dec2bcd(dec):
    return ((dec // 10) << 4) | (dec % 10)

class PCF85063:
    I2C_ADDR = I2C_ADDR_RTC

    REG_CTRL1 = 0x00
    REG_CTRL2 = 0x01
    REG_OFFSET = 0x02
    REG_RAM_BYTE = 0x03
    REG_TIME = 0x04 # Seconds register address

    def __init__(self, i2c=None):
        self.i2c = i2c

    def is_time_valid(self):
        if not self.i2c:
            return True
        try:
            sec_reg = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_TIME, 1)[0]
            # Bit 7 of seconds register is OS (Oscillator Stop) flag
            return (sec_reg & 0x80) == 0
        except Exception:
            return False

    def datetime(self, val=None):
        if val is None:
            # Read datetime
            if not self.i2c:
                t = time.localtime()
                return (t[0], t[1], t[2], t[6], t[3], t[4], t[5])
            try:
                data = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_TIME, 7)
                sec = bcd2dec(data[0] & 0x7F)
                minute = bcd2dec(data[1] & 0x7F)
                hour = bcd2dec(data[2] & 0x3F)
                day = bcd2dec(data[3] & 0x3F)
                wday = bcd2dec(data[4] & 0x07)
                month = bcd2dec(data[5] & 0x1F)
                year = 2000 + bcd2dec(data[6])
                return (year, month, day, wday, hour, minute, sec)
            except Exception:
                t = time.localtime()
                return (t[0], t[1], t[2], t[6], t[3], t[4], t[5])
        else:
            # Set datetime
            year, month, day, wday, hour, minute, sec = val
            if self.i2c:
                try:
                    buf = bytes([
                        dec2bcd(sec) & 0x7F, # Clear OS flag on write
                        dec2bcd(minute),
                        dec2bcd(hour),
                        dec2bcd(day),
                        dec2bcd(wday),
                        dec2bcd(month),
                        dec2bcd(year % 100)
                    ])
                    self.i2c.writeto_mem(self.I2C_ADDR, self.REG_TIME, buf)
                except Exception as e:
                    print(f"PCF85063 set_time error: {e}")

    def get_time(self):
        dt = self.datetime()
        return {
            "year": dt[0], "month": dt[1], "day": dt[2],
            "weekday": dt[3], "hour": dt[4], "minute": dt[5], "second": dt[6]
        }

    def set_time(self, year, month, day, hour, minute, second, wday=0):
        self.datetime((year, month, day, wday, hour, minute, second))

    def sync_to_machine_rtc(self):
        if HARDWARE_AVAILABLE and hasattr(machine, "RTC"):
            dt = self.datetime()
            try:
                machine.RTC().datetime((dt[0], dt[1], dt[2], dt[3], dt[4], dt[5], dt[6], 0))
                return True
            except Exception:
                return False
        return True
