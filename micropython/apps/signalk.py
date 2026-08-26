"""
signalk.py —— SignalK 航海仪表与告警应用
"""
import lvgl as lv
import driver as hw
from services import get_font, clean_obj, add_back_button

APP_INFO = {
    "name": "SignalK / 航海仪表",
    "icon": None
}

def run():
    hw.init_essential()
    scr = lv.screen_active()
    clean_obj(scr)
    scr.set_style_bg_color(lv.color_black(), 0)

    add_back_button(scr)

    title = lv.label(scr)
    title.set_text("SignalK Dashboard")
    title.set_style_text_font(get_font("montserrat_20"), 0)
    title.align(lv.ALIGN.TOP_MID, 0, 15)

    grid = lv.obj(scr)
    grid.set_size(360, 380)
    grid.align(lv.ALIGN.TOP_MID, 0, 60)
    grid.set_flex_flow(lv.FLEX_FLOW.ROW_WRAP)

    metrics = [
        ("SOG (Speed)", "6.4 kn"),
        ("COG (Course)", "182°"),
        ("Depth", "12.4 m"),
        ("Wind Speed", "14 kn"),
        ("Wind Angle", "45° P"),
        ("Heading", "180°")
    ]

    for name, val in metrics:
        card = lv.obj(grid)
        card.set_size(165, 110)
        card.set_style_bg_color(lv.color_hex(0x1E272C), 0)

        lbl_name = lv.label(card)
        lbl_name.set_text(name)
        lbl_name.set_style_text_font(get_font("montserrat_14"), 0)
        lbl_name.set_style_text_color(lv.color_hex(0xAAAAAA), 0)
        lbl_name.align(lv.ALIGN.TOP_LEFT, 5, 5)

        lbl_val = lv.label(card)
        lbl_val.set_text(val)
        lbl_val.set_style_text_font(get_font("montserrat_20"), 0)
        lbl_val.align(lv.ALIGN.CENTER, 0, 10)
