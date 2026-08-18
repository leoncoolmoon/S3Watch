"""
FT3168 Capacitive Touch Controller Driver for MicroPython
Handles touch point polling and gesture reporting for LVGL input driver integration.
"""

try:
    import lvgl as lv
    LVGL_AVAILABLE = True
except ImportError:
    LVGL_AVAILABLE = False

class FT3168:
    I2C_ADDR = 0x38

    REG_TD_STATUS = 0x02
    REG_P1_XH = 0x03
    REG_P1_XL = 0x04
    REG_P1_YH = 0x05
    REG_P1_YL = 0x06

    def __init__(self, i2c=None):
        self.i2c = i2c
        self.indev_drv = None

    def read_touch(self):
        if not self.i2c:
            return None
        try:
            status = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_TD_STATUS, 1)[0]
            points = status & 0x0F
            if points == 0:
                return None

            p1_xh = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_P1_XH, 1)[0]
            p1_xl = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_P1_XL, 1)[0]
            p1_yh = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_P1_YH, 1)[0]
            p1_yl = self.i2c.readfrom_mem(self.I2C_ADDR, self.REG_P1_YL, 1)[0]

            x = ((p1_xh & 0x0F) << 8) | p1_xl
            y = ((p1_yh & 0x0F) << 8) | p1_yl

            return {"x": x, "y": y, "points": points}
        except Exception as e:
            return None

    def touch_read_cb(self, indev, data):
        touch_point = self.read_touch()
        if touch_point:
            data.point.x = touch_point["x"]
            data.point.y = touch_point["y"]
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
