"""
services.py —— 通用系统服务与工具层。

集中管理字体加载、应用跳转、RTC 导航、界面退回按键及日志调试等通用功能。
"""
import sys
import time
import os
import machine
import lvgl as lv

_loaded_fonts = {}
_registered_drive_letter = None
_fs_driver_registered = False

DEBUG = True
DEBUG_TOUCH = True

def log_debug(msg):
    if DEBUG:
        print(f"[S3Watch] {msg}")

def init_fs_driver():
    """尝试注册 LVGL 文件系统驱动，并记录首个成功的盘符字母"""
    global _registered_drive_letter, _fs_driver_registered
    if not _fs_driver_registered:
        try:
            import fs_driver
            if hasattr(fs_driver, "fs_register"):
                for letter in ['S', 'A', 'F']:
                    try:
                        fs_driver.fs_register(lv.fs_drv_t(), letter)
                        _registered_drive_letter = letter
                        log_debug(f"FS Driver registered successfully with drive '{letter}:'")
                        break
                    except Exception as e:
                        log_debug(f"FS Driver register letter '{letter}' attempt failed: {e}")
        except Exception as e:
            log_debug(f"fs_driver module import skipped/failed: {e}")
        _fs_driver_registered = True

def get_font(name="montserrat_14"):
    """
    根据字体名称动态获取字体。
    自带字体仅包含 montserrat_12, 14, 16；
    其它字体优先从已成功注册的盘符（如 S:）加载，并打印 REPL 调试信息。
    """
    init_fs_driver()

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

    possible_paths = []
    if _registered_drive_letter:
        possible_paths.extend([
            f"{_registered_drive_letter}:/fonts/{name}.bin",
            f"{_registered_drive_letter}:/fonts/lv_font_{name}.bin",
            f"{_registered_drive_letter}:/{name}.bin",
        ])
    possible_paths.extend([
        f"S:/fonts/{name}.bin",
        f"S:/fonts/lv_font_{name}.bin",
        f"A:/fonts/{name}.bin",
        f"A:/fonts/lv_font_{name}.bin",
        f"/fonts/{name}.bin",
        f"/fonts/lv_font_{name}.bin",
        f"/{name}.bin",
    ])

    if hasattr(lv, "binfont_create"):
        for path in possible_paths:
            try:
                f = lv.binfont_create(path)
                if f:
                    _loaded_fonts[name] = f
                    print(f"[Font] Successfully loaded font '{name}' from: {path}")
                    return f
            except Exception:
                pass

    log_debug(f"Font '{name}' not found, fallback to montserrat_14")
    return getattr(lv, "font_montserrat_14", None)

def set_next_app(app_name="", args=None):
    """将目标程序信息写入 RTC 内存并直接触发系统复位 (machine.reset())"""
    try:
        rtc = machine.RTC()
        data = app_name.encode("utf-8") if app_name else b""
        rtc.memory(data)
        log_debug(f"RTC target set to '{app_name}', triggering system reset...")
        machine.reset()
    except Exception as e:
        log_debug(f"set_next_app error: {e}")

def check_rtc_navigation():
    """主程序启动时检查 RTC 内存是否有跳转目标"""
    try:
        rtc = machine.RTC()
        mem = rtc.memory()
        rtc.memory(b"")  # 立即清空，防止重复引导循环
        if mem:
            target = mem.decode("utf-8").strip()
            if target:
                log_debug(f"RTC navigation target detected: '{target}'")
                load_app(target)
                return True
    except Exception as e:
        log_debug(f"check_rtc_navigation error: {e}")
    return False

def load_app(app_module_name):
    """动态加载并运行 /apps 目录下的 app 模块"""
    log_debug(f"Launching app: {app_module_name}")
    try:
        mod = __import__(app_module_name)
        if hasattr(mod, "run"):
            mod.run()
        else:
            print(f"[Launcher] Module {app_module_name} has no run() function.")
    except Exception as e:
        print(f"[Launcher] Failed to launch {app_module_name}: {e}")

def clean_obj(obj):
    """安全清理 LVGL 对象子节点"""
    if hasattr(obj, "clean"):
        obj.clean()

def add_back_button(parent, target_app=""):
    """创建统一的返回按键"""
    btn = lv.button(parent)
    btn.set_size(70, 36)
    btn.align(lv.ALIGN.TOP_LEFT, 10, 10)
    btn.set_style_bg_color(lv.color_hex(0x34495E), 0)
    lbl = lv.label(btn)
    lbl.set_text("< Back")
    lbl.center()
    def on_back(e):
        try:
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
