"""
Calculator Application for MicroPython LVGL v9
Four-function calculator with grid keypad layout.
"""

try:
    import lvgl as lv
    LVGL_AVAILABLE = True
except ImportError:
    LVGL_AVAILABLE = False

class CalculatorApp:
    def __init__(self, parent_tile, ui_manager):
        self.parent = parent_tile
        self.ui_manager = ui_manager
        self.container = None
        self.display_val = "0"
        self.first_operand = None
        self.operator = None
        self.new_input = True
        self.lbl_disp = None
        self.create_ui()

    def create_ui(self):
        if not LVGL_AVAILABLE:
            return

        self.container = lv.obj(self.parent)
        self.container.set_size(410, 502)
        self.container.center()
        self.container.set_style_bg_color(lv.color_hex(0x000000), 0)

        self.lbl_disp = lv.label(self.container)
        self.lbl_disp.set_text(self.display_val)
        self.lbl_disp.align(lv.ALIGN.TOP_RIGHT, -20, 30)

        # Keypad Grid
        btnm_map = [
            "C", "/", "*", "-", "\n",
            "7", "8", "9", "+", "\n",
            "4", "5", "6", "=", "\n",
            "1", "2", "3", "0", ""
        ]
        btnm = lv.buttonmatrix(self.container)
        btnm.set_size(360, 350)
        btnm.align(lv.ALIGN.BOTTOM_MID, 0, -10)
        btnm.set_map(btnm_map)
        btnm.add_event_cb(self._on_btn_click, lv.EVENT.VALUE_CHANGED, None)

    def _on_btn_click(self, e):
        btnm = e.get_target() if hasattr(e, "get_target") else e.get_target_obj()
        btn_id = btnm.get_selected_button()
        btn_txt = btnm.get_button_text(btn_id)
        if not btn_txt:
            return

        if btn_txt in "0123456789":
            if self.new_input or self.display_val == "0":
                self.display_val = btn_txt
                self.new_input = False
            else:
                self.display_val += btn_txt
        elif btn_txt == "C":
            self.display_val = "0"
            self.first_operand = None
            self.operator = None
            self.new_input = True
        elif btn_txt in "+-*/":
            self.first_operand = float(self.display_val)
            self.operator = btn_txt
            self.new_input = True
        elif btn_txt == "=" and self.operator and self.first_operand is not None:
            second = float(self.display_val)
            res = 0
            if self.operator == "+": res = self.first_operand + second
            elif self.operator == "-": res = self.first_operand - second
            elif self.operator == "*": res = self.first_operand * second
            elif self.operator == "/": res = self.first_operand / second if second != 0 else "Error"

            self.display_val = str(res)
            self.first_operand = None
            self.operator = None
            self.new_input = True

        if self.lbl_disp:
            self.lbl_disp.set_text(self.display_val)

    def clean(self):
        if self.container:
            self.container.delete()
