"""
lib/main.py —— 硬件调度层。

设计原则：能懒加载的都懒加载。除了开机就必须要用的电源/屏幕/触摸，
其它硬件（RTC、IMU、音频、SD卡、GPIO扩展器）都是"用到才创建，
用完可以主动释放"，避免手表这种内存/电量都紧张的设备上把所有驱动
一次性常驻内存。

用法：
    import main as hw
    hw.init_essential()          # 开机跑一次：电源+屏幕+触摸+LVGL
    imu = hw.get_imu()           # 用到 IMU 时才创建
    ...
    hw.release_imu()             # 用完主动释放，回收内存
"""
import machine
import time
import gc
import i2c as _i2c_mod
import lcd_bus
import lvgl as lv

import myboard as board

# ============================================================
# 单例缓存，全部初始为 None，真正用到才创建
# ============================================================
_i2c_bus = None
_power = None
_display = None
_touch = None
_touch_indev = None
_rtc = None
_imu = None
_audio_out = None
_audio_in = None
_gpio_expander = None
_sdcard = None
_mclk_pwm = None  # ES7210/ES8311 共用的 I2S 主时钟，谁先用到就负责起振


# ============================================================
# I2C 总线（内部共用，所有 I2C 设备都通过这个拿 bus）
# ============================================================
def _get_i2c_bus():
    dir(board)
    global _i2c_bus
    if _i2c_bus is None:
        _i2c_bus = _i2c_mod.I2C.Bus(
            host=0, scl=board.I2C_SCL, sda=board.I2C_SDA,
            freq=board.I2C_FREQ, use_locks=False,
        )
    return _i2c_bus


def _i2c_device(addr):
    return _i2c_mod.I2C.Device(_get_i2c_bus(), dev_id=addr, reg_bits=8)


# ============================================================
# 电源 (AXP2101) —— 开机就需要，常驻
# ============================================================
def get_power():
    global _power
    if _power is None:
        from hw.axp2101 import AXP2101
        _power = AXP2101(_i2c_device(board.I2C_ADDR_PMIC))
        _power.power_on_all_known()
        time.sleep_ms(50)  # NOQA
    return _power


# ============================================================
# GPIO 扩展器 —— 只在开机时用一次（把所有引脚拉高），用完可以释放
# ============================================================
def get_gpio_expander():
    global _gpio_expander
    if _gpio_expander is None:
        from hw.tca9554 import TCA9554
        _gpio_expander = TCA9554(_i2c_device(board.I2C_ADDR_GPIO_EXPANDER))
    return _gpio_expander


def release_gpio_expander():
    global _gpio_expander
    _gpio_expander = None
    gc.collect()


# ============================================================
# 屏幕 (CO5300) —— 手表核心 UI，常驻
# ============================================================
def get_display():
    global _display
    if _display is None:
        from hw.co5300 import CO5300
        print("start screen")
        spi_bus = machine.SPI.Bus(
            host=board.LCD_QSPI_HOST,
            sck=board.LCD_SCK,
            quad_pins=(board.LCD_D0, board.LCD_D1, board.LCD_D2, board.LCD_D3),
        )
        display_bus = lcd_bus.SPIBus(
            spi_bus=spi_bus, cs=board.LCD_CS, dc=-1,
            freq=board.LCD_FREQ, quad=True,
        )
        _lcd_buf_rows = 40
        _lcd_buf_size = board.LCD_WIDTH * _lcd_buf_rows * 2

        _display = CO5300(
            data_bus=display_bus,
            display_width=board.LCD_WIDTH,
            display_height=board.LCD_HEIGHT,
            frame_buffer1=display_bus.allocate_framebuffer(
                _lcd_buf_size, lcd_bus.MEMORY_SPIRAM | lcd_bus.MEMORY_DMA
            ),
            offset_x=board.LCD_OFFSET_X,
            offset_y=board.LCD_OFFSET_Y,
            rgb565_byte_swap=True,
            reset_pin=machine.Pin(board.LCD_RST, machine.Pin.OUT),
            backlight_pin=None,  # AMOLED 自发光，没有背光电路
        )
        _display.reset()
        _display.init()
        _display.set_power(True)
    return _display


def sleep_display():
    """省电：让屏幕休眠，但不释放对象（框架/缓冲区还在，唤醒快）"""
    if _display is not None:
        _display.set_power(False)


def wake_display():
    if _display is not None:
        _display.set_power(True)


# ============================================================
# 触摸 (FT3168) —— 常驻，接入 LVGL 输入设备系统
# ============================================================
def get_touch():
    global _touch, _touch_indev
    if _touch is None:
        from hw.ft3168 import FT3168

        rst_pin = machine.Pin(board.TOUCH_RST, machine.Pin.OUT)
        rst_pin.value(0)
        time.sleep_ms(10)  # NOQA
        rst_pin.value(1)
        time.sleep_ms(300)  # NOQA

        _touch = FT3168(_i2c_device(board.I2C_ADDR_TOUCH), rst_pin=rst_pin)
        _touch.init()

        def _read_cb(indev_drv, data):
            try:
                points = _touch.read()
            except Exception:  # NOQA
                points = None
            if points:
                x, y, event = points[0]
                data.point.x = x
                data.point.y = y
                data.state = lv.INDEV_STATE.PRESSED
            else:
                data.state = lv.INDEV_STATE.RELEASED

        _touch_indev = lv.indev_create()
        _touch_indev.set_type(lv.INDEV_TYPE.POINTER)
        _touch_indev.set_read_cb(_read_cb)
    return _touch


# ============================================================
# RTC (PCF85063) —— 只在需要读/写外部 RTC 时才用，平时用
# machine.RTC()（内部时钟，几乎零开销）就够了，同步一次可以释放
# ============================================================
def get_rtc():
    global _rtc
    if _rtc is None:
        from hw.pcf85063 import PCF85063
        _rtc = PCF85063(_i2c_device(board.I2C_ADDR_RTC))
    return _rtc


def release_rtc():
    global _rtc
    _rtc = None
    gc.collect()


def sync_time_from_rtc():
    """开机同步一次外部 RTC 到系统时钟，同步完自动释放，不用常驻"""
    rtc = get_rtc()
    ok = False
    if rtc.is_time_valid():
        rtc.sync_to_machine_rtc()
        ok = True
    release_rtc()
    return ok


# ============================================================
# IMU (QMI8658) —— 按需加载，用完释放
# ============================================================
def get_imu():
    global _imu
    if _imu is None:
        from hw.qmi8658 import QMI8658
        _imu = QMI8658(_i2c_device(board.I2C_ADDR_IMU))
    return _imu


def release_imu():
    global _imu
    _imu = None
    gc.collect()


# ============================================================
# 音频 —— 按需加载。ES7210(录音)/ES8311(播放) 共用一路 MCLK，
# 谁先被用到就负责启动 PWM，两个都释放了才真正关掉 MCLK
# ============================================================
def _ensure_mclk():
    global _mclk_pwm
    if _mclk_pwm is None:
        _mclk_pwm = machine.PWM(
            machine.Pin(board.I2S_MCLK),
            freq=board.AUDIO_MCLK_FREQ, duty_u16=32768,
        )
        time.sleep_ms(20)  # NOQA


def _release_mclk_if_unused():
    global _mclk_pwm
    if _audio_in is None and _audio_out is None and _mclk_pwm is not None:
        _mclk_pwm.deinit()
        _mclk_pwm = None


def get_audio_in(mic_gain_db=12):
    global _audio_in
    if _audio_in is None:
        _ensure_mclk()
        from hw.es7210 import ES7210
        _audio_in = ES7210(
            _i2c_device(board.I2C_ADDR_AUDIO_IN),
            mclk_pin=board.I2S_MCLK, mic_gain_db=mic_gain_db,
        )
        _audio_in.init()
    return _audio_in


def release_audio_in():
    global _audio_in
    if _audio_in is not None:
        try:
            _audio_in.standby()
        except Exception:  # NOQA
            pass
    _audio_in = None
    _release_mclk_if_unused()
    gc.collect()


def get_audio_out():
    global _audio_out
    if _audio_out is None:
        _ensure_mclk()
        from hw.es8311 import ES8311
        _audio_out = ES8311(
            _i2c_device(board.I2C_ADDR_AUDIO_OUT), pa_pin=board.AUDIO_PA_ENABLE,
        )
        _audio_out.init()
    return _audio_out


def release_audio_out():
    global _audio_out
    if _audio_out is not None:
        try:
            _audio_out.enable_speaker(False)
        except Exception:  # NOQA
            pass
    _audio_out = None
    _release_mclk_if_unused()
    gc.collect()


# ============================================================
# TF 卡 —— 按需挂载，用完卸载
# ============================================================
def mount_sdcard(mount_point="/sd"):
    global _sdcard
    if _sdcard is None:
        import os
        from hw.sdcard import SDCard
        spi = machine.SoftSPI(
            baudrate=400_000, polarity=0, phase=0,
            sck=machine.Pin(board.SD_SCK, machine.Pin.OUT),
            mosi=machine.Pin(board.SD_MOSI, machine.Pin.OUT),
            miso=machine.Pin(board.SD_MISO, machine.Pin.IN),
        )
        cs = machine.Pin(board.SD_CS, machine.Pin.OUT, value=1)
        _sdcard = SDCard(spi, cs)
        os.mount(_sdcard, mount_point)
    return _sdcard


def unmount_sdcard(mount_point="/sd"):
    global _sdcard
    if _sdcard is not None:
        import os
        os.umount(mount_point)
        _sdcard = None
        gc.collect()


# ============================================================
# 开机必要初始化：LVGL + 电源 + GPIO扩展器(用完即释放) + 屏幕 + 触摸
# ============================================================
def init_essential():
    lv.init()  # 必须在 get_display() 之前
    get_power()

    expander = get_gpio_expander()
    try:
        expander.set_all_output_high()
    except Exception as e:  # NOQA
        print(f"GPIO 扩展器初始化失败（不一定致命）: {e}")
    release_gpio_expander()  # 只用一次，用完释放

    get_display()
    get_touch()
    sync_time_from_rtc()
