"""
Task Coordinator Service for MicroPython
Shared periodic job scheduler using uasyncio.
"""

import time
try:
    import uasyncio as asyncio
except ImportError:
    import asyncio

class TaskCoordinator:
    def __init__(self, base_interval_ms=100):
        self.base_interval_ms = base_interval_ms
        self.subscribers = {}
        self.running = False

    def subscribe(self, name, callback, interval_ms):
        self.subscribers[name] = {
            "callback": callback,
            "interval_ms": interval_ms,
            "last_run": 0
        }

    def unsubscribe(self, name):
        if name in self.subscribers:
            del self.subscribers[name]

    async def run(self):
        self.running = True
        while self.running:
            now_ms = int(time.time() * 1000)
            for name, sub in self.subscribers.items():
                if now_ms - sub["last_run"] >= sub["interval_ms"]:
                    try:
                        res = sub["callback"]()
                        if asyncio.iscoroutine(res):
                            await res
                    except Exception as e:
                        print(f"TaskCoordinator subscriber error [{name}]: {e}")
                    sub["last_run"] = now_ms
            await asyncio.sleep_ms(self.base_interval_ms)

    def stop(self):
        self.running = False
