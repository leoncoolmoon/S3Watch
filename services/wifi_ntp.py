"""
Wi-Fi and NTP Synchronization Service for MicroPython
Handles Wi-Fi scanning, connection, and SNTP time sync.
"""

import time

try:
    import network
    WIFI_HARDWARE = True
except ImportError:
    WIFI_HARDWARE = False

class WiFiManager:
    def __init__(self, settings):
        self.settings = settings
        self.wlan = network.WLAN(network.STA_IF) if WIFI_HARDWARE else None
        self.connected = False

    def is_enabled(self):
        return self.settings.get("wifi_enabled", False)

    def scan(self):
        if not WIFI_HARDWARE or not self.wlan:
            return [("Mock_SSID_1", -50), ("Mock_SSID_2", -70)]
        self.wlan.active(True)
        try:
            scan_results = self.wlan.scan()
            return [(res[0].decode('utf-8'), res[3]) for res in scan_results]
        except Exception as e:
            print(f"WiFi scan error: {e}")
            return []

    def connect(self, ssid=None, password=None):
        if not self.is_enabled():
            print("Wi-Fi disabled in settings.")
            return False

        if not ssid:
            ssid = self.settings.get("wifi_ssid", "")
            password = self.settings.get("wifi_pass", "")

        if not ssid:
            return False

        if not WIFI_HARDWARE or not self.wlan:
            self.connected = True
            return True

        self.wlan.active(True)
        self.wlan.connect(ssid, password)

        for _ in range(20): # Timeout ~10 seconds
            if self.wlan.isconnected():
                self.connected = True
                print(f"Connected to Wi-Fi: {ssid}")
                return True
            time.sleep(0.5)

        self.connected = False
        return False

    def sync_ntp(self):
        if not self.connected:
            if not self.connect():
                return False
        try:
            import ntptime
            ntp_server = self.settings.get("ntp_server", "pool.ntp.org")
            ntptime.host = ntp_server
            ntptime.settime()
            print("NTP time sync successful.")
            return True
        except Exception as e:
            print(f"NTP sync error: {e}")
            return False

    def disconnect(self):
        if WIFI_HARDWARE and self.wlan:
            self.wlan.active(False)
        self.connected = False
