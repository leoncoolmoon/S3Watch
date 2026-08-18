"""
Stopwatch Application for MicroPython LVGL v9
Start/Stop/Reset and Lap timer.
"""

import time

try:
    import lvgl as lv
    LVGL_AVAILABLE = True
except ImportError:
    LVGL_AVAILABLE = False

class StopwatchApp:
    def __init__(self, parent_tile, ui_manager):
        self.parent = parent_tile
        self.ui_manager = ui_manager
        self.container = None
        self.start_time = 0
        self.elapsed = 0
        self.running = False
        self.lbl_time = None
        self.btn_start_label = None
        self.create_ui()

    def create_ui(self):
        if not LVGL_AVAILABLE:
            return

        self.container = lv.obj(self.parent)
        self.container.set_size(410, 502)
        self.container.center()
        self.container.set_style_bg_color(lv.color_hex(0x000000), 0)

        title = lv.label(self.container)
        title.set_text("Stopwatch")
        title.align(lv.ALIGN.TOP_MID, 0, 15)

        self.lbl_time = lv.label(self.container)
        self.lbl_time.set_text("00:00.00")
        self.lbl_time.center()

        btn_start = lv.button(self.container)
        btn_start.set_size(100, 50)
        btn_start.align(lv.ALIGN.BOTTOM_LEFT, 40, -40)
        self.btn_start_label = lv.label(btn_start)
        self.btn_start_label.set_text("Start")
        self.btn_start_label.center()
        btn_start.add_event_cb(self._on_toggle_start, lv.EVENT.CLICKED, None)

        btn_reset = lv.button(self.container)
        btn_reset.set_size(100, 50)
        btn_reset.align(lv.ALIGN.BOTTOM_RIGHT, -40, -40)
        lbl_reset = lv.label(btn_reset)
        lbl_reset.set_text("Reset")
        lbl_reset.center()
        btn_reset.add_event_cb(self._on_reset, lv.EVENT.CLICKED, None)

    def _on_toggle_start(self, e):
        if self.running:
            self.running = False
            self.elapsed += time.time() - self.start_time
            if self.btn_start_label:
                self.btn_start_label.set_text("Start")
        else:
            self.running = True
            self.start_time = time.time()
            if self.btn_start_label:
                self.btn_start_label.set_text("Stop")

    def _on_reset(self, e):
        self.running = False
        self.start_time = 0
        self.elapsed = 0
        if self.lbl_time:
            self.lbl_time.set_text("00:00.00")
        if self.btn_start_label:
            self.btn_start_label.set_text("Start")

    def update(self):
        if self.running and self.lbl_time:
            now = time.time()
            total = self.elapsed + (now - self.start_time)
            mins = int(total // 60)
            secs = int(total % 60)
            csecs = int((total * 100) % 100)
            self.lbl_time.set_text(f"{mins:02d}:{secs:02d}.{csecs:02d}")

    def clean(self):
        if self.container:
            self.container.delete()
