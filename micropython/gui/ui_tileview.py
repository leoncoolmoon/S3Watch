"""
UI Tileview Navigation Manager for LVGL v9 MicroPython API
Handles 2D Tileview navigation grid (Watchface, SignalK Alerts, Controls, App Picker, Dynamic App Tile).
"""

try:
    import lvgl as lv
    LVGL_AVAILABLE = True
except ImportError:
    LVGL_AVAILABLE = False

from gui.face_1 import Face1
from gui.face_2 import Face2
from apps.app_picker import AppPicker

class UITileview:
    def __init__(self, settings, pmu, rtc, imu, signalk):
        self.settings = settings
        self.pmu = pmu
        self.rtc = rtc
        self.imu = imu
        self.signalk = signalk

        self.tileview = None
        self.tile_alerts = None
        self.tile_watchface = None
        self.tile_controls = None
        self.tile_picker = None
        self.tile_app = None

        self.active_face = None
        self.picker_app = None
        self.current_app_obj = None

        self.init_ui()

    def init_ui(self):
        if not LVGL_AVAILABLE:
            return

        # Screen setup
        scr = lv.screen_active()
        scr.set_style_bg_color(lv.color_hex(0x000000), 0)

        # 2D Tileview creation
        self.tileview = lv.tileview(scr)
        self.tileview.set_size(410, 502)

        # Grid Tiles setup:
        # Tile (0, 0): SignalK Alerts (Swipe Down from Watchface)
        # Tile (0, 1): Watchface ("Home")
        # Tile (1, 1): Quick Controls (Swipe Right from Watchface)
        # Tile (0, 2): App Picker Grid (Swipe Up from Watchface)
        # Tile (1, 2): Dynamic App Tile

        self.tile_alerts = self.tileview.add_tile(0, 0, lv.DIR.BOTTOM)
        self.tile_watchface = self.tileview.add_tile(0, 1, lv.DIR.TOP | lv.DIR.BOTTOM | lv.DIR.RIGHT)
        self.tile_controls = self.tileview.add_tile(1, 1, lv.DIR.LEFT)
        self.tile_picker = self.tileview.add_tile(0, 2, lv.DIR.TOP | lv.DIR.RIGHT)
        self.tile_app = self.tileview.add_tile(1, 2, lv.DIR.LEFT)

        # Set default active tile to Watchface (0, 1)
        self.tileview.set_tile(self.tile_watchface, lv.ANIM.OFF)

        # Setup Watchface
        style_idx = self.settings.get("watchface_style", 0)
        if style_idx == 0:
            self.active_face = Face1(self.tile_watchface, self.rtc, self.pmu)
        else:
            self.active_face = Face2(self.tile_watchface, self.rtc, self.pmu)

        # Setup App Picker inside Tile (0, 2)
        self.picker_app = AppPicker(self.tile_picker, self)

        # Setup Quick Controls Tile UI
        self._build_controls_tile()

        # Setup SignalK Alerts Tile UI
        self._build_alerts_tile()

    def _build_controls_tile(self):
        lbl = lv.label(self.tile_controls)
        lbl.set_text("Quick Controls\n\nBrightness: 40%\nWi-Fi: Off")
        lbl.center()

    def _build_alerts_tile(self):
        lbl = lv.label(self.tile_alerts)
        lbl.set_text("SignalK Active Alerts\n\nNo Active Alerts")
        lbl.center()

    def open_app(self, app_class):
        """ Dynamically mount an application into Tile (1, 2) """
        if not LVGL_AVAILABLE:
            return

        # Clear existing app in dynamic tile if any
        if self.current_app_obj and hasattr(self.current_app_obj, 'clean'):
            self.current_app_obj.clean()

        # Instantiate new app view inside tile_app
        self.current_app_obj = app_class(self.tile_app, self)
        # Navigate tileview to dynamic app tile (1, 2)
        self.tileview.set_tile(self.tile_app, lv.ANIM.ON)

    def update(self):
        if not LVGL_AVAILABLE:
            return

        if self.active_face:
            self.active_face.update()
        if self.current_app_obj and hasattr(self.current_app_obj, 'update'):
            self.current_app_obj.update()
