"""
calendar_app.py —— 日历应用
"""
import lvgl as lv
import driver as hw
from services import get_font, clean_obj, add_back_button

APP_INFO = {
    "name": "Calendar / 日历",
    "icon": None
}

def run():
    hw.init_essential()
    scr = lv.screen_active()
    clean_obj(scr)
    scr.set_style_bg_color(lv.color_black(), 0)

    add_back_button(scr)

    title = lv.label(scr)
    title.set_text("Calendar")
    title.set_style_text_font(get_font("montserrat_20"), 0)
    title.align(lv.ALIGN.TOP_MID, 0, 15)

    cal = lv.calendar(scr)
    cal.set_size(360, 380)
    cal.align(lv.ALIGN.CENTER, 0, 20)
    cal.set_today_date(2025, 1, 1)
    cal.set_showed_date(2025, 1)

    lv.calendar_header_arrow(cal)
