"""
main.py —— 手表主程序入口。

初始化硬件，启动 UI TileView 框架与事件驱动循环。
"""
import time
import os
import lvgl as lv
import driver as hw
from services import (
    get_font, load_app, check_rtc_navigation, init_fs_driver,
    log_debug, DEBUG_TOUCH, clean_obj
)

def setup_touch_debug():
    if not DEBUG_TOUCH:
        return
    try:
        touch = hw.get_touch()
        log_debug(f"Touch driver active: {touch}")
    except Exception as e:
        log_debug(f"Touch driver setup query info: {e}")

def create_main_ui():
    hw.init_essential()
    init_fs_driver()
    setup_touch_debug()

    scr = lv.screen_active()
    clean_obj(scr)
    scr.set_style_bg_color(lv.color_black(), 0)

    # 创建 Tileview 页面
    tv = lv.tileview(scr)
    tv.set_size(410, 502)
    tv.center()
    tv.set_style_bg_color(lv.color_black(), 0)

    # Tile (0, 0): Watchface 表盘页
    tile_watchface = tv.add_tile(0, 0, lv.DIR.RIGHT)
    try:
        import watchface
        watchface.create_watchface_tile(tile_watchface)
    except Exception as e:
        lbl = lv.label(tile_watchface)
        lbl.set_text(f"Watchface error:\n{e}")
        lbl.center()

    # Tile (1, 0): App Picker 应用列表页
    tile_apps = tv.add_tile(1, 0, lv.DIR.LEFT)
    lbl_title = lv.label(tile_apps)
    lbl_title.set_text("Applications")
    lbl_title.set_style_text_font(get_font("montserrat_20"), 0)
    lbl_title.align(lv.ALIGN.TOP_MID, 0, 20)

    list_container = lv.obj(tile_apps)
    list_container.set_size(360, 420)
    list_container.align(lv.ALIGN.TOP_MID, 0, 60)
    list_container.set_flex_flow(lv.FLEX_FLOW.COLUMN)
    list_container.set_flex_align(lv.FLEX_ALIGN.START, lv.FLEX_ALIGN.CENTER, lv.FLEX_ALIGN.CENTER)

    # 扫描 /apps 目录下的应用
    apps_list = []
    try:
        files = os.listdir("/apps")
        for f in files:
            if f.endswith(".py") and not f.startswith("__"):
                apps_list.append(f[:-3])
            elif os.stat("/apps/" + f)[0] & 0x4000:  # Directory app
                apps_list.append(f)
    except Exception as e:
        log_debug(f"Error listing /apps: {e}")
        apps_list = ["stopwatch", "calculator", "calendar_app", "alarm", "world_clock", "music", "pedometer", "file_explorer", "signalk", "settings"]

    for app_name in sorted(apps_list):
        btn = lv.button(list_container)
        btn.set_size(320, 50)
        btn.set_style_bg_color(lv.color_hex(0x2C3E50), 0)
        btn.set_style_radius(12, 0)

        lbl = lv.label(btn)

        display_name = app_name
        try:
            mod = __import__(app_name)
            if hasattr(mod, "APP_INFO") and isinstance(mod.APP_INFO, dict):
                display_name = mod.APP_INFO.get("name", app_name)
        except Exception:
            pass

        lbl.set_text(display_name)
        lbl.center()

        def make_cb(name):
            def cb(e):
                log_debug(f"Button clicked for app: {name}")
                load_app(name)
            return cb

        btn.add_event_cb(make_cb(app_name), lv.EVENT.CLICKED, None)

def main():
    log_debug("Starting S3Watch Main Program...")
    if not check_rtc_navigation():
        create_main_ui()

    _last_tick = time.ticks_ms()
    while True:
        _now = time.ticks_ms()
        lv.tick_inc(time.ticks_diff(_now, _last_tick))
        _last_tick = _now
        lv.timer_handler()
        time.sleep_ms(20)

if __name__ == "__main__":
    main()
