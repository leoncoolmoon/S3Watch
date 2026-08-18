"""
Face 1 - Stacked HH/MM + Centered SS Watchface for LVGL v9 MicroPython API
"""

import time

try:
    import lvgl as lv
    LVGL_AVAILABLE = True
except ImportError:
    LVGL_AVAILABLE = False

class Face1:
    def __init__(self, parent_tile, rtc_driver=None, pmu_driver=None):
        self.parent = parent_tile
        self.rtc = rtc_driver
        self.pmu = pmu_driver
        self.time_label = None
        self.sec_label = None
        self.batt_label = None
        self.create_ui()

    def create_ui(self):
        if not LVGL_AVAILABLE:
            return

        # Main container
        self.container = lv.obj(self.parent)
        self.container.set_size(410, 502)
        self.container.center()
        self.container.set_style_bg_color(lv.color_hex(0x000000), 0)

        # Battery Label at top right
        self.batt_label = lv.label(self.container)
        self.batt_label.set_text("100%")
        self.batt_label.align(lv.ALIGN.TOP_RIGHT, -20, 20)
        self.batt_label.set_style_text_color(lv.color_hex(0x00FF00), 0)

        # Time HH:MM Label Stacked
        self.time_label = lv.label(self.container)
        self.time_label.set_text("12\n34")
        self.time_label.center()
        self.time_label.set_style_text_color(lv.color_hex(0xFFFFFF), 0)
        self.time_label.set_style_text_align(lv.TEXT_ALIGN.CENTER, 0)

        # Seconds SS Label
        self.sec_label = lv.label(self.container)
        self.sec_label.set_text("56")
        self.sec_label.align(lv.ALIGN.BOTTOM_MID, 0, -40)
        self.sec_label.set_style_text_color(lv.color_hex(0xFF9500), 0)

        self.update()

    def update(self):
        if not LVGL_AVAILABLE:
            return

        if self.rtc:
            t = self.rtc.get_time()
            hh = f"{t['hour']:02d}"
            mm = f"{t['minute']:02d}"
            ss = f"{t['second']:02d}"
        else:
            local = time.localtime()
            hh = f"{local[3]:02d}"
            mm = f"{local[4]:02d}"
            ss = f"{local[5]:02d}"

        if self.time_label:
            self.time_label.set_text(f"{hh}\n{mm}")
        if self.sec_label:
            self.sec_label.set_text(ss)

        if self.pmu and self.batt_label:
            info = self.pmu.get_battery_info()
            self.batt_label.set_text(f"{info['percentage']}%")
