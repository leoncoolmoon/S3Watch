"""
alarm.py —— 闹钟应用
"""
import lvgl as lv
import driver as hw
from services import get_font, clean_obj, add_back_button

APP_INFO = {
    "name": "Alarm / 闹钟",
    "icon": None
}

def run():
    hw.init_essential()
    scr = lv.screen_active()
    clean_obj(scr)
    scr.set_style_bg_color(lv.color_black(), 0)

    add_back_button(scr)

    title = lv.label(scr)
    title.set_text("Alarm Clock")
    title.set_style_text_font(get_font("montserrat_20"), 0)
    title.align(lv.ALIGN.TOP_MID, 0, 15)

    list_container = lv.obj(scr)
    list_container.set_size(360, 380)
    list_container.align(lv.ALIGN.TOP_MID, 0, 60)
    list_container.set_flex_flow(lv.FLEX_FLOW.COLUMN)

    alarms = [("07:00 AM", True), ("08:30 AM", False), ("12:00 PM", False)]

    for time_str, enabled in alarms:
        row = lv.obj(list_container)
        row.set_size(340, 60)
        row.set_style_bg_color(lv.color_hex(0x1E272C), 0)

        lbl = lv.label(row)
        lbl.set_text(time_str)
        lbl.set_style_text_font(get_font("montserrat_20"), 0)
        lbl.align(lv.ALIGN.LEFT_MID, 10, 0)

        sw = lv.switch(row)
        sw.align(lv.ALIGN.RIGHT_MID, -10, 0)
        if enabled:
            sw.add_state(lv.STATE.CHECKED)
