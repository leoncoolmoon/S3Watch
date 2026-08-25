"""
music.py —— 音乐播放器应用
"""
import lvgl as lv
import driver as hw
from main import get_font

APP_INFO = {
    "name": "Music / 音乐播放器",
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
    title.set_text("Music Player")
    title.set_style_text_font(get_font("montserrat_20"), 0)
    title.align(lv.ALIGN.TOP_MID, 0, 15)

    lbl_title = lv.label(scr)
    lbl_title.set_text("Track 01")
    lbl_title.set_style_text_font(get_font("montserrat_24"), 0)
    lbl_title.align(lv.ALIGN.CENTER, 0, -80)

    lbl_artist = lv.label(scr)
    lbl_artist.set_text("S3Watch Audio")
    lbl_artist.set_style_text_font(get_font("montserrat_16"), 0)
    lbl_artist.set_style_text_color(lv.color_hex(0x888888), 0)
    lbl_artist.align(lv.ALIGN.CENTER, 0, -50)

    is_playing = False

    btn_prev = lv.button(scr)
    btn_prev.set_size(60, 60)
    btn_prev.align(lv.ALIGN.CENTER, -90, 20)
    lbl_p = lv.label(btn_prev)
    lbl_p.set_text("<<")
    lbl_p.center()

    btn_play = lv.button(scr)
    btn_play.set_size(70, 70)
    btn_play.align(lv.ALIGN.CENTER, 0, 20)
    lbl_play = lv.label(btn_play)
    lbl_play.set_text("Play")
    lbl_play.center()

    btn_next = lv.button(scr)
    btn_next.set_size(60, 60)
    btn_next.align(lv.ALIGN.CENTER, 90, 20)
    lbl_n = lv.label(btn_next)
    lbl_n.set_text(">>")
    lbl_n.center()

    def play_cb(e):
        nonlocal is_playing
        try:
            spk = hw.get_audio_out()
            if not is_playing:
                spk.enable_speaker(True)
                is_playing = True
                lbl_play.set_text("Pause")
            else:
                spk.enable_speaker(False)
                is_playing = False
                lbl_play.set_text("Play")
        except Exception as ex:
            print(f"Audio control error: {ex}")

    btn_play.add_event_cb(play_cb, lv.EVENT.CLICKED, None)

    lbl_vol = lv.label(scr)
    lbl_vol.set_text("Volume")
    lbl_vol.set_style_text_font(get_font("montserrat_16"), 0)
    lbl_vol.align(lv.ALIGN.BOTTOM_MID, 0, -70)

    slider_v = lv.slider(scr)
    slider_v.set_range(-60, 0)
    slider_v.set_value(-20, lv.ANIM.OFF)
    slider_v.set_size(240, 15)
    slider_v.align(lv.ALIGN.BOTTOM_MID, 0, -40)

    def vol_cb(e):
        try:
            spk = hw.get_audio_out()
            spk.set_volume(slider_v.get_value())
        except Exception:
            pass

    slider_v.add_event_cb(vol_cb, lv.EVENT.VALUE_CHANGED, None)
