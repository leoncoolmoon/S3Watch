# sensors

Manages the QMI8658C IMU (accelerometer + gyroscope). Currently limited to power-state management and a test read hook; step-counting and motion classification have been removed.

## Power behaviour

The QMI8658C is hardwired to VCC3V3 (always-on) and boots in active mode (~270 µA). `sensors_low_power_idle()` puts it in Power-Down mode (~6 µA) at boot, since motion sensing is not used at runtime. The chip's digital interface remains reachable for test reads.

## Usage

```c
sensors_low_power_idle();   // call once at boot; ~270 µA → ~6 µA
```

## Dependencies

`esp32_s3_touch_amoled_2_06` (BSP — I2C bus to QMI8658C)
