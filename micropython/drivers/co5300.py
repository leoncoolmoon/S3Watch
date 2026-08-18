"""
CO5300 AMOLED Display Driver for MicroPython (410x502)
Target Board: Waveshare ESP32-S3-Touch-AMOLED-2.06
Handles hardware init over SPI and LVGL v9 display flush callback.
"""

import time

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

# CO5300 Commands
CMD_SLPOUT = 0x11
CMD_DISPON = 0x29
CMD_CASET  = 0x2A
CMD_PASET  = 0x2B
CMD_RAMWR  = 0x2C
CMD_MADCTL = 0x36
CMD_COLMOD = 0x3A

class CO5300:
    WIDTH = 410
    HEIGHT = 502

    def __init__(self, spi=None, cs_pin=12, dc_pin=14, rst_pin=9):
        self.spi = spi
        self.cs = machine.Pin(cs_pin, machine.Pin.OUT) if HARDWARE_AVAILABLE and cs_pin is not None else None
        self.dc = machine.Pin(dc_pin, machine.Pin.OUT) if HARDWARE_AVAILABLE and dc_pin is not None else None
        self.rst = machine.Pin(rst_pin, machine.Pin.OUT) if HARDWARE_AVAILABLE and rst_pin is not None else None
        self.disp_drv = None

        self.reset()
        self.init_display()

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
        if self.dc: self.dc.value(0)
        if self.cs: self.cs.value(0)
        self.spi.write(bytes([cmd]))
        if self.cs: self.cs.value(1)

    def write_data(self, data):
        if not self.spi or not HARDWARE_AVAILABLE:
            return
        if self.dc: self.dc.value(1)
        if self.cs: self.cs.value(0)
        if isinstance(data, (bytes, bytearray, memoryview)):
            self.spi.write(data)
        elif isinstance(data, int):
            self.spi.write(bytes([data]))
        else:
            self.spi.write(bytes(data))
        if self.cs: self.cs.value(1)

    def init_display(self):
        if not HARDWARE_AVAILABLE or not self.spi:
            return
        self.write_cmd(CMD_SLPOUT)
        time.sleep_ms(120)

        # RGB565 Color Format (0x55 = 16-bit)
        self.write_cmd(CMD_COLMOD)
        self.write_data(0x55)

        self.write_cmd(CMD_DISPON)
        time.sleep_ms(50)

    def set_window(self, x1, y1, x2, y2):
        # Column address set
        self.write_cmd(CMD_CASET)
        self.write_data(bytes([x1 >> 8, x1 & 0xFF, x2 >> 8, x2 & 0xFF]))

        # Row address set
        self.write_cmd(CMD_PASET)
        self.write_data(bytes([y1 >> 8, y1 & 0xFF, y2 >> 8, y2 & 0xFF]))

        # Memory write
        self.write_cmd(CMD_RAMWR)

    def flush(self, disp, area, px_map):
        x1 = area.x1
        y1 = area.y1
        x2 = area.x2
        y2 = area.y2

        self.set_window(x1, y1, x2, y2)

        # Calculate buffer size
        width = x2 - x1 + 1
        height = y2 - y1 + 1
        data_len = width * height * 2

        # Convert C pointer / memoryview px_map
        if px_map:
            if hasattr(px_map, "__dereference__"):
                data = px_map.__dereference__(data_len)
            else:
                data = px_map
            self.write_data(data)

        # Inform LVGL that flushing is ready
        if LVGL_AVAILABLE and hasattr(disp, "flush_ready"):
            disp.flush_ready()

    def register_lvgl_display(self):
        if not LVGL_AVAILABLE:
            return None

        disp = lv.display_create(self.WIDTH, self.HEIGHT)
        disp.set_flush_cb(self.flush)

        # Allocate buffer for ~1/10th screen in PSRAM/RAM
        buf_size = self.WIDTH * 50 * 2
        buf1 = bytearray(buf_size)
        disp.set_buffers(buf1, None, buf_size, lv.DISPLAY_RENDER_MODE.PARTIAL)
        self.disp_drv = disp
        return disp
