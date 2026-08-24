"""
boot.py - MicroPython boot entry point for S3Watch
Target Board: Waveshare ESP32-S3-Touch-AMOLED-2.06
Handles boot rescue check (BTN_BOOT) and hardware essential initialization.
"""

import sys
import time

# Ensure /lib or lib is in sys.path
if "lib" not in sys.path and "/lib" not in sys.path:
    sys.path.append("lib")

try:
    import machine
    HARDWARE_AVAILABLE = True
except ImportError:
    HARDWARE_AVAILABLE = False

from lib.myboard import BTN_BOOT

def check_boot_rescue():
    """
    If BOOT button (GPIO 0, active low) is held down on boot,
    raise KeyboardInterrupt to halt automatic application launch and fall back to REPL.
    """
    if HARDWARE_AVAILABLE:
        try:
            btn = machine.Pin(BTN_BOOT, machine.Pin.IN, machine.Pin.PULL_UP)
            if btn.value() == 0:
                print("\n[BOOT RESCUE] BTN_BOOT pressed! Halting boot sequence for REPL access...")
                time.sleep(0.5)
                raise KeyboardInterrupt("Boot rescue requested by user.")
        except Exception as e:
            if isinstance(e, KeyboardInterrupt):
                raise e

def main():
    print("S3Watch booting...")
    check_boot_rescue()

    # Import hardware manager explicitly from lib.main
    import lib.main as hw

    hw.init_essential()
    print("S3Watch hardware essential initialization complete.")

if __name__ == "__main__":
    main()
