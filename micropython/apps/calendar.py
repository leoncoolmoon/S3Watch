"""
Calendar Application for MicroPython LVGL v9
Monthly calendar reference view.
"""

try:
    import lvgl as lv
    LVGL_AVAILABLE = True
except ImportError:
    LVGL_AVAILABLE = False

class CalendarApp:
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
        self.container.set_style_bg_color(lv.color_hex(0x0A0A0A), 0)

        cal = lv.calendar(self.container)
        cal.set_size(360, 380)
        cal.center()
        cal.set_today(2025, 1, 15)
        cal.set_showed_date(2025, 1)

    def clean(self):
        if self.container:
            self.container.delete()
