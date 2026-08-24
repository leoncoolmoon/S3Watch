"""
AXP2101 PMIC Driver for MicroPython
Target Board: Waveshare ESP32-S3-Touch-AMOLED-2.06
"""

class AXP2101:
    I2C_ADDR = 0x34

    REG_CHIP_ID = 0x03
    REG_STATUS1 = 0x00
    REG_STATUS2 = 0x01
    REG_ADC_ENABLE = 0x30
    REG_BAT_VOLT_H = 0x34
    REG_BAT_VOLT_L = 0x35
    REG_BAT_PERCENT = 0xA4
    REG_DCDC_ENABLE = 0x80
    REG_ALDO_ENABLE = 0x90

    def __init__(self, i2c=None):
        self.i2c = i2c
        self._raw_voltage = 3800
        self._percent = 80
        self._charging = False
        self._vbus = False
        self.enable_battery_adc()

    def check_chip_id(self):
        if not self.i2c:
            return True
        try:
            chip_id = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_CHIP_ID, 1)[0]
            return chip_id == 0x4A
        except Exception:
            return False

    def enable_battery_adc(self):
        if not self.i2c:
            return
        try:
            self.i2c.writeto_mem(self.I2C_ADDR, self.REG_ADC_ENABLE, bytes([0x01]))
        except Exception:
            pass

    def power_on_main_rail(self):
        self.enable_dcdc(1, True)
        self.enable_dcdc(3, True)

    def power_on_display(self):
        self.enable_aldo(1, True)
        self.enable_aldo(2, True)

    def power_on_all_known(self):
        self.enable_dcdc(1, True)
        self.enable_dcdc(2, True)
        self.enable_dcdc(3, True)
        self.enable_aldo(1, True)
        self.enable_aldo(2, True)
        self.enable_aldo(3, True)
        self.enable_aldo(4, True)

    def enable_dcdc(self, channel, enable=True):
        if not self.i2c or channel < 1 or channel > 5:
            return
        try:
            curr = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_DCDC_ENABLE, 1)[0]
            bit = 1 << (channel - 1)
            new_val = (curr | bit) if enable else (curr & ~bit)
            self.i2c.writeto_mem(self.I2C_ADDR, self.REG_DCDC_ENABLE, bytes([new_val]))
        except Exception:
            pass

    def enable_aldo(self, channel, enable=True):
        if not self.i2c or channel < 1 or channel > 4:
            return
        try:
            curr = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_ALDO_ENABLE, 1)[0]
            bit = 1 << (channel - 1)
            new_val = (curr | bit) if enable else (curr & ~bit)
            self.i2c.writeto_mem(self.I2C_ADDR, self.REG_ALDO_ENABLE, bytes([new_val]))
        except Exception:
            pass

    def _refresh_status(self):
        if not self.i2c:
            return
        try:
            st1 = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_STATUS1, 1)[0]
            st2 = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_STATUS2, 1)[0]
            self._vbus = bool(st1 & 0x20)
            self._charging = bool(st2 & 0x04)

            vh = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_BAT_VOLT_H, 1)[0]
            vl = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_BAT_VOLT_L, 1)[0]
            self._raw_voltage = ((vh << 4) | (vl & 0x0F))

            pct = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_BAT_PERCENT, 1)[0]
            self._percent = min(100, max(0, pct & 0x7F))
        except Exception:
            pass

    def refresh(self):
        self._refresh_status()

    def get_battery_info(self):
        return {
            "voltage_mv": self.battery_voltage_mv,
            "percentage": self.battery_percent,
            "charging": self.is_charging,
            "vbus": self.is_vbus_present
        }

    @property
    def battery_percent(self):
        self._refresh_status()
        return self._percent

    @property
    def battery_voltage_raw(self):
        self._refresh_status()
        return self._raw_voltage

    @property
    def battery_voltage_mv(self):
        return float(self.battery_voltage_raw * 1.0)

    @property
    def is_charging(self):
        self._refresh_status()
        return self._charging

    @property
    def is_vbus_present(self):
        self._refresh_status()
        return self._vbus
