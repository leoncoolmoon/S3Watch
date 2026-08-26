"""
pedometer.py —— 计步器应用
"""
import lvgl as lv
import driver as hw
from services import get_font, clean_obj, add_back_button

APP_INFO = {
    "name": "Pedometer / 计步器",
    "icon": None
}

def run():
    hw.init_essential()
    scr = lv.screen_active()
    clean_obj(scr)
    scr.set_style_bg_color(lv.color_black(), 0)

    add_back_button(scr)

    title = lv.label(scr)
    title.set_text("Step Tracker")
    title.set_style_text_font(get_font("montserrat_20"), 0)
    title.align(lv.ALIGN.TOP_MID, 0, 15)

    lbl_steps = lv.label(scr)
    lbl_steps.set_text("0")
    lbl_steps.set_style_text_font(get_font("montserrat_48"), 0)
    lbl_steps.align(lv.ALIGN.CENTER, 0, -30)

    lbl_unit = lv.label(scr)
    lbl_unit.set_text("STEPS TODAY")
    lbl_unit.set_style_text_font(get_font("montserrat_16"), 0)
    lbl_unit.set_style_text_color(lv.color_hex(0x888888), 0)
    lbl_unit.align(lv.ALIGN.CENTER, 0, 20)

    steps_cnt = 0

    def update_steps(timer):
        nonlocal steps_cnt
        try:
            imu = hw.get_imu()
            ax, ay, az = imu.read_accel_g()
            mag = (ax*ax + ay*ay + az*az)**0.5
            if mag > 1.25:
                steps_cnt += 1
                lbl_steps.set_text(str(steps_cnt))
        except Exception:
            pass

    timer = lv.timer_create(update_steps, 200, None)
    def on_delete(e):
        try:
            timer.delete()
        except Exception:
            pass
    scr.add_event_cb(on_delete, lv.EVENT.DELETE, None)
