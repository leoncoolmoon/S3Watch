"""
lib/main.py - Unified Hardware Scheduling Layer & Lazy-Loading Singleton Interface
Target Board: Waveshare ESP32-S3-Touch-AMOLED-2.06
"""

import sys
import gc
import time

try:
    import machine
    HARDWARE_AVAILABLE = True
except ImportError:
    HARDWARE_AVAILABLE = False

try:
    import os
    OS_AVAILABLE = True
except ImportError:
    OS_AVAILABLE = False

try:
    import lvgl as lv
    LVGL_AVAILABLE = True
except ImportError:
    LVGL_AVAILABLE = False

try:
    import myboard
except ImportError:
    from . import myboard

# Singleton instances
_i2c_bus = None
_power_inst = None
_display_inst = None
_touch_inst = None
_rtc_inst = None
_imu_inst = None
_audio_in_inst = None
_audio_out_inst = None
_gpio_exp_inst = None
_sd_inst = None

# Audio MCLK resource tracking
_mclk_pin = None
_audio_in_active = False
_audio_out_active = False


def _get_i2c():
    global _i2c_bus
    if _i2c_bus is None and HARDWARE_AVAILABLE:
        try:
            _i2c_bus = machine.I2C(
                0,
                scl=machine.Pin(myboard.I2C_SCL),
                sda=machine.Pin(myboard.I2C_SDA),
                freq=myboard.I2C_FREQ
            )
        except Exception as e:
            print(f"I2C Bus init error: {e}")
            _i2c_bus = None
    return _i2c_bus


def _start_audio_mclk():
    global _mclk_pin
    if HARDWARE_AVAILABLE and _mclk_pin is None:
        try:
            # Generate PWM on I2S_MCLK pin for 4.096 MHz
            _mclk_pin = machine.PWM(machine.Pin(myboard.I2S_MCLK), freq=myboard.AUDIO_MCLK_FREQ, duty=512)
        except Exception as e:
            print(f"Audio MCLK init error: {e}")


def _stop_audio_mclk():
    global _mclk_pin
    if not _audio_in_active and not _audio_out_active and _mclk_pin is not None:
        try:
            _mclk_pin.deinit()
        except Exception:
            pass
        _mclk_pin = None


def init_essential():
    """
    Once-off startup initialization:
    1. Initialize LVGL
    2. Power-on rails via PMIC
    3. Initialize & set GPIO expander outputs high (then release)
    4. Initialize CO5300 display
    5. Initialize FT3168 touch & attach to LVGL
    6. Sync time from RTC (then release RTC)
    """
    if LVGL_AVAILABLE and not lv.is_initialized():
        lv.init()

    pmu = get_power()
    if pmu:
        pmu.power_on_all_known()

    exp = get_gpio_expander()
    if exp:
        exp.set_all_output_high()
        release_gpio_expander()

    get_display()
    get_touch()
    sync_time_from_rtc()


# Power (AXP2101)
def get_power():
    global _power_inst
    if _power_inst is None:
        try:
            from hw.axp2101 import AXP2101
        except ImportError:
            from .hw.axp2101 import AXP2101
        i2c = _get_i2c()
        _power_inst = AXP2101(i2c=i2c)
    return _power_inst


# Display (CO5300)
def get_display():
    global _display_inst
    if _display_inst is None:
        try:
            from hw.co5300 import CO5300
        except ImportError:
            from .hw.co5300 import CO5300

        spi = None
        if HARDWARE_AVAILABLE:
            try:
                spi = machine.SPI(
                    myboard.LCD_QSPI_HOST,
                    baudrate=myboard.LCD_FREQ,
                    sck=machine.Pin(myboard.LCD_SCK),
                    mosi=machine.Pin(myboard.LCD_D0)
                )
            except Exception:
                pass

        _display_inst = CO5300(spi=spi, cs_pin=myboard.LCD_CS, rst_pin=myboard.LCD_RST)
        if LVGL_AVAILABLE:
            _display_inst.create_lvgl_display()
    return _display_inst


def sleep_display():
    disp = get_display()
    if disp:
        disp.sleep_display()


def wake_display():
    disp = get_display()
    if disp:
        disp.wake_display()


# Touch (FT3168)
def get_touch():
    global _touch_inst
    if _touch_inst is None:
        try:
            from hw.ft3168 import FT3168
        except ImportError:
            from .hw.ft3168 import FT3168
        i2c = _get_i2c()
        _touch_inst = FT3168(i2c=i2c)
        if LVGL_AVAILABLE:
            _touch_inst.register_lvgl_indev()
    return _touch_inst


# RTC (PCF85063)
def sync_time_from_rtc():
    rtc = get_rtc()
    ok = False
    if rtc:
        ok = rtc.sync_to_machine_rtc()
    release_rtc()
    return ok


def get_rtc():
    global _rtc_inst
    if _rtc_inst is None:
        try:
            from hw.pcf85063 import PCF85063
        except ImportError:
            from .hw.pcf85063 import PCF85063
        i2c = _get_i2c()
        _rtc_inst = PCF85063(i2c=i2c)
    return _rtc_inst


def release_rtc():
    global _rtc_inst
    _rtc_inst = None
    gc.collect()


# IMU (QMI8658)
def get_imu():
    global _imu_inst
    if _imu_inst is None:
        try:
            from hw.qmi8658 import QMI8658
        except ImportError:
            from .hw.qmi8658 import QMI8658
        i2c = _get_i2c()
        _imu_inst = QMI8658(i2c=i2c)
    return _imu_inst


def release_imu():
    global _imu_inst
    _imu_inst = None
    gc.collect()


# Audio Microphone (ES7210)
def get_audio_in(mic_gain_db=12):
    global _audio_in_inst, _audio_in_active
    if _audio_in_inst is None:
        _start_audio_mclk()
        _audio_in_active = True
        try:
            from hw.es7210 import ES7210
        except ImportError:
            from .hw.es7210 import ES7210
        i2c = _get_i2c()
        _audio_in_inst = ES7210(i2c=i2c, mic_gain_db=mic_gain_db)
    return _audio_in_inst


def release_audio_in():
    global _audio_in_inst, _audio_in_active
    if _audio_in_inst:
        try:
            _audio_in_inst.standby()
        except Exception:
            pass
        _audio_in_inst = None
    _audio_in_active = False
    _stop_audio_mclk()
    gc.collect()


# Audio Speaker (ES8311)
def get_audio_out():
    global _audio_out_inst, _audio_out_active
    if _audio_out_inst is None:
        _start_audio_mclk()
        _audio_out_active = True
        try:
            from hw.es8311 import ES8311
        except ImportError:
            from .hw.es8311 import ES8311
        i2c = _get_i2c()
        _audio_out_inst = ES8311(i2c=i2c, pa_pin=myboard.AUDIO_PA_ENABLE)
    return _audio_out_inst


def release_audio_out():
    global _audio_out_inst, _audio_out_active
    if _audio_out_inst:
        try:
            _audio_out_inst.enable_speaker(False)
            _audio_out_inst.mute(True)
        except Exception:
            pass
        _audio_out_inst = None
    _audio_out_active = False
    _stop_audio_mclk()
    gc.collect()


# SD Card Mount / Unmount
def mount_sdcard(mount_point="/sd"):
    global _sd_inst
    if _sd_inst is not None:
        return _sd_inst

    try:
        from hw.sdcard import SDCard
    except ImportError:
        from .hw.sdcard import SDCard

    spi = None
    if HARDWARE_AVAILABLE:
        try:
            spi = machine.SoftSPI(
                baudrate=10_000_000,
                sck=machine.Pin(myboard.SD_SCK),
                mosi=machine.Pin(myboard.SD_MOSI),
                miso=machine.Pin(myboard.SD_MISO)
            )
        except Exception:
            pass

    _sd_inst = SDCard(spi=spi, cs_pin=myboard.SD_CS)
    if OS_AVAILABLE and hasattr(os, "VfsFat"):
        try:
            vfs = os.VfsFat(_sd_inst)
            os.mount(vfs, mount_point)
        except Exception as e:
            print(f"SDCard mount error at {mount_point}: {e}")
    return _sd_inst


def unmount_sdcard(mount_point="/sd"):
    global _sd_inst
    if OS_AVAILABLE and hasattr(os, "umount"):
        try:
            os.umount(mount_point)
        except Exception:
            pass
    _sd_inst = None
    gc.collect()


# GPIO Expander (TCA9554)
def get_gpio_expander():
    global _gpio_exp_inst
    if _gpio_exp_inst is None:
        try:
            from hw.tca9554 import TCA9554
        except ImportError:
            from .hw.tca9554 import TCA9554
        i2c = _get_i2c()
        _gpio_exp_inst = TCA9554(i2c=i2c)
    return _gpio_exp_inst


def release_gpio_expander():
    global _gpio_exp_inst
    _gpio_exp_inst = None
    gc.collect()
