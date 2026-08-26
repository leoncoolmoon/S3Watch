"""
file_explorer.py —— 文件浏览器应用
"""
import os
import machine
import lvgl as lv
import driver as hw
from main import get_font

APP_INFO = {
    "name": "Files / 文件浏览器",
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

    title = lv.label(scr)
    title.set_text("File Explorer")
    title.set_style_text_font(get_font("montserrat_20"), 0)
    title.align(lv.ALIGN.TOP_MID, 0, 15)

    try:
        hw.mount_sdcard("/sd")
    except Exception as e:
        print(f"SD Card mount info: {e}")

    path_lbl = lv.label(scr)
    path_lbl.set_text("/")
    path_lbl.set_style_text_font(get_font("montserrat_16"), 0)
    path_lbl.align(lv.ALIGN.TOP_LEFT, 20, 50)

    list_container = lv.obj(scr)
    list_container.set_size(360, 360)
    list_container.align(lv.ALIGN.TOP_MID, 0, 80)
    list_container.set_flex_flow(lv.FLEX_FLOW.COLUMN)

    current_path = "/"

    def populate(path):
        nonlocal current_path
        current_path = path
        path_lbl.set_text(path)
        clean_obj(list_container)

        try:
            items = os.listdir(path)
        except Exception:
            items = []

        if path != "/":
            btn_up = lv.button(list_container)
            btn_up.set_size(340, 40)
            lbl = lv.label(btn_up)
            lbl.set_text(".. (Up)")
            lbl.set_style_text_font(get_font("montserrat_16"), 0)
            lbl.center()
            def up_cb(e):
                parent = path.rsplit("/", 1)[0]
                if parent == "": parent = "/"
                populate(parent)
            btn_up.add_event_cb(up_cb, lv.EVENT.CLICKED, None)

        for item in items:
            btn = lv.button(list_container)
            btn.set_size(340, 40)
            btn.set_style_bg_color(lv.color_hex(0x2C3E50), 0)
            lbl = lv.label(btn)
            lbl.set_text(item)
            lbl.set_style_text_font(get_font("montserrat_16"), 0)
            lbl.center()

            full = (path + "/" + item).replace("//", "/")
            def make_cb(p):
                def cb(e):
                    try:
                        populate(p)
                    except Exception:
                        pass
                return cb

            btn.add_event_cb(make_cb(full), lv.EVENT.CLICKED, None)

    populate("/")
