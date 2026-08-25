"""
settings.py —— 设置应用
"""
import lvgl as lv
import driver as hw
from main import get_font

APP_INFO = {
    "name": "Settings / 设置",
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
    title.set_text("Settings")
    title.set_style_text_font(get_font("montserrat_20"), 0)
    title.align(lv.ALIGN.TOP_MID, 0, 15)

    container = lv.obj(scr)
    container.set_size(360, 420)
    container.align(lv.ALIGN.TOP_MID, 0, 60)
    container.set_flex_flow(lv.FLEX_FLOW.COLUMN)
    container.set_style_bg_color(lv.color_black(), 0)

    lbl_b = lv.label(container)
    lbl_b.set_text("Display Brightness")
    lbl_b.set_style_text_font(get_font("montserrat_16"), 0)

    slider_b = lv.slider(container)
    slider_b.set_range(10, 100)
    disp = hw.get_display()
    slider_b.set_value(80, lv.ANIM.OFF)

    def brightness_cb(e):
        val = slider_b.get_value()
        disp.set_brightness(val)

    slider_b.add_event_cb(brightness_cb, lv.EVENT.VALUE_CHANGED, None)

    lbl_v = lv.label(container)
    lbl_v.set_text("Audio Volume")
    lbl_v.set_style_text_font(get_font("montserrat_16"), 0)

    slider_v = lv.slider(container)
    slider_v.set_range(-60, 0)
    slider_v.set_value(-20, lv.ANIM.OFF)

    def volume_cb(e):
        try:
            spk = hw.get_audio_out()
            spk.set_volume(slider_v.get_value())
        except Exception as ex:
            print(f"Volume change error: {ex}")

    slider_v.add_event_cb(volume_cb, lv.EVENT.VALUE_CHANGED, None)

    lbl_p = lv.label(container)
    lbl_p.set_style_text_font(get_font("montserrat_16"), 0)
    pwr = hw.get_power()
    try:
        pct = pwr.battery_percent
        v_mv = pwr.battery_voltage_mv
        lbl_p.set_text(f"Battery: {pct}% ({v_mv:.0f}mV)")
    except Exception:
        lbl_p.set_text("Battery: N/A")
