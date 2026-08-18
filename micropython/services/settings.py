"""
Settings Manager for MicroPython
Handles persistence of settings via config.json.
"""

import json

DEFAULT_SETTINGS = {
    "brightness": 40,
    "display_timeout_ms": 30000,
    "sound_enabled": True,
    "notify_volume": 80,
    "step_goal": 8000,
    "time_format_24h": True,
    "timezone": "UTC0",
    "wifi_enabled": False,
    "wifi_ssid": "",
    "wifi_pass": "",
    "ntp_server": "pool.ntp.org",
    "signalk_host": "192.168.1.1",
    "signalk_port": 3000,
    "watchface_style": 0,
    "alarm_timeout_min": 10
}

class SettingsManager:
    def __init__(self, filename="config.json"):
        self.filename = filename
        self.data = DEFAULT_SETTINGS.copy()

    def load(self):
        try:
            with open(self.filename, "r") as f:
                loaded = json.load(f)
                self.data.update(loaded)
        except Exception as e:
            print(f"Settings load exception: {e}, using defaults")
            self.save()

    def save(self):
        try:
            with open(self.filename, "w") as f:
                json.dump(self.data, f)
        except Exception as e:
            print(f"Settings save exception: {e}")

    def get(self, key, default=None):
        return self.data.get(key, default)

    def set(self, key, value):
        self.data[key] = value
        self.save()
