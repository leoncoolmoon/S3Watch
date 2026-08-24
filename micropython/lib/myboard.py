"""
板级配置 —— 集中存放所有引脚号、I2C 地址、总线参数。
以后移植到新硬件，理论上只需要改这一个文件，lib/ 下的其他抽象层
（display.py / touch.py / power.py 等）和上层业务代码都不用动。

当前配置对应：Waveshare ESP32-S3-Touch-AMOLED-2.06
"""

BOARD_NAME = "Waveshare ESP32-S3-Touch-AMOLED-2.06"

# ─── I2C 总线（触摸/IMU/RTC/PMIC/音频/GPIO扩展器共用）───────────
I2C_SCL = 14
I2C_SDA = 15
I2C_FREQ = 400_000

# ─── I2C 设备地址 ────────────────────────────────────────────
I2C_ADDR_TOUCH = 0x38       # FT3168
I2C_ADDR_PMIC = 0x34        # AXP2101
I2C_ADDR_RTC = 0x51         # PCF85063
I2C_ADDR_IMU = 0x6B         # QMI8658
I2C_ADDR_GPIO_EXPANDER = 0x40  # TCA9554
I2C_ADDR_AUDIO_OUT = 0x18   # ES8311（跟 FT3168 共存但地址不同）
I2C_ADDR_AUDIO_IN = 0x40    # ES7210（跟 TCA9554 地址矛盾，待实测确认，见项目笔记）

# ─── 显示屏 (CO5300, QSPI) ──────────────────────────────────
LCD_QSPI_HOST = 1  # 实测确认，不是 2
LCD_SCK = 11
LCD_D0 = 4
LCD_D1 = 5
LCD_D2 = 6
LCD_D3 = 7
LCD_CS = 12
LCD_RST = 8
LCD_TE = 13
LCD_WIDTH = 410
LCD_HEIGHT = 502
LCD_OFFSET_X = 22  # 实测确认的精确值
LCD_OFFSET_Y = 0
LCD_FREQ = 40_000_000

# ─── 触摸 (FT3168) ───────────────────────────────────────────
TOUCH_RST = 9
TOUCH_INT = 38

# ─── IMU (QMI8658) ───────────────────────────────────────────
IMU_INT = 21

# ─── RTC (PCF85063) ──────────────────────────────────────────
RTC_INT = 39

# ─── 音频 I2S 总线（ES8311 播放 + ES7210 录音共用）──────────
I2S_MCLK = 16
I2S_SCLK = 41   # BCLK
I2S_WS = 45     # LRCK
I2S_DATA_IN = 42   # ASDOUT，麦克风数据，ESP32 是接收方
I2S_DATA_OUT = 40  # DSDIN，喇叭数据，ESP32 是发送方
AUDIO_PA_ENABLE = 46
AUDIO_MCLK_FREQ = 4_096_000
AUDIO_SAMPLE_RATE = 16_000

# ─── SD 卡（SPI 模式，实测需要 SoftSPI）─────────────────────
SD_SCK = 2
SD_MOSI = 1   # 官方标注 CMD
SD_MISO = 3   # 官方标注 D0
SD_CS = 17

# ─── 按键 ────────────────────────────────────────────────────
BTN_BOOT = 0    # 低电平=按下
BTN_PWR = 10    # 高电平=按下（官方文档确认，跟 BOOT 极性相反）

# ─── 振动马达（未确认是否所有批次都有，见项目笔记）─────────
MOTOR_PIN = 18

# ─── USB（原生 USB-Serial-JTAG，供参考，一般不需要手动操作）──
USB_DM = 19
USB_DP = 20

