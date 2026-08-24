"""
CO5300 AMOLED Display Driver for MicroPython (410x502)
Target Board: Waveshare ESP32-S3-Touch-AMOLED-2.06
"""

import time

if not hasattr(time, "sleep_ms"):
    time.sleep_ms = lambda ms: time.sleep(ms / 1000.0)

try:
    import machine
    HARDWARE_AVAILABLE = True
except ImportError:
    HARDWARE_AVAILABLE = False

try:
    import lvgl as lv
    LVGL_AVAILABLE = True
except ImportError:
    LVGL_AVAILABLE = False

from myboard import (
    LCD_SCK, LCD_D0, LCD_D1, LCD_D2, LCD_D3,
    LCD_CS, LCD_RST, LCD_WIDTH, LCD_HEIGHT, LCD_FREQ
)

CMD_SLPOUT  = 0x11
CMD_SLPIN   = 0x10
CMD_DISPON  = 0x29
CMD_DISPOFF = 0x28
CMD_CASET   = 0x2A
CMD_PASET   = 0x2B
CMD_RAMWR   = 0x2C
CMD_COLMOD  = 0x3A
CMD_BRIGHT  = 0x51

class CO5300:
    WIDTH = LCD_WIDTH
    HEIGHT = LCD_HEIGHT

    def __init__(self, spi=None, cs_pin=LCD_CS, dc_pin=None, rst_pin=LCD_RST):
        self.spi = spi
        self.cs = machine.Pin(cs_pin, machine.Pin.OUT) if HARDWARE_AVAILABLE and cs_pin is not None else None
        self.rst = machine.Pin(rst_pin, machine.Pin.OUT) if HARDWARE_AVAILABLE and rst_pin is not None else None
        self.disp = None
        self._backlight_pct = 100

        self.reset()
        self.init()
        self.set_power(True)

    def reset(self):
        if self.rst:
            self.rst.value(1)
            time.sleep_ms(10)
            self.rst.value(0)
            time.sleep_ms(50)
            self.rst.value(1)
            time.sleep_ms(120)

    def write_cmd(self, cmd):
        if not self.spi or not HARDWARE_AVAILABLE:
            return
        if self.cs: self.cs.value(0)
        self.spi.write(bytes([cmd]))
        if self.cs: self.cs.value(1)

    def write_data(self, data):
        if not self.spi or not HARDWARE_AVAILABLE:
            return
        if self.cs: self.cs.value(0)
        if isinstance(data, (bytes, bytearray, memoryview)):
            self.spi.write(data)
        elif isinstance(data, int):
            self.spi.write(bytes([data]))
        else:
            self.spi.write(bytes(data))
        if self.cs: self.cs.value(1)

    def init(self):
        if not HARDWARE_AVAILABLE or not self.spi:
            return
        self.write_cmd(CMD_SLPOUT)
        time.sleep_ms(120)

        # RGB565 Color Format (0x55 = 16-bit)
        self.write_cmd(CMD_COLMOD)
        self.write_data(0x55)

        self.display_on()

    def set_power(self, enable=True):
        if enable:
            self.wake_display()
        else:
            self.sleep_display()

    def sleep_display(self):
        self.write_cmd(CMD_SLPIN)
        time.sleep_ms(50)

    def wake_display(self):
        self.write_cmd(CMD_SLPOUT)
        time.sleep_ms(120)
        self.display_on()

    def display_on(self):
        self.write_cmd(CMD_DISPON)
        time.sleep_ms(50)

    def display_off(self):
        self.write_cmd(CMD_DISPOFF)
        time.sleep_ms(50)

    def set_backlight(self, percent):
        self._backlight_pct = max(0, min(100, percent))
        val = int(self._backlight_pct * 255 / 100)
        self.write_cmd(CMD_BRIGHT)
        self.write_data(val)

    def set_window(self, x1, y1, x2, y2):
        self.write_cmd(CMD_CASET)
        self.write_data(bytes([x1 >> 8, x1 & 0xFF, x2 >> 8, x2 & 0xFF]))
        self.write_cmd(CMD_PASET)
        self.write_data(bytes([y1 >> 8, y1 & 0xFF, y2 >> 8, y2 & 0xFF]))
        self.write_cmd(CMD_RAMWR)

    def flush(self, disp, area, px_map):
        x1, y1, x2, y2 = area.x1, area.y1, area.x2, area.y2
        self.set_window(x1, y1, x2, y2)

        width = x2 - x1 + 1
        height = y2 - y1 + 1
        data_len = width * height * 2

        if px_map:
            if hasattr(px_map, "__dereference__"):
                data = px_map.__dereference__(data_len)
            else:
                data = px_map
            self.write_data(data)

        if LVGL_AVAILABLE and hasattr(disp, "flush_ready"):
            disp.flush_ready()

    def create_lvgl_display(self):
        if not LVGL_AVAILABLE:
            return None

        disp = lv.display_create(self.WIDTH, self.HEIGHT)
        disp.set_flush_cb(self.flush)

        buf_size = self.WIDTH * 50 * 2
        buf1 = bytearray(buf_size)
        disp.set_buffers(buf1, None, buf_size, lv.DISPLAY_RENDER_MODE.PARTIAL)
        self.disp = disp
        return disp

    def register_lvgl_display(self):
        return self.create_lvgl_display()
