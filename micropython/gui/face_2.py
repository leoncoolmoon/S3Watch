"""
Face 2 - Horizontal HH:MM + AM/PM + Date Watchface for LVGL v9 MicroPython API
"""

import time

try:
    import lvgl as lv
    LVGL_AVAILABLE = True
except ImportError:
    LVGL_AVAILABLE = False

class Face2:
    def __init__(self, parent_tile, rtc_driver=None, pmu_driver=None):
        self.parent = parent_tile
        self.rtc = rtc_driver
        self.pmu = pmu_driver
        self.time_label = None
        self.date_label = None
        self.batt_label = None
        self.create_ui()

    def create_ui(self):
        if not LVGL_AVAILABLE:
            return

        # Main container
        self.container = lv.obj(self.parent)
        self.container.set_size(410, 502)
        self.container.center()
        self.container.set_style_bg_color(lv.color_hex(0x0A0A1A), 0)

        # Battery Label top right
        self.batt_label = lv.label(self.container)
        self.batt_label.set_text("100%")
        self.batt_label.align(lv.ALIGN.TOP_RIGHT, -20, 20)
        self.batt_label.set_style_text_color(lv.color_hex(0x00FF88), 0)

        # Time HH:MM Horizontal
        self.time_label = lv.label(self.container)
        self.time_label.set_text("12:34")
        self.time_label.center()
        self.time_label.set_style_text_color(lv.color_hex(0xFFFFFF), 0)

        # Date Label below time
        self.date_label = lv.label(self.container)
        self.date_label.set_text("Mon 01 Jan")
        self.date_label.align(lv.ALIGN.CENTER, 0, 60)
        self.date_label.set_style_text_color(lv.color_hex(0x888888), 0)

        self.update()

    def update(self):
        if not LVGL_AVAILABLE:
            return

        if self.rtc:
            t = self.rtc.get_time()
            hh = t['hour']
            mm = t['minute']
            yy = t['year']
            mo = t['month']
            dd = t['day']
        else:
            local = time.localtime()
            yy, mo, dd, hh, mm = local[0], local[1], local[2], local[3], local[4]

        time_str = f"{hh:02d}:{mm:02d}"
        date_str = f"{yy:04d}-{mo:02d}-{dd:02d}"

        if self.time_label:
            self.time_label.set_text(time_str)
        if self.date_label:
            self.date_label.set_text(date_str)

        if self.pmu and self.batt_label:
            info = self.pmu.get_battery_info()
            self.batt_label.set_text(f"{info['percentage']}%")
