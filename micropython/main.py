"""
main.py —— 手表主程序入口。

初始化硬件并启动 UI TileView 框架 ( Watchface / App Picker / Settings )，
并在主循环中持续调用 lv.timer_handler() 处理 LVGL 事件。
"""
import sys
import time
import gc
import os
import lvgl as lv
import driver as hw

_loaded_fonts = {}

def get_font(name="montserrat_14"):
    """
    根据字体名称动态获取字体。
    自带字体仅包含 montserrat_12, 14, 16；
    其它字体优先尝试从 /fonts/ 动态加载 bin 文件，若失败则退回 montserrat_14。
    """
    builtin_map = {
        "12": getattr(lv, "font_montserrat_12", None),
        "14": getattr(lv, "font_montserrat_14", None),
        "16": getattr(lv, "font_montserrat_16", None),
        "montserrat_12": getattr(lv, "font_montserrat_12", None),
        "montserrat_14": getattr(lv, "font_montserrat_14", None),
        "montserrat_16": getattr(lv, "font_montserrat_16", None),
    }
    if name in builtin_map and builtin_map[name] is not None:
        return builtin_map[name]

    if name in _loaded_fonts:
        return _loaded_fonts[name]

    possible_paths = [
        f"/fonts/lv_font_{name}.bin",
        f"/fonts/{name}.bin",
        f"A:/fonts/lv_font_{name}.bin",
        f"A:/fonts/{name}.bin",
    ]
    if hasattr(lv, "binfont_create"):
        for path in possible_paths:
            try:
                f = lv.binfont_create(path)
                if f:
                    _loaded_fonts[name] = f
                    return f
            except Exception:
                pass

    return getattr(lv, "font_montserrat_14", None)

def load_app(app_module_name):
    """动态加载并运行 /apps 目录下的 app 模块"""
    try:
        mod = __import__(app_module_name)
        if hasattr(mod, "run"):
            mod.run()
        else:
            print(f"[Launcher] Module {app_module_name} has no run() function.")
    except Exception as e:
        print(f"[Launcher] Failed to launch {app_module_name}: {e}")

def create_main_ui():
    hw.init_essential()

    scr = lv.screen_active()
    if hasattr(scr, "clean"):
        scr.clean()
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
        print(f"[Launcher] Error listing /apps: {e}")
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
                load_app(name)
            return cb

        btn.add_event_cb(make_cb(app_name), lv.EVENT.CLICKED, None)

def main():
    create_main_ui()
    while True:
        lv.timer_handler()
        time.sleep_ms(20)

if __name__ == "__main__":
    main()
