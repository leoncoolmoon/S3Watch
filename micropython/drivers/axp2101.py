"""
AXP2101 PMIC Driver wrapper for backwards compatibility
"""

from lib.hw.axp2101 import AXP2101 as _AXP2101

class AXP2101(_AXP2101):
    def refresh(self):
        self._refresh_status()

    def get_battery_info(self):
        return {
            "voltage_mv": self.battery_voltage_mv,
            "percentage": self.battery_percent,
            "charging": self.is_charging,
            "vbus": self.is_vbus_present
        }
