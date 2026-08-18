"""
SignalK Marine Dashboard Application for MicroPython LVGL v9
Displays 2x2 grid of SOG, HDG, Depth, and Wind.
"""

try:
    import lvgl as lv
    LVGL_AVAILABLE = True
except ImportError:
    LVGL_AVAILABLE = False

class SignalKApp:
    def __init__(self, parent_tile, ui_manager):
        self.parent = parent_tile
        self.ui_manager = ui_manager
        self.container = None
        self.lbl_hdg = None
        self.lbl_depth = None
        self.lbl_sog = None
        self.lbl_wind = None
        self.create_ui()

    def create_ui(self):
        if not LVGL_AVAILABLE:
            return

        self.container = lv.obj(self.parent)
        self.container.set_size(410, 502)
        self.container.center()
        self.container.set_style_bg_color(lv.color_hex(0x051525), 0)

        title = lv.label(self.container)
        title.set_text("SignalK Marine")
        title.align(lv.ALIGN.TOP_MID, 0, 15)

        # 2x2 Grid labels
        self.lbl_hdg = lv.label(self.container)
        self.lbl_hdg.set_text("HDG: ---°")
        self.lbl_hdg.align(lv.ALIGN.TOP_LEFT, 20, 80)

        self.lbl_sog = lv.label(self.container)
        self.lbl_sog.set_text("SOG: --- kt")
        self.lbl_sog.align(lv.ALIGN.TOP_RIGHT, -20, 80)

        self.lbl_depth = lv.label(self.container)
        self.lbl_depth.set_text("DEPTH: --- ft")
        self.lbl_depth.align(lv.ALIGN.BOTTOM_LEFT, 20, -120)

        self.lbl_wind = lv.label(self.container)
        self.lbl_wind.set_text("WIND: ---")
        self.lbl_wind.align(lv.ALIGN.BOTTOM_RIGHT, -20, -120)

        self.update()

    def update(self):
        if not LVGL_AVAILABLE or not self.ui_manager.signalk:
            return

        metrics = self.ui_manager.signalk.get_metrics()
        if self.lbl_hdg:
            self.lbl_hdg.set_text(f"HDG: {metrics['heading']:.0f}°")
        if self.lbl_sog:
            self.lbl_sog.set_text(f"SOG: {metrics['sog']:.1f} kt")
        if self.lbl_depth:
            self.lbl_depth.set_text(f"DEPTH: {metrics['depth']:.1f} ft")
        if self.lbl_wind:
            self.lbl_wind.set_text(f"WIND: {metrics['wind_angle']}° / {metrics['wind_speed']:.1f}kt")

    def clean(self):
        if self.container:
            self.container.delete()
