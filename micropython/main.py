"""
S3Watch - Smartwatch Firmware in MicroPython with LVGL v9 API
Target Board: Waveshare ESP32-S3-Touch-AMOLED-2.06
"""

import sys
import time
import json

# Ensure lib directory is in sys.path
if "lib" not in sys.path and "/lib" not in sys.path:
    sys.path.append("lib")

try:
    import machine
    HARDWARE_AVAILABLE = True
except ImportError:
    HARDWARE_AVAILABLE = False

try:
    import uasyncio as asyncio
except ImportError:
    import asyncio

try:
    import lvgl as lv
    LVGL_AVAILABLE = True
except ImportError:
    LVGL_AVAILABLE = False
    print("Warning: LVGL v9 module not available in standard Python environment.")

import lib.main as hw

from services.settings import SettingsManager
from services.coordinator import TaskCoordinator
from services.wifi_ntp import WiFiManager
from services.signalk import SignalKClient

from gui.ui_tileview import UITileview

class S3WatchApp:
    def __init__(self):
        print("Initializing S3Watch MicroPython Firmware...")
        self.settings = SettingsManager()
        self.settings.load()

        self.coordinator = TaskCoordinator()

        # Initialize Essential Hardware via lib/main.py
        hw.init_essential()

        self.pmu = hw.get_power()
        self.rtc = hw.get_rtc()
        self.imu = hw.get_imu()
        self.touch = hw.get_touch()
        self.display_drv = hw.get_display()

        # Services
        self.wifi = WiFiManager(self.settings)
        self.signalk = SignalKClient(self.settings)

        # UI Engine and LVGL v9 integration
        if LVGL_AVAILABLE:
            self.ui = UITileview(self.settings, self.pmu, self.rtc, self.imu, self.signalk)
        else:
            self.ui = None

    async def main_loop(self):
        print("S3Watch core services started.")
        # Start Periodic Task Coordinator
        if hasattr(self.pmu, "refresh"):
            self.coordinator.subscribe("axp_state", self.pmu.refresh, 3000)
        if hasattr(self.rtc, "get_time"):
            self.coordinator.subscribe("rtc_sync", self.rtc.get_time, 60000)
        if hasattr(self.imu, "read_pedometer"):
            self.coordinator.subscribe("imu_step", self.imu.read_pedometer, 1000)

        asyncio.create_task(self.coordinator.run())

        while True:
            if LVGL_AVAILABLE and self.ui:
                self.ui.update()
                lv.timer_handler()
            await asyncio.sleep_ms(33) # ~30 FPS loop

def main():
    app = S3WatchApp()
    try:
        asyncio.run(app.main_loop())
    except KeyboardInterrupt:
        print("S3Watch stopped.")

if __name__ == "__main__":
    main()
