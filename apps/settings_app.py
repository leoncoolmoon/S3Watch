"""
Settings Application for MicroPython LVGL v9
Provides menu options for brightness, display timeout, sound, Wi-Fi, and SignalK settings.
"""

try:
    import lvgl as lv
    LVGL_AVAILABLE = True
except ImportError:
    LVGL_AVAILABLE = False

class SettingsApp:
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
        self.container.set_style_bg_color(lv.color_hex(0x1A1A1A), 0)

        title = lv.label(self.container)
        title.set_text("Settings")
        title.align(lv.ALIGN.TOP_MID, 0, 15)

        # Settings List Container
        list_obj = lv.list(self.container)
        list_obj.set_size(370, 420)
        list_obj.align(lv.ALIGN.BOTTOM_MID, 0, -10)

        list_obj.add_button(None, "Brightness: 40%")
        list_obj.add_button(None, "Display Timeout: 30s")
        list_obj.add_button(None, "Watchface Style")
        list_obj.add_button(None, "Wi-Fi Settings")
        list_obj.add_button(None, "SignalK Server")
        list_obj.add_button(None, "Time & Date")
        list_obj.add_button(None, "Reset Defaults")

    def clean(self):
        if self.container:
            self.container.delete()
