"""
FT3168 Capacitive Touch Controller Driver for MicroPython
Target Board: Waveshare ESP32-S3-Touch-AMOLED-2.06
"""

try:
    import lvgl as lv
    LVGL_AVAILABLE = True
except ImportError:
    LVGL_AVAILABLE = False

from myboard import I2C_ADDR_TOUCH

class FT3168:
    I2C_ADDR = I2C_ADDR_TOUCH

    REG_TD_STATUS = 0x02
    REG_P1_XH = 0x03
    REG_P1_XL = 0x04
    REG_P1_YH = 0x05
    REG_P1_YL = 0x06

    def __init__(self, i2c=None):
        self.i2c = i2c
        self.indev_drv = None
        self._last_state = 1 # 1 = released

    def read(self):
        """
        Returns a list of touch tuples: [(x, y, event), ...]
        event: 0 = pressed, 1 = released, 2 = contact (continuous)
        Returns [] if no touch detected.
        """
        if not self.i2c:
            return []
        try:
            status = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_TD_STATUS, 1)[0]
            points = status & 0x0F
            if points == 0:
                if self._last_state != 1:
                    self._last_state = 1
                return []

            p1_xh = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_P1_XH, 1)[0]
            p1_xl = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_P1_XL, 1)[0]
            p1_yh = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_P1_YH, 1)[0]
            p1_yl = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_P1_YL, 1)[0]

            x = ((p1_xh & 0x0F) << 8) | p1_xl
            y = ((p1_yh & 0x0F) << 8) | p1_yl

            if self._last_state == 1:
                event = 0 # Pressed
            else:
                event = 2 # Contact / continuous

            self._last_state = 0
            return [(x, y, event)]
        except Exception:
            return []

    def read_touch(self):
        pts = self.read()
        if pts:
            x, y, event = pts[0]
            return {"x": x, "y": y, "points": len(pts)}
        return None

    def touch_read_cb(self, indev, data):
        pts = self.read()
        if pts:
            x, y, event = pts[0]
            data.point.x = x
            data.point.y = y
            data.state = lv.INDEV_STATE.PRESSED
        else:
            data.state = lv.INDEV_STATE.RELEASED

    def register_lvgl_indev(self):
        if not LVGL_AVAILABLE:
            return None

        indev = lv.indev_create()
        indev.set_type(lv.INDEV_TYPE.POINTER)
        indev.set_read_cb(self.touch_read_cb)
        self.indev_drv = indev
        return indev
