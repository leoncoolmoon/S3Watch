"""
world_clock.py —— 世界时钟应用
"""
import time
import machine
import lvgl as lv
import driver as hw
from main import get_font

APP_INFO = {
    "name": "World Clock / 世界时钟",
    "icon": None
}

CITIES = [
    ("Beijing", 8),
    ("London", 0),
    ("New York", -5),
    ("Tokyo", 9),
    ("Sydney", 11)
]

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
    title.set_text("World Clock")
    title.set_style_text_font(get_font("montserrat_20"), 0)
    title.align(lv.ALIGN.TOP_MID, 0, 15)

    container = lv.obj(scr)
    container.set_size(360, 400)
    container.align(lv.ALIGN.TOP_MID, 0, 60)
    container.set_flex_flow(lv.FLEX_FLOW.COLUMN)

    labels = []
    for city, offset in CITIES:
        row = lv.obj(container)
        row.set_size(340, 60)
        row.set_style_bg_color(lv.color_hex(0x1E272C), 0)

        lbl_c = lv.label(row)
        lbl_c.set_text(city)
        lbl_c.set_style_text_font(get_font("montserrat_16"), 0)
        lbl_c.align(lv.ALIGN.LEFT_MID, 10, 0)

        lbl_t = lv.label(row)
        lbl_t.set_style_text_font(get_font("montserrat_20"), 0)
        lbl_t.align(lv.ALIGN.RIGHT_MID, -10, 0)

        labels.append((offset, lbl_t))

    def update_times(timer):
        rtc = machine.RTC()
        dt = rtc.datetime()
        utc_h = dt[4] if len(dt) > 4 else 12
        utc_m = dt[5] if len(dt) > 5 else 0

        for offset, lbl in labels:
            h = (utc_h + offset) % 24
            lbl.set_text(f"{h:02d}:{utc_m:02d}")

    update_times(None)
    timer = lv.timer_create(update_times, 1000, None)
    def on_delete(e):
        try:
            timer.delete()
        except Exception:
            pass
    scr.add_event_cb(on_delete, lv.EVENT.DELETE, None)
