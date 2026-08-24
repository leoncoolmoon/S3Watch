"""
calendar_app.py —— 日历应用
"""
import lvgl as lv
import driver as hw

APP_INFO = {
    "name": "Calendar / 日历",
    "icon": None
}

def clean_obj(obj):
    if hasattr(obj, "clean"):
        obj.clean()

def add_back_button(parent):
    btn = lv.button(parent)
    btn.set_size(70, 36)
    btn.align(lv.ALIGN.TOP_LEFT, 10, 10)
    btn.set_style_bg_color(lv.color_hex(0x34495E), 0)
    lbl = lv.label(btn)
    lbl.set_text("< Back")
    lbl.center()
    def on_back(e):
        def async_back(t):
            if t: t.delete()
            try:
                import main
                main.create_main_ui()
            except Exception as ex:
                print(f"Back error: {ex}")
        lv.timer_create(async_back, 10, None)
    btn.add_event_cb(on_back, lv.EVENT.CLICKED, None)
    return btn

def run():
    hw.init_essential()
    scr = lv.screen_active()
    clean_obj(scr)
    scr.set_style_bg_color(lv.color_black(), 0)

    add_back_button(scr)

    title = lv.label(scr)
    title.set_text("Calendar")
    title.set_style_text_font(lv.font_montserrat_20, 0)
    title.align(lv.ALIGN.TOP_MID, 0, 15)

    cal = lv.calendar(scr)
    cal.set_size(360, 380)
    cal.align(lv.ALIGN.CENTER, 0, 20)
    cal.set_today_date(2025, 1, 1)
    cal.set_showed_date(2025, 1)

    lv.calendar_header_arrow(cal)
