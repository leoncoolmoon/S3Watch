"""
FT3168 Touch Controller Driver wrapper for backwards compatibility
"""

from lib.hw.ft3168 import FT3168 as _FT3168

class FT3168(_FT3168):
    def read_touch(self):
        pts = self.read()
        if pts:
            x, y, event = pts[0]
            return {"x": x, "y": y, "points": len(pts)}
        return None
