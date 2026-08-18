"""
SignalK Client and Alert/Data Cache Service for MicroPython
Manages WebSocket subscriptions and delta parsing for boat marine metrics and alerts.
"""

import json

try:
    import uasyncio as asyncio
except ImportError:
    import asyncio

class SignalKClient:
    def __init__(self, settings):
        self.settings = settings
        self.is_connected = False
        self.metrics = {
            "heading": 0.0,    # navigation.headingMagnetic
            "depth": 0.0,      # environment.depth.belowTransducer (ft)
            "sog": 0.0,        # navigation.speedOverGround (kt)
            "wind_angle": 0,   # environment.wind.angleApparent
            "wind_speed": 0.0  # environment.wind.speedApparent
        }
        self.alerts = [] # List of active notifications/alerts

    def get_url(self):
        host = self.settings.get("signalk_host", "192.168.1.1")
        port = self.settings.get("signalk_port", 3000)
        return f"ws://{host}:{port}/signalk/v1/stream?subscribe=self"

    async def connect_and_listen(self):
        url = self.get_url()
        print(f"Connecting to SignalK WebSocket: {url}")
        self.is_connected = True
        try:
            while self.is_connected:
                # Simulate / handle stream messages in async task loop
                await asyncio.sleep(1)
        except Exception as e:
            print(f"SignalK WebSocket error: {e}")
            self.is_connected = False

    def parse_delta(self, delta_json):
        try:
            data = json.loads(delta_json) if isinstance(delta_json, str) else delta_json
            updates = data.get("updates", [])
            for update in updates:
                values = update.get("values", [])
                for item in values:
                    path = item.get("path", "")
                    val = item.get("value")
                    self._update_metric(path, val)
        except Exception as e:
            print(f"SignalK parse error: {e}")

    def _update_metric(self, path, value):
        if value is None:
            return

        if path == "navigation.headingMagnetic":
            # Convert rad to degrees
            self.metrics["heading"] = (value * 57.2958) % 360
        elif path == "environment.depth.belowTransducer":
            # Convert m to ft
            self.metrics["depth"] = value * 3.28084
        elif path == "navigation.speedOverGround":
            # Convert m/s to knots
            self.metrics["sog"] = value * 1.94384
        elif path == "environment.wind.angleApparent":
            self.metrics["wind_angle"] = int((value * 57.2958) % 360)
        elif path == "environment.wind.speedApparent":
            self.metrics["wind_speed"] = value * 1.94384
        elif path.startswith("notifications."):
            self._update_alert(path, value)

    def _update_alert(self, path, alert_data):
        alert_name = path.replace("notifications.", "")
        state = alert_data.get("state", "normal") if isinstance(alert_data, dict) else "normal"
        msg = alert_data.get("message", alert_name) if isinstance(alert_data, dict) else alert_name

        # Evict if normal
        self.alerts = [a for a in self.alerts if a["path"] != path]

        if state != "normal":
            severity_order = {"emergency": 4, "alarm": 3, "warn": 2, "alert": 1}
            self.alerts.append({
                "path": path,
                "name": alert_name,
                "state": state,
                "message": msg,
                "severity": severity_order.get(state, 0)
            })
            # Sort highest severity first
            self.alerts.sort(key=lambda x: x["severity"], reverse=True)

    def get_metrics(self):
        return self.metrics

    def get_alerts(self):
        return self.alerts
