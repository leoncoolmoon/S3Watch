"""
AXP2101 Power Management IC Driver for MicroPython
Handles battery status, charging state, VBUS power, and ALDO rail control.
"""

class AXP2101:
    I2C_ADDR = 0x34

    REG_STATUS1 = 0x00
    REG_STATUS2 = 0x01
    REG_BAT_VOLT_H = 0x34
    REG_BAT_VOLT_L = 0x35
    REG_BAT_PERCENT = 0xA4
    REG_ALDO_ENABLE = 0x90

    def __init__(self, i2c=None):
        self.i2c = i2c
        self.battery_voltage_mv = 3800
        self.battery_percentage = 80
        self.is_charging = False
        self.vbus_present = False
        self.init_pmu()

    def init_pmu(self):
        if not self.i2c:
            return
        try:
            # Enable ALDO1-4 rails for AMOLED display & sensors
            self.enable_aldo_rails(True)
        except Exception as e:
            print(f"AXP2101 init error: {e}")

    def enable_aldo_rails(self, enable=True):
        if not self.i2c:
            return
        try:
            val = 0x0F if enable else 0x00
            self.i2c.writeto_mem(self.I2C_ADDR, self.REG_ALDO_ENABLE, bytes([val]))
        except Exception as e:
            print(f"AXP2101 rail control error: {e}")

    def refresh(self):
        if not self.i2c:
            return
        try:
            st1 = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_STATUS1, 1)[0]
            st2 = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_STATUS2, 1)[0]
            self.vbus_present = bool(st1 & 0x20)
            self.is_charging = bool(st2 & 0x04)

            vh = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_BAT_VOLT_H, 1)[0]
            vl = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_BAT_VOLT_L, 1)[0]
            self.battery_voltage_mv = ((vh << 4) | (vl & 0x0F)) * 1

            pct = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_BAT_PERCENT, 1)[0]
            self.battery_percentage = min(100, max(0, pct & 0x7F))
        except Exception as e:
            print(f"AXP2101 refresh error: {e}")

    def get_battery_info(self):
        return {
            "voltage_mv": self.battery_voltage_mv,
            "percentage": self.battery_percentage,
            "charging": self.is_charging,
            "vbus": self.vbus_present
        }
