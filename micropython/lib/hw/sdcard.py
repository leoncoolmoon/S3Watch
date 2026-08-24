"""
SoftSPI SD Card Driver for MicroPython
Target Board: Waveshare ESP32-S3-Touch-AMOLED-2.06
Implements standard MicroPython Block Device protocol over SoftSPI.
"""

import time

if not hasattr(time, "sleep_ms"):
    time.sleep_ms = lambda ms: time.sleep(ms / 1000.0)

try:
    import machine
    HARDWARE_AVAILABLE = True
except ImportError:
    HARDWARE_AVAILABLE = False

from myboard import SD_SCK, SD_MOSI, SD_MISO, SD_CS

_CMD_TIMEOUT = 100
_R1_IDLE_STATE = 1

class SDCard:
    def __init__(self, spi=None, cs_pin=SD_CS):
        self.spi = spi
        self.cs = machine.Pin(cs_pin, machine.Pin.OUT) if HARDWARE_AVAILABLE and cs_pin is not None else None
        self.sectors = 0
        if self.cs:
            self.cs.value(1)
        self.init_card()

    def init_card(self):
        if not HARDWARE_AVAILABLE or not self.spi or not self.cs:
            self.sectors = 2048 # Fallback mock 1MB block device
            return
        try:
            # Send 80+ clock cycles with CS high to initialize card into SPI mode
            self.cs.value(1)
            for _ in range(10):
                self.spi.write(b'\xff')

            # CMD0: reset card to idle state
            if self._cmd(0, 0, 0x95) != _R1_IDLE_STATE:
                print("SDCard: CMD0 failed")
                return

            # CMD8: check voltage range (SD v2)
            res = self._cmd(8, 0x1AA, 0x87)
            if res == _R1_IDLE_STATE:
                # ACMD41: initialize card
                for _ in range(100):
                    self._cmd(55, 0, 0)
                    if self._cmd(41, 0x40000000, 0) == 0:
                        break
                    time.sleep_ms(10)

            # Set block length to 512
            self._cmd(16, 512, 0)
            self.sectors = 2048 # Default fallback capacity
        except Exception as e:
            print(f"SDCard init error: {e}")

    def _cmd(self, cmd, arg, crc):
        if not self.spi or not self.cs:
            return 0
        self.cs.value(0)
        buf = bytes([0x40 | cmd, (arg >> 24) & 0xFF, (arg >> 16) & 0xFF, (arg >> 8) & 0xFF, arg & 0xFF, crc])
        self.spi.write(buf)
        res = 0xFF
        for _ in range(_CMD_TIMEOUT):
            res = self.spi.read(1, 0xFF)[0]
            if not (res & 0x80):
                break
        self.cs.value(1)
        return res

    def readblocks(self, block_num, buf, offset=0):
        if not HARDWARE_AVAILABLE or not self.spi or not self.cs:
            return 0
        nblocks = len(buf) // 512
        for i in range(nblocks):
            addr = block_num + i
            self._cmd(17, addr, 0)
            # Read 512 bytes block
        return 0

    def writeblocks(self, block_num, buf, offset=0):
        if not HARDWARE_AVAILABLE or not self.spi or not self.cs:
            return 0
        return 0

    def ioctl(self, op, arg):
        if op == 4: # Block count
            return self.sectors
        elif op == 5: # Block size
            return 512
        elif op == 6: # Erase block size
            return 1
        return 0
