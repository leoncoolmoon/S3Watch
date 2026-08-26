"""
calculator.py —— 计算器应用
"""
import machine
import lvgl as lv
import driver as hw
from main import get_font

APP_INFO = {
    "name": "Calculator / 计算器",
    "icon": None
}

def clean_obj(obj):
    if hasattr(obj, "clean"):
        obj.clean()

def add_back_button(parent, target_app=""):
    btn = lv.button(parent)
    btn.set_size(70, 36)
    btn.align(lv.ALIGN.TOP_LEFT, 10, 10)
    btn.set_style_bg_color(lv.color_hex(0x34495E), 0)
    lbl = lv.label(btn)
    lbl.set_text("< Back")
    lbl.center()
    def on_back(e):
        try:
            from main import set_next_app
            set_next_app(target_app)
        except Exception:
            try:
                rtc = machine.RTC()
                rtc.memory(target_app.encode("utf-8") if target_app else b"")
                machine.reset()
            except Exception as ex:
                print(f"Back reset error: {ex}")
    btn.add_event_cb(on_back, lv.EVENT.CLICKED, None)
    return btn

def run():
    hw.init_essential()
    scr = lv.screen_active()
    clean_obj(scr)
    scr.set_style_bg_color(lv.color_black(), 0)

    add_back_button(scr)

    lbl_display = lv.label(scr)
    lbl_display.set_text("0")
    lbl_display.set_style_text_font(get_font("montserrat_36"), 0)
    lbl_display.align(lv.ALIGN.TOP_RIGHT, -30, 40)

    current_val = "0"
    stored_val = 0
    pending_op = None
    reset_next = False

    def on_key(val):
        nonlocal current_val, stored_val, pending_op, reset_next
        if val in "0123456789.":
            if current_val == "0" or reset_next:
                current_val = val
                reset_next = False
            else:
                if val == "." and "." in current_val:
                    return
                current_val += val
        elif val == "C":
            current_val = "0"
            stored_val = 0
            pending_op = None
        elif val in "+-x/":
            stored_val = float(current_val)
            pending_op = val
            reset_next = True
        elif val == "=":
            if pending_op:
                cur = float(current_val)
                res = 0
                if pending_op == "+": res = stored_val + cur
                elif pending_op == "-": res = stored_val - cur
                elif pending_op == "x": res = stored_val * cur
                elif pending_op == "/": res = stored_val / cur if cur != 0 else "Error"

                if isinstance(res, float) and res.is_integer():
                    res = int(res)
                current_val = str(res)
                pending_op = None
                reset_next = True

        lbl_display.set_text(current_val[:10])

    btn_matrix_map = [
        "C", "/", "x", "-", "\n",
        "7", "8", "9", "+", "\n",
        "4", "5", "6", "=", "\n",
        "1", "2", "3", "0", ""
    ]

    btnm = lv.buttonmatrix(scr)
    btnm.set_size(360, 350)
    btnm.align(lv.ALIGN.BOTTOM_MID, 0, -20)
    btnm.set_map(btn_matrix_map)

    def btnm_cb(e):
        try:
            btn_id = btnm.get_selected_button()
            if btn_id != getattr(lv, "BUTTONMATRIX_BUTTON_NONE", 0xFFFF):
                txt = btnm.get_button_text(btn_id)
                if txt:
                    on_key(txt)
        except Exception as ex:
            print(f"Calculator button error: {ex}")

    btnm.add_event_cb(btnm_cb, lv.EVENT.VALUE_CHANGED, None)
