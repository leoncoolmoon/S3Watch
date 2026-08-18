"""
PCF85063A Real-Time Clock Driver for MicroPython
Handles hardware RTC read/write operations and BCD format conversion.
"""

import time

def bcd2dec(bcd):
    return (bcd >> 4) * 10 + (bcd & 0x0F)

def dec2bcd(dec):
    return ((dec // 10) << 4) | (dec % 10)

class PCF85063A:
    I2C_ADDR = 0x51
    REG_TIME = 0x04 # Seconds register address

    def __init__(self, i2c=None):
        self.i2c = i2c

    def get_time(self):
        if not self.i2c:
            t = time.localtime()
            return {
                "year": t[0], "month": t[1], "day": t[2],
                "hour": t[3], "minute": t[4], "second": t[5],
                "weekday": t[6]
            }
        try:
            data = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_TIME, 7)
            sec = bcd2dec(data[0] & 0x7F)
            minute = bcd2dec(data[1] & 0x7F)
            hour = bcd2dec(data[2] & 0x3F)
            day = bcd2dec(data[3] & 0x3F)
            wday = bcd2dec(data[4] & 0x07)
            month = bcd2dec(data[5] & 0x1F)
            year = 2000 + bcd2dec(data[6])
            return {
                "year": year, "month": month, "day": day,
                "hour": hour, "minute": minute, "second": sec,
                "weekday": wday
            }
        except Exception as e:
            print(f"PCF85063A get_time error: {e}")
            t = time.localtime()
            return {
                "year": t[0], "month": t[1], "day": t[2],
                "hour": t[3], "minute": t[4], "second": t[5],
                "weekday": t[6]
            }

    def set_time(self, year, month, day, hour, minute, second, wday=0):
        if not self.i2c:
            return
        try:
            buf = bytes([
                dec2bcd(second),
                dec2bcd(minute),
                dec2bcd(hour),
                dec2bcd(day),
                dec2bcd(wday),
                dec2bcd(month),
                dec2bcd(year % 100)
            ])
            self.i2c.writeto_mem(self.I2C_ADDR, self.REG_TIME, buf)
        except Exception as e:
            print(f"PCF85063A set_time error: {e}")
