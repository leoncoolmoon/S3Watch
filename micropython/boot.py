"""
boot.py —— 开机入口。

重要约束：
1. 不碰任何硬件初始化（防止 Thonny / REPL 连上时 SPI 被占用报错）。
2. 只做两件事：救砖判断（按住 BOOT 键直接进 REPL）+ 把 /lib 和 /apps 加进 sys.path。
"""
import sys
import machine

# 将 /lib 与 /apps 加入 sys.path
if "/lib" not in sys.path:
    sys.path.append("/lib")
if "/apps" not in sys.path:
    sys.path.append("/apps")

# 救砖检测：GPIO 0 (BOOT 按键，低电平有效)
boot_btn = machine.Pin(0, machine.Pin.IN, machine.Pin.PULL_UP)
if boot_btn.value() == 0:
    print("\n[BOOT] BOOT button held down. Entering REPL directly...")
    raise KeyboardInterrupt("Rescue boot requested via BOOT button.")
