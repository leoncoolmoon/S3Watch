# `lib/` 接口参考（头文件式速查手册）

这份文档描述 `lib/` 目录对外暴露的全部接口。以后任何时候只需要把这份文档发给协作者，不需要附带具体源码，就能准确知道怎么调用。

**硬件型号：** Waveshare ESP32-S3-Touch-AMOLED-2.06（410 × 502 AMOLED）  
**LVGL：** v9

---

## 目录结构

```text
/boot.py                  开机入口：救砖判断 + 调用 lib/main.py
/apps/                    保存软件
/font/                    存放外挂字体
/lib/
  main.py                 硬件调度层，懒加载单例，唯一对外接口
  myboard.py              引脚号 / I2C 地址 / 总线参数常量
  hw/                     具体芯片驱动实现（不建议直接 import，走 main.py）
    co5300.py
    ft3168.py
    axp2101.py
    pcf85063.py
    qmi8658.py
    es7210.py
    es8311.py
    tca9554.py
    sdcard.py
```

> **字体说明：** `/font/` 存放运行时动态加载的预转换 `.bin` 字体。LVGL 自带字体包括 `lv.font_montserrat_12`、`lv.font_montserrat_14`、`lv.font_montserrat_16`可以直接使用，其它字体需要加载，见后面说明。

---

## 设计原则

- **统一硬件入口：** 所有硬件访问都通过 `import main as hw`，不要直接 import `hw/` 下的具体驱动。
- **懒加载：** 除电源、屏幕、触摸这三样开机必需硬件外，其它硬件（RTC、IMU、音频、SD 卡、GPIO 扩展器）都是第一次调用对应 `get_X()` 时才创建并初始化；重复调用返回同一个实例，不会重复初始化。
- **主动释放：** 用完调用对应的 `release_X()`。驱动支持时会尝试让芯片进入待机，然后清空引用并触发 `gc.collect()`。
- **共享资源：** I2C 总线全局只有一条，由 `main.py` 统一管理。音频 MCLK 由 ES7210 / ES8311 共用：谁先使用谁负责启动，两个设备都释放后才关闭。
- **LVGL 版本：** v9

---

# `lib/main.py` —— 唯一入口

```python
import main as hw
```

## 开机初始化

```python
hw.init_essential()
```

一次性完成：

1. `lv.init()`
2. 电源上电
3. GPIO 扩展器初始化（用完自动释放）
4. 屏幕初始化
5. 触摸初始化，并自动接入 LVGL 输入设备
6. RTC 同步系统时钟（同步完成后自动释放）

> **必须在其它任何 `get_X()` 调用之前执行一次。**

无返回值。

---

## 电源

```python
power = hw.get_power()  # 返回 AXP2101 实例，常驻不释放
```

`power` 对象方法 / 属性：

| 接口 | 返回值 / 说明 |
|---|---|
| `.power_on_main_rail()` | 开启主电源轨 |
| `.power_on_display()` | 开启显示电源 |
| `.power_on_all_known()` | 开启所有已知电源 |
| `.check_chip_id()` | `bool`；期望芯片 ID 为 `0x4A` |
| `.enable_battery_adc()` | 启用电池 ADC |
| `.battery_percent` | `int`，0–100；**属性，不是方法** |
| `.battery_voltage_raw` | `int`，14-bit ADC 原始值 |
| `.battery_voltage_mv` | `float`，单位 mV；当前换算为 `raw × 1.0` |
| `.is_charging` | `bool` |
| `.is_vbus_present` | `bool`，USB 供电是否存在 |
| `.enable_dcdc(channel, enable=True)` | `channel`：1–5 |
| `.enable_aldo(channel, enable=True)` | `channel`：1–4 |

---

## 屏幕

```python
display = hw.get_display()  # 返回 CO5300 实例，常驻

hw.sleep_display()           # 休眠但不释放对象，唤醒快
hw.wake_display()
```

`display` 对象是 LVGL 的 `display_driver_framework.DisplayDriver` 子类。创建后已经完成：

- `reset()`
- `init()`
- `set_power(True)`

因此可以直接使用标准 LVGL API（例如 `lv.screen_active()`）绘图，无需手动调用底层初始化方法。

其它可用方法：

- `.set_backlight(percent)`：0–100。AMOLED 没有传统背光电路，实际控制的是面板亮度寄存器。
- `.display_on()`
- `.display_off()`

---

## 触摸

```python
touch = hw.get_touch()  # 返回 FT3168 实例，常驻，已自动接入 LVGL 输入设备
```

调用一次 `hw.get_touch()` 后，LVGL 控件（按钮、滑动条等）即可直接响应触摸，无需额外配置。

`touch` 对象也可以手动读取坐标：

```python
touch.read()
```

返回：

```text
[(x, y, event), ...]
```

无触摸时返回空列表。

| `event` | 含义 |
|---:|---|
| `0` | 按下 |
| `1` | 抬起 |
| `2` | 持续接触 |

---

## RTC

```python
ok = hw.sync_time_from_rtc()  # 开机同步一次到 machine.RTC()，自动释放

rtc = hw.get_rtc()             # 需要手动读写外部 RTC 时才使用
hw.release_rtc()               # 用完释放
```

`rtc` 对象方法：

| 接口 | 返回值 / 说明 |
|---|---|
| `.datetime()` | `(year, month, day, weekday, hour, minute, second)` |
| `.datetime((year, month, day, weekday, hour, minute, second))` | 设置时间 |
| `.is_time_valid()` | `bool`；`False` 表示曾经掉电，时间不可信 |
| `.sync_to_machine_rtc()` | 同步到系统 `machine.RTC()` |

日常读取时间建议直接使用：

```python
machine.RTC().datetime()
```

系统内置，零额外开销，不需要每次 `get_rtc()`。

---

## IMU（六轴）

```python
imu = hw.get_imu()       # 用到才创建
hw.release_imu()         # 用完释放
```

`imu` 对象方法：

| 接口 | 返回值 / 说明 |
|---|---|
| `.read_accel_g()` | `(ax, ay, az)`，单位 g |
| `.read_raw()` | `(ax, ay, az, gx, gy, gz)`，原始 16-bit 值 |
| `.read_gyro_raw()` | `(gx, gy, gz)`，原始值 |
| `.calibrate_gyro(num_samples=200, sample_delay_ms=5)` | 返回零点偏移元组，并在内部记住 |
| `.read_gyro_calibrated()` | `(gx, gy, gz)`，已减去零点偏移 |

> 加速度计换算成 g 的公式已验证准确（±8G 量程）。陀螺仪角速度换算成 °/s 的比例因子尚未核实，因此目前只能使用原始值进行相对判断。

---

## 音频

### 麦克风（ES7210）

```python
mic = hw.get_audio_in(mic_gain_db=12)  # 默认增益 12 dB
hw.release_audio_in()
```

`mic` 对象方法：

- `.init()`：`get_audio_in()` 内部已经调用，无需重复调用。
- `.set_mic_gain(db)`
- `.standby()`

### 扬声器（ES8311）

```python
speaker = hw.get_audio_out()
hw.release_audio_out()
```

`speaker` 对象方法：

| 接口 | 说明 |
|---|---|
| `.init()` | 默认播放模式；内部调用 `init_playback()` |
| `.init_mic()` | 麦克风模式（DAC 静音）；一般不需要，麦克风应使用 `hw.get_audio_in()` |
| `.enable_speaker(enable=True)` | 功放开关，GPIO46 |
| `.set_volume(level=0)` | `-128 ~ 0 dB`；`0` 为最大音量 |
| `.get_volume()` | 读取当前音量寄存器值 |
| `.set_mic_gain(db)` | 设置麦克风增益 |
| `.mute(enable)` | 静音控制 |
| `.mute_dac(enable)` | DAC 静音控制 |
| `.dump_regs()` | 调试：打印全部寄存器 |

> **I2S 总线需要由业务代码自行使用 `machine.I2S(...)` 建立。** `main.py` 只负责 I2C 配置侧和共享 MCLK。
>
> - 麦克风：STEREO 格式（L = MIC1，R = MIC2），业务代码只取左声道。
> - 扬声器：MONO。
> - 引脚从 `myboard.I2S_SCLK`、`myboard.I2S_WS`、`myboard.I2S_DATA_IN`、`myboard.I2S_DATA_OUT` 获取。

---

## TF 卡

```python
sd = hw.mount_sdcard("/sd")  # 使用 SoftSPI；硬件 SPI 实测不可用
hw.unmount_sdcard("/sd")
```

---

## GPIO 扩展器（TCA9554）

```python
exp = hw.get_gpio_expander()
hw.release_gpio_expander()
```

`exp` 对象方法：

| 接口 | 返回值 / 说明 |
|---|---|
| `.set_all_output_high()` | 将所有输出置高 |
| `.read_status()` | `(config, output, input)` |

---

# `lib/myboard.py` —— 常量速查

| 分类 | 关键常量 |
|---|---|
| I2C | `I2C_SCL=14` · `I2C_SDA=15` · `I2C_FREQ=400_000` |
| I2C 地址 | `I2C_ADDR_TOUCH=0x38` · `I2C_ADDR_PMIC=0x34` · `I2C_ADDR_RTC=0x51` · `I2C_ADDR_IMU=0x6B` · `I2C_ADDR_GPIO_EXPANDER=0x40` · `I2C_ADDR_AUDIO_OUT=0x18` · `I2C_ADDR_AUDIO_IN=0x40` ⚠️ 存疑，见下方已知问题 |
| 屏幕 | `LCD_QSPI_HOST=1` · `LCD_SCK=11` · `LCD_D0~D3=4,5,6,7` · `LCD_CS=12` · `LCD_RST=8` · `LCD_WIDTH=410` · `LCD_HEIGHT=502` · `LCD_OFFSET_X=22` · `LCD_OFFSET_Y=0` · `LCD_FREQ=40_000_000` |
| 触摸 | `TOUCH_RST=9` · `TOUCH_INT=38` |
| IMU | `IMU_INT=21` |
| RTC | `RTC_INT=39` |
| 音频 | `I2S_MCLK=16` · `I2S_SCLK=41` · `I2S_WS=45` · `I2S_DATA_IN=42` · `I2S_DATA_OUT=40` · `AUDIO_PA_ENABLE=46` · `AUDIO_MCLK_FREQ=4_096_000` · `AUDIO_SAMPLE_RATE=16_000` |
| SD 卡 | `SD_SCK=2` · `SD_MOSI=1` · `SD_MISO=3` · `SD_CS=17`；**必须 SoftSPI，硬件 SPI 实测失败** |
| 按键 | `BTN_BOOT=0`（低电平按下） · `BTN_PWR=10`（高电平按下，与 BOOT 相反） |
| 马达 | `MOTOR_PIN=18`（未确认所有批次都有安装，见已知问题） |

---

# 已知问题 / 待确认

1. **`I2C_ADDR_AUDIO_IN`（ES7210）与 `I2C_ADDR_GPIO_EXPANDER`（TCA9554）均为 `0x40`**
   - 两个不同来源的资料互相矛盾。
   - `board.json` 曾提示 ES7210 可能是 `0x41`。
   - 需要使用 `i2c.scan()` + 实际读写行为交叉验证。

2. **振动马达（GPIO18）目前测试无反应**
   - 原理图确认是标准三极管开关电路，不是 DRV2605 芯片。
   - 怀疑与 R7（0 Ω 可选跳线电阻）有关。
   - 可能不是所有批次都有安装。
   - 官方 Wiki 主页也完全没有提及这一功能。

3. **AXP2101 LDO 开关寄存器目前为“ALDO1-4 + BLDO1-2 全开”**
   - 目前为简化测试而全部开启。
   - 尚不清楚具体哪一路对应哪个外设。
   - 后续如需精细化省电，需要逐路确认对应关系并改成按需开关。

4. **陀螺仪角速度的物理单位换算比例未核实**
   - `read_gyro_raw()` / `read_gyro_calibrated()` 返回芯片原始值。
   - 当前不是已经标定的 °/s 数值。

5. **`boot.py` 中 `raise KeyboardInterrupt` 的救砖行为尚未实测确认**
   - 能否真正跳过后续代码并直接进入 REPL，取决于具体 MicroPython 版本及端口行为。

---

# 换硬件时怎么改

1. 新驱动放进 `lib/hw/`
2. 修改 `lib/myboard.py` 中的引脚、地址及相关总线参数常量
3. 修改 `lib/main.py` 中对应 `get_X()` 函数内的 `from hw.xxx import ...`
4. 上层业务代码的调用方式**完全不用修改**

---

# 外挂字体（`/font` 目录动态加载）

`/font` 目录存放预转换的 `.bin` 格式字体文件，可在运行时动态加载。

## 字体列表

| 字体 | 字号 | 文件名 | 大小 | 说明 |
|---|---:|---|---:|---|
| Chicago | 24 | `chicago24.bin` | 1.8 KB | 复古芝加哥字体，适合数码风格 |
| Chicago | 36 | `chicago36.bin` | 3.4 KB | — |
| Chicago | 48 | `chicago48.bin` | 5.4 KB | — |
| Cozette | 24 | `cozette24.bin` | 139 KB | 像素风格等宽字体，含丰富字符 |
| Cozette | 36 | `cozette36.bin` | 268 KB | — |
| Cozette | 48 | `cozette48.bin` | 434 KB | — |
| Montserrat | 18 | `lv_font_montserrat_18.bin` | 3.4 KB | LVGL 标准字体（扩展尺寸） |
| Montserrat | 20 | `lv_font_montserrat_20.bin` | 4.1 KB | LVGL 标准字体（扩展尺寸） |
| Montserrat | 24 | `lv_font_montserrat_24.bin` | 5.3 KB | LVGL 标准字体（扩展尺寸） |
| Montserrat | 24 | `montserrat24.bin` | 9.8 KB | 备选版本 |
| Montserrat | 36 | `montserrat36.bin` | 20 KB | — |
| Montserrat | 48 | `montserrat48.bin` | 36 KB | — |
| Tabler | 36 | `tabler36.bin` | 1.4 KB | 极简风格字体 |
| Tabler | 48 | `tabler48.bin` | 2.4 KB | 极简风格字体 |

## 使用示例

```python
import lvgl as lv

# 加载字体文件
font = lv.binfont_create("A:/font/lv_font_montserrat_24.bin")

# 注意：路径格式取决于文件系统挂载点
# 如果 SD 卡挂载在 "/sd"，则使用：
# font = lv.binfont_create("/sd/font/xxx.bin")
#
# 如果使用内部 Flash，则可能使用：
# font = lv.binfont_create("/font/xxx.bin")

# 应用到控件
label = lv.label(lv.screen_active())
label.set_style_text_font(font, 0)
label.set_text("Hello 24px!")
```

> **路径提示：** `A:/font/...` 只是 LVGL 文件系统驱动配置下的一种路径写法。实际项目中应根据当前文件系统挂载方式使用对应路径。

## 字体选择建议

| 使用场景 | 推荐 |
|---|---|
| 数码 / 复古界面 | Chicago |
| 像素风格 / 等宽显示 | Cozette |
| 通用 UI | Montserrat |
| 大字号数字 / 标题 | Montserrat 36 / 48 |
| 极简图标 / 字形 | Tabler |

---

## 快速索引

| 需求 | 调用 |
|---|---|
| 初始化硬件 | `hw.init_essential()` |
| 获取电源 | `hw.get_power()` |
| 获取屏幕 | `hw.get_display()` |
| 获取触摸 | `hw.get_touch()` |
| 获取 RTC | `hw.get_rtc()` |
| 同步 RTC | `hw.sync_time_from_rtc()` |
| 获取 IMU | `hw.get_imu()` |
| 获取麦克风 | `hw.get_audio_in()` |
| 获取扬声器 | `hw.get_audio_out()` |
| 挂载 SD 卡 | `hw.mount_sdcard("/sd")` |
| 获取 GPIO 扩展器 | `hw.get_gpio_expander()` |
| 动态加载字体 | `lv.binfont_create(...)` |
