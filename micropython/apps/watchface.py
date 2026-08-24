"""
watchface.py —— 表盘应用/部件
"""
import time
import machine
import lvgl as lv
import driver as hw

APP_INFO = {
    "name": "Watchface / 表盘",
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

def create_watchface_tile(parent):
    clean_obj(parent)
    parent.set_style_bg_color(lv.color_black(), 0)

    lbl_time = lv.label(parent)
    lbl_time.set_style_text_font(lv.font_montserrat_48 if hasattr(lv, "font_montserrat_48") else lv.font_montserrat_24, 0)
    lbl_time.set_style_text_color(lv.color_white(), 0)
    lbl_time.align(lv.ALIGN.CENTER, 0, -40)

    lbl_date = lv.label(parent)
    lbl_date.set_style_text_font(lv.font_montserrat_20, 0)
    lbl_date.set_style_text_color(lv.color_hex(0xAAAAAA), 0)
    lbl_date.align(lv.ALIGN.CENTER, 0, 20)

    lbl_battery = lv.label(parent)
    lbl_battery.set_style_text_font(lv.font_montserrat_16, 0)
    lbl_battery.set_style_text_color(lv.color_hex(0x2ECC71), 0)
    lbl_battery.align(lv.ALIGN.BOTTOM_MID, 0, -60)

    def update_time_cb(timer):
        try:
            rtc = machine.RTC()
            dt = rtc.datetime()
            if len(dt) >= 6:
                y, m, d = dt[0], dt[1], dt[2]
                hh, mm, ss = dt[4], dt[5], dt[6] if len(dt) > 6 else 0
            else:
                y, m, d, hh, mm = 2025, 1, 1, 12, 0

            lbl_time.set_text(f"{hh:02d}:{mm:02d}")
            lbl_date.set_text(f"{y:04d}-{m:02d}-{d:02d}")

            pwr = hw.get_power()
            try:
                pct = pwr.battery_percent
            except Exception:
                pct = 100
            lbl_battery.set_text(f"BAT: {pct}%")
        except Exception:
            pass

    update_time_cb(None)
    timer = lv.timer_create(update_time_cb, 1000, None)

    def on_delete(e):
        try:
            timer.delete()
        except Exception:
            pass
    parent.add_event_cb(on_delete, lv.EVENT.DELETE, None)

def run():
    hw.init_essential()
    scr = lv.screen_active()
    create_watchface_tile(scr)
    add_back_button(scr)

if __name__ == "__main__":
    run()
