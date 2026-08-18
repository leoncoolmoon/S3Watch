"""
S3Watch - Smartwatch Firmware in MicroPython with LVGL v9 API
Target Board: Waveshare ESP32-S3-Touch-AMOLED-2.06
"""

import sys
import time
import json

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

from services.settings import SettingsManager
from services.coordinator import TaskCoordinator
from services.wifi_ntp import WiFiManager
from services.signalk import SignalKClient

from drivers.axp2101 import AXP2101
from drivers.pcf85063a import PCF85063A
from drivers.qmi8658 import QMI8658
from drivers.ft3168 import FT3168
from drivers.co5300 import CO5300

from gui.ui_tileview import UITileview

class S3WatchApp:
    def __init__(self):
        print("Initializing S3Watch MicroPython Firmware...")
        self.settings = SettingsManager()
        self.settings.load()

        self.coordinator = TaskCoordinator()

        # Initialize Hardware Busses (I2C & SPI)
        if HARDWARE_AVAILABLE:
            self.i2c = machine.I2C(0, scl=machine.Pin(7), sda=machine.Pin(6), freq=400000)
            self.spi = machine.SPI(1, baudrate=40000000, sck=machine.Pin(11), mosi=machine.Pin(13))
        else:
            self.i2c = None
            self.spi = None

        # Initialize Hardware Drivers with bus instances
        self.pmu = AXP2101(i2c=self.i2c)
        self.rtc = PCF85063A(i2c=self.i2c)
        self.imu = QMI8658(i2c=self.i2c)
        self.touch = FT3168(i2c=self.i2c)
        self.display_drv = CO5300(spi=self.spi, cs_pin=12, dc_pin=14, rst_pin=9)

        # Services
        self.wifi = WiFiManager(self.settings)
        self.signalk = SignalKClient(self.settings)

        # UI Engine and LVGL v9 hardware registration
        if LVGL_AVAILABLE:
            if not lv.is_initialized():
                lv.init()

            # Register display and input hardware callbacks to LVGL
            self.display_drv.register_lvgl_display()
            self.touch.register_lvgl_indev()

            self.ui = UITileview(self.settings, self.pmu, self.rtc, self.imu, self.signalk)
        else:
            self.ui = None

    async def main_loop(self):
        print("S3Watch core services started.")
        # Start Periodic Task Coordinator
        self.coordinator.subscribe("axp_state", self.pmu.refresh, 3000)
        self.coordinator.subscribe("rtc_sync", self.rtc.get_time, 60000)
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
