"""
stopwatch.py —— 秒表应用
"""
import time
import lvgl as lv
import driver as hw
from services import get_font, clean_obj, add_back_button

APP_INFO = {
    "name": "Stopwatch / 秒表",
    "icon": None
}

class StopwatchApp:
    def __init__(self):
        self.state = "RESET" # RESET, RUNNING, PAUSED
        self.start_time = 0
        self.accum_time = 0
        self.laps = []

    def get_elapsed_ms(self):
        if self.state == "RUNNING":
            return self.accum_time + int((time.ticks_ms() - self.start_time))
        return self.accum_time

def run():
    hw.init_essential()
    scr = lv.screen_active()
    clean_obj(scr)
    scr.set_style_bg_color(lv.color_black(), 0)

    add_back_button(scr)

    sw = StopwatchApp()

    title = lv.label(scr)
    title.set_text("Stopwatch")
    title.set_style_text_font(get_font("montserrat_20"), 0)
    title.align(lv.ALIGN.TOP_MID, 0, 15)

    lbl_main = lv.label(scr)
    lbl_main.set_text("00:00")
    lbl_main.set_style_text_font(get_font("montserrat_48"), 0)
    lbl_main.align(lv.ALIGN.CENTER, -30, -50)

    lbl_frac = lv.label(scr)
    lbl_frac.set_text(".00")
    lbl_frac.set_style_text_font(get_font("montserrat_20"), 0)
    lbl_frac.align(lv.ALIGN.CENTER, 60, -40)

    lap_list = lv.obj(scr)
    lap_list.set_size(360, 220)
    lap_list.align(lv.ALIGN.CENTER, 0, 50)
    lap_list.set_flex_flow(lv.FLEX_FLOW.COLUMN)

    btn_left = lv.button(scr)
    btn_left.set_size(150, 50)
    btn_left.align(lv.ALIGN.BOTTOM_LEFT, 30, -30)
    btn_left.set_style_bg_color(lv.color_hex(0x27AE60), 0)
    lbl_left = lv.label(btn_left)
    lbl_left.set_text("Start")
    lbl_left.set_style_text_font(get_font("montserrat_20"), 0)
    lbl_left.center()

    btn_right = lv.button(scr)
    btn_right.set_size(150, 50)
    btn_right.align(lv.ALIGN.BOTTOM_RIGHT, -30, -30)
    btn_right.set_style_bg_color(lv.color_hex(0x7F8C8D), 0)
    lbl_right = lv.label(btn_right)
    lbl_right.set_text("Lap")
    lbl_right.set_style_text_font(get_font("montserrat_20"), 0)
    lbl_right.center()

    def update_ui(timer):
        ms = sw.get_elapsed_ms()
        cs = (ms // 10) % 100
        sec = (ms // 1000) % 60
        mn = (ms // 60000) % 100
        lbl_main.set_text(f"{mn:02d}:{sec:02d}")
        lbl_frac.set_text(f".{cs:02d}")

    def btn_left_cb(e):
        if sw.state == "RESET" or sw.state == "PAUSED":
            sw.start_time = time.ticks_ms()
            sw.state = "RUNNING"
            lbl_left.set_text("Stop")
            btn_left.set_style_bg_color(lv.color_hex(0xC0392B), 0)
        else:
            sw.accum_time = sw.get_elapsed_ms()
            sw.state = "PAUSED"
            lbl_left.set_text("Resume")
            btn_left.set_style_bg_color(lv.color_hex(0x27AE60), 0)

    def btn_right_cb(e):
        if sw.state == "RUNNING":
            ms = sw.get_elapsed_ms()
            sw.laps.append(ms)
            lbl = lv.label(lap_list)
            lbl.set_style_text_font(get_font("montserrat_16"), 0)
            cs = (ms // 10) % 100
            sec = (ms // 1000) % 60
            mn = (ms // 60000) % 100
            lbl.set_text(f"Lap {len(sw.laps)}: {mn:02d}:{sec:02d}.{cs:02d}")
        elif sw.state == "PAUSED":
            sw.state = "RESET"
            sw.accum_time = 0
            sw.laps.clear()
            clean_obj(lap_list)
            lbl_left.set_text("Start")
            btn_left.set_style_bg_color(lv.color_hex(0x27AE60), 0)
            update_ui(None)

    btn_left.add_event_cb(btn_left_cb, lv.EVENT.CLICKED, None)
    btn_right.add_event_cb(btn_right_cb, lv.EVENT.CLICKED, None)

    timer = lv.timer_create(update_ui, 50, None)
    def on_delete(e):
        try:
            timer.delete()
        except Exception:
            pass
    scr.add_event_cb(on_delete, lv.EVENT.DELETE, None)
