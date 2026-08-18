"""
App Picker Launcher Grid for LVGL v9 MicroPython API
Provides a 2-column icon grid to launch smartwatch apps into dynamic tile (1,2).
"""

try:
    import lvgl as lv
    LVGL_AVAILABLE = True
except ImportError:
    LVGL_AVAILABLE = False

from apps.signalk_app import SignalKApp
from apps.world_clock import WorldClockApp
from apps.stopwatch import StopwatchApp
from apps.alarm import AlarmApp
from apps.calendar import CalendarApp
from apps.music import MusicApp
from apps.calculator import CalculatorApp
from apps.steps import StepsApp
from apps.settings_app import SettingsApp

APP_MAP = {
    "SignalK": SignalKApp,
    "World Clock": WorldClockApp,
    "Stopwatch": StopwatchApp,
    "Alarm": AlarmApp,
    "Calendar": CalendarApp,
    "Music": MusicApp,
    "Calculator": CalculatorApp,
    "Steps": StepsApp,
    "Settings": SettingsApp
}

class AppPicker:
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
        self.container.set_style_bg_color(lv.color_hex(0x111122), 0)

        title = lv.label(self.container)
        title.set_text("Applications")
        title.align(lv.ALIGN.TOP_MID, 0, 15)

        # 2-column flex container
        grid = lv.obj(self.container)
        grid.set_size(380, 420)
        grid.align(lv.ALIGN.BOTTOM_MID, 0, -10)
        grid.set_flex_flow(lv.FLEX_FLOW.ROW_WRAP)
        grid.set_flex_align(lv.FLEX_ALIGN.SPACE_EVENLY, lv.FLEX_ALIGN.CENTER, lv.FLEX_ALIGN.CENTER)

        for name, app_cls in APP_MAP.items():
            btn = lv.button(grid)
            btn.set_size(170, 70)
            lbl = lv.label(btn)
            lbl.set_text(name)
            lbl.center()

            # Register click callback to open application
            btn.add_event_cb(lambda e, cls=app_cls: self._on_app_click(cls), lv.EVENT.CLICKED, None)

    def _on_app_click(self, app_cls):
        if self.ui_manager:
            self.ui_manager.open_app(app_cls)
