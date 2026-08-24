"""
PCF85063A Real-Time Clock Driver wrapper for backwards compatibility
"""

from lib.hw.pcf85063 import PCF85063 as _PCF85063

class PCF85063A(_PCF85063):
    def get_time(self):
        dt = self.datetime()
        return {
            "year": dt[0], "month": dt[1], "day": dt[2],
            "weekday": dt[3], "hour": dt[4], "minute": dt[5], "second": dt[6]
        }

    def set_time(self, year, month, day, hour, minute, second, wday=0):
        self.datetime((year, month, day, wday, hour, minute, second))
