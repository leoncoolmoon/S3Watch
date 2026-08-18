"""
Steps Counter Application for MicroPython LVGL v9
Displays step counts, goal progress bar, and distance estimates.
"""

try:
    import lvgl as lv
    LVGL_AVAILABLE = True
except ImportError:
    LVGL_AVAILABLE = False

class StepsApp:
    def __init__(self, parent_tile, ui_manager):
        self.parent = parent_tile
        self.ui_manager = ui_manager
        self.container = None
        self.lbl_steps = None
        self.bar_goal = None
        self.create_ui()

    def create_ui(self):
        if not LVGL_AVAILABLE:
            return

        self.container = lv.obj(self.parent)
        self.container.set_size(410, 502)
        self.container.center()
        self.container.set_style_bg_color(lv.color_hex(0x051E15), 0)

        title = lv.label(self.container)
        title.set_text("Step Counter")
        title.align(lv.ALIGN.TOP_MID, 0, 15)

        self.lbl_steps = lv.label(self.container)
        self.lbl_steps.set_text("0 / 8000 Steps")
        self.lbl_steps.center()

        self.bar_goal = lv.bar(self.container)
        self.bar_goal.set_size(300, 20)
        self.bar_goal.align(lv.ALIGN.CENTER, 0, 50)
        self.bar_goal.set_range(0, 8000)
        self.bar_goal.set_value(0, lv.ANIM.OFF)

    def update(self):
        if not LVGL_AVAILABLE or not self.ui_manager.imu:
            return

        steps = self.ui_manager.imu.read_pedometer()
        goal = self.ui_manager.settings.get("step_goal", 8000)

        if self.lbl_steps:
            self.lbl_steps.set_text(f"{steps} / {goal} Steps")
        if self.bar_goal:
            self.bar_goal.set_range(0, goal)
            self.bar_goal.set_value(min(steps, goal), lv.ANIM.ON)

    def clean(self):
        if self.container:
            self.container.delete()
