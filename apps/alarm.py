"""
Alarm Application for MicroPython LVGL v9
One-shot alarm clock setting and trigger management.
"""

try:
    import lvgl as lv
    LVGL_AVAILABLE = True
except ImportError:
    LVGL_AVAILABLE = False

class AlarmApp:
    def __init__(self, parent_tile, ui_manager):
        self.parent = parent_tile
        self.ui_manager = ui_manager
        self.container = None
        self.alarm_hour = 7
        self.alarm_min = 0
        self.alarm_enabled = False
        self.create_ui()

    def create_ui(self):
        if not LVGL_AVAILABLE:
            return

        self.container = lv.obj(self.parent)
        self.container.set_size(410, 502)
        self.container.center()
        self.container.set_style_bg_color(lv.color_hex(0x111122), 0)

        title = lv.label(self.container)
        title.set_text("Alarm Clock")
        title.align(lv.ALIGN.TOP_MID, 0, 15)

        self.lbl_alarm = lv.label(self.container)
        self.lbl_alarm.set_text(f"Alarm: {self.alarm_hour:02d}:{self.alarm_min:02d}")
        self.lbl_alarm.center()

        btn_toggle = lv.button(self.container)
        btn_toggle.set_size(120, 50)
        btn_toggle.align(lv.ALIGN.BOTTOM_MID, 0, -40)
        lbl_toggle = lv.label(btn_toggle)
        lbl_toggle.set_text("Toggle On/Off")
        lbl_toggle.center()

    def clean(self):
        if self.container:
            self.container.delete()
