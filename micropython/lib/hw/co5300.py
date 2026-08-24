"""
CO5300 QSPI AMOLED 显示驱动，用于 lvgl_micropython 的 DISPLAY=<路径> 机制。

框架部分（继承 display_driver_framework.DisplayDriver、set_params 的 QSPI 命令
打包方式、_set_memory_location 的写法）是照抄项目里唯一现成的 QSPI 驱动
axs15231b.py（同一个作者 Kevin G. Schlosser / straga 维护），置信度高。

初始化字节序列是从你贴的 Arduino_CO5300.h 里的 co5300_init_operations 表
直接翻译过来的，命令码、参数值和顺序跟原文件一一对应。

⚠️ 需要你上板验证的两处：
1. _set_memory_location 里用了 CASET+PASET 分开发送（CO5300 头文件里两个命令
   是分开定义的），跟 axs15231b 把 y 窗口合并进 RAMWR 命令字节的做法不一样。
   如果刷新时图像错位/花屏，第一个怀疑对象是这里。
2. MADCTL 的镜像常量直接照抄头文件里的 0x02 / 0x05（不是标准 MIPI 的
   0x40/0x80/0x20），这是原厂驱动的写法，保留原样，但没有单独测试过。
"""
import display_driver_framework
from micropython import const  # NOQA
import lcd_bus
import lvgl as lv  # NOQA
import time

STATE_HIGH = display_driver_framework.STATE_HIGH
STATE_LOW = display_driver_framework.STATE_LOW
STATE_PWM = display_driver_framework.STATE_PWM
BYTE_ORDER_RGB = display_driver_framework.BYTE_ORDER_RGB
BYTE_ORDER_BGR = display_driver_framework.BYTE_ORDER_BGR

# --- 命令码：和 CO5300 头文件保持一致 ---
_SW_RESET = const(0x01)
_SLPIN = const(0x10)
_SLPOUT = const(0x11)
_DISPON = const(0x29)
_CASET = const(0x2A)
_PASET = const(0x2B)
_RAMWR = const(0x2C)
_MADCTL = const(0x36)
_PIXFMT = const(0x3A)
_WDBRIGHTNESSVALNOR = const(0x51)
_WCTRLD1 = const(0x53)
_WCE = const(0x58)
_WDBRIGHTNESSVALHBM = const(0x63)
_SPIMODECTL = const(0xC4)
_BANK_SWITCH = const(0xFE)  # 头文件原表里没起名字的私有寄存器，照抄

_SLPOUT_DELAY = const(120)
_FINAL_DELAY = const(10)

# QSPI 命令包装方式，跟 axs15231b 完全一致
_WRITE_CMD = const(0x02)
_WRITE_COLOR = const(0x32)

# MADCTL：直接照抄头文件里的非标准值，没有验证过，先能亮屏为主
_MADCTL_X_AXIS_FLIP = const(0x02)
_MADCTL_Y_AXIS_FLIP = const(0x05)
_MADCTL_RGB = const(0x00)
_MADCTL_BGR = const(0x08)


class CO5300(display_driver_framework.DisplayDriver):

    def __init__(
        self,
        data_bus,
        display_width,
        display_height,
        frame_buffer1=None,
        frame_buffer2=None,
        reset_pin=None,
        reset_state=STATE_HIGH,
        power_pin=None,
        power_on_state=STATE_HIGH,
        backlight_pin=None,
        backlight_on_state=STATE_HIGH,
        offset_x=0,
        offset_y=0,
        color_byte_order=BYTE_ORDER_RGB,
        color_space=lv.COLOR_FORMAT.RGB565,  # NOQA  CO5300 默认表里用的是 16bit
        rgb565_byte_swap=False,
    ):
        num_lanes = data_bus.get_lane_count()
        if isinstance(data_bus, lcd_bus.SPIBus) and num_lanes == 4:
            self.__qspi = True
            _cmd_bits = 32
        else:
            self.__qspi = False
            _cmd_bits = 8

        if not isinstance(data_bus, lcd_bus.SPIBus):
            raise RuntimeError('CO5300 目前只按 QSPI(4-line SPIBus) 场景移植，其它总线未做')

        self._brightness = 0xD0

        super().__init__(
            data_bus,
            display_width,
            display_height,
            frame_buffer1,
            frame_buffer2,
            reset_pin,
            reset_state,
            power_pin,
            power_on_state,
            backlight_pin,
            backlight_on_state,
            offset_x,
            offset_y,
            color_byte_order,
            color_space,  # NOQA
            rgb565_byte_swap,
            _cmd_bits=_cmd_bits,
            _param_bits=8,
            _init_bus=True,
        )

    def reset(self):
        if self._reset_pin is None:
            self.set_params(_SW_RESET)
            time.sleep_ms(120)  # NOQA
        else:
            # 修正：之前这里用 not self._reset_state 计算极性，结尾停在"低"电平，
            # 如果 CO5300 是低电平复位（拉低=复位，拉高=运行——这是 turfptax
            # 那份验证过能点亮屏幕的代码采用的时序），等于全程把芯片摁在复位里，
            # 从来没真正启动过。这可能是"所有指令都不报错但芯片毫无反应"的真正原因。
            # 直接照抄验证过的时序：高→低→高，结尾停在"高"（正常运行状态）。
            self._reset_pin.value(1)
            time.sleep_ms(10)  # NOQA
            self._reset_pin.value(0)
            time.sleep_ms(20)  # NOQA
            self._reset_pin.value(1)
            time.sleep_ms(200)  # NOQA  头文件里 CO5300_RST_DELAY = 200

    def init(self, type=None):  # NOQA
        param_buf = self._param_buf
        param_mv = self._param_mv

        # 第一步：单独 SLPOUT，然后延时（对应原表的 BEGIN_WRITE ... END_WRITE 之前那段）
        self.set_params(_SLPOUT)
        time.sleep_ms(_SLPOUT_DELAY)  # NOQA

        # 第二步：一次性初始化序列，逐条对应原表
        param_buf[0] = 0x00
        self.set_params(_BANK_SWITCH, param_mv[:1])

        param_buf[0] = 0x80
        self.set_params(_SPIMODECTL, param_mv[:1])

        color_size = lv.color_format_get_size(self._color_space)
        param_buf[0] = 0x55 if color_size == 2 else 0x77  # 16bit vs 18/24bit
        self.set_params(_PIXFMT, param_mv[:1])

        param_buf[0] = 0x20
        self.set_params(_WCTRLD1, param_mv[:1])

        param_buf[0] = 0xFF
        self.set_params(_WDBRIGHTNESSVALHBM, param_mv[:1])

        self.set_params(_DISPON)

        param_buf[0] = 0xD0  # 正常模式亮度，默认值，后面可以用 set_brightness 调
        self.set_params(_WDBRIGHTNESSVALNOR, param_mv[:1])

        param_buf[0] = 0x00  # 高对比度模式关闭
        self.set_params(_WCE, param_mv[:1])

        time.sleep_ms(_FINAL_DELAY)  # NOQA

        self._initilized = True

    def set_brightness(self, value):
        """value: 0-100"""
        value = int(value / 100.0 * 255)
        value = max(0x00, min(value, 0xFF))
        self._brightness = value
        self._param_buf[0] = value
        self.set_params(_WDBRIGHTNESSVALNOR, self._param_mv[:1])

    def get_brightness(self):
        return round(self._brightness / 255.0 * 100.0, 1)

    def set_params(self, cmd, params=None):
        if self.__qspi:
            cmd &= 0xFF
            cmd <<= 8
            cmd |= _WRITE_CMD << 24
        self._data_bus.tx_param(cmd, params)

    def _set_memory_location(self, x1: int, y1: int, x2: int, y2: int):
        param_buf = self._param_buf

        param_buf[0] = (x1 >> 8) & 0xFF
        param_buf[1] = x1 & 0xFF
        param_buf[2] = (x2 >> 8) & 0xFF
        param_buf[3] = x2 & 0xFF
        self.set_params(_CASET, self._param_mv[:4])  # 修正：改成走 set_params()，
                                                        # 之前直接调 tx_param() 绕过了
                                                        # QSPI 命令封装，发出去的是裸的
                                                        # 0x2A 而不是 0x02002A00

        param_buf[0] = (y1 >> 8) & 0xFF
        param_buf[1] = y1 & 0xFF
        param_buf[2] = (y2 >> 8) & 0xFF
        param_buf[3] = y2 & 0xFF
        self.set_params(_PASET, self._param_mv[:4])  # 同上，修正

        cmd = _RAMWR
        if self.__qspi:
            cmd &= 0xFF
            cmd <<= 8
            cmd |= _WRITE_COLOR << 24
        return cmd
