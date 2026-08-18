"""
World Clock Application for MicroPython LVGL v9
Displays local time for multiple global cities.
"""

import time

try:
    import lvgl as lv
    LVGL_AVAILABLE = True
except ImportError:
    LVGL_AVAILABLE = False

CITIES = [
    ("London", 0),
    ("New York", -5),
    ("Tokyo", 9),
    ("Sydney", 11),
    ("Paris", 1)
]

class WorldClockApp:
    def __init__(self, parent_tile, ui_manager):
        self.parent = parent_tile
        self.ui_manager = ui_manager
        self.container = None
        self.create_ui()

    def create_ui(self):
        if not LVGL_AVAILABLE:
            return

        self.container = lv.obj(self.parent)
        self.container.set_size(410, 502)
        self.container.center()
        self.container.set_style_bg_color(lv.color_hex(0x111111), 0)

        title = lv.label(self.container)
        title.set_text("World Clock")
        title.align(lv.ALIGN.TOP_MID, 0, 15)

        y_offset = 60
        utc_now = time.time()
        for city, offset in CITIES:
            lbl = lv.label(self.container)
            city_time = time.localtime(utc_now + offset * 3600)
            time_str = f"{city_time[3]:02d}:{city_time[4]:02d}"
            lbl.set_text(f"{city}: {time_str} (UTC{'+' if offset>=0 else ''}{offset})")
            lbl.align(lv.ALIGN.TOP_LEFT, 30, y_offset)
            y_offset += 40

    def clean(self):
        if self.container:
            self.container.delete()
