"""
CO5300 AMOLED Display Driver wrapper for backwards compatibility
"""

from lib.hw.co5300 import CO5300 as _CO5300

class CO5300(_CO5300):
    def register_lvgl_display(self):
        return self.create_lvgl_display()
