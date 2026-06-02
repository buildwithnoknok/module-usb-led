# noknok_leds_pc.py
# PC helper for the noknok LEDs module — connect directly from Windows/Mac/Linux.
# The module appears as a USB serial port (CDC virtual COM port).
#
# Requirements:  pip install pyserial
#
# Quick start:
#   from noknok_leds_pc import NoknokLEDsPC
#   leds = NoknokLEDsPC.find()     # auto-detect the module
#   leds.set_all(255, 0, 0)        # all red
#   leds.set_pixel(3, 0, 255, 0)   # LED 3 green
#   leds.off()

import serial
import serial.tools.list_ports
import time


class NoknokLEDsPC:
    """
    Control the noknok LEDs module directly from a PC over USB CDC serial.

    Either auto-detect with find(), or pass the COM port name directly:
        leds = NoknokLEDsPC("COM5")          # Windows
        leds = NoknokLEDsPC("/dev/ttyACM0")  # Linux
        leds = NoknokLEDsPC("/dev/cu.usbmodem001")  # Mac
    """

    LED_COUNT   = 8
    MODULE_TYPE = 0x04

    def __init__(self, port: str):
        self._ser = serial.Serial(port, baudrate=115200, timeout=0.5)
        time.sleep(0.1)  # give CDC a moment after open

    # ── Discovery ────────────────────────────────────────────────────────────

    @classmethod
    def find(cls, timeout_sec: float = 5.0) -> "NoknokLEDsPC":
        """
        Scan all serial ports for a noknok LEDs module.
        Sends an identity query (0xF0) and checks the response [0x4E, 0x4E, 0x04].
        Returns a connected NoknokLEDsPC instance, or raises RuntimeError if not found.
        """
        deadline = time.monotonic() + timeout_sec
        while time.monotonic() < deadline:
            for port_info in serial.tools.list_ports.comports():
                try:
                    s = serial.Serial(port_info.device, baudrate=115200, timeout=0.3)
                    time.sleep(0.05)
                    s.reset_input_buffer()
                    s.write(bytes([0xF0]))  # identity query
                    resp = s.read(3)
                    if resp == bytes([0x4E, 0x4E, 0x04]):
                        inst = cls.__new__(cls)
                        inst._ser = s
                        print(f"noknok LEDs found on {port_info.device}")
                        return inst
                    s.close()
                except (serial.SerialException, OSError):
                    pass
        raise RuntimeError("noknok LEDs module not found. Is it plugged in?")

    # ── LED control ───────────────────────────────────────────────────────────

    def set_all(self, r: int, g: int, b: int):
        """Set all 8 LEDs to one colour. R, G, B each 0–255."""
        self._send(bytes([0x01, _c(r), _c(g), _c(b)]))

    def set_pixel(self, index: int, r: int, g: int, b: int):
        """Set a single LED (0–7) to a colour."""
        if not 0 <= index < self.LED_COUNT:
            raise ValueError(f"LED index must be 0–{self.LED_COUNT - 1}")
        self._send(bytes([0x02, index, _c(r), _c(g), _c(b)]))

    def set_brightness(self, brightness: int):
        """Global brightness scale 0–255. Applied on top of individual colours."""
        self._send(bytes([0x03, _c(brightness)]))

    def fill(self, hex_color: int):
        """Set all LEDs to a hex colour, e.g. fill(0xFF0000) for red."""
        r = (hex_color >> 16) & 0xFF
        g = (hex_color >> 8)  & 0xFF
        b =  hex_color        & 0xFF
        self.set_all(r, g, b)

    def set_all_pixels(self, pixels):
        """
        Set all 8 LEDs individually in one call.
        pixels: list or tuple of 8 (r, g, b) tuples.

        Example:
            leds.set_all_pixels([
                (255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0),
                (0, 255, 255), (255, 0, 255), (255, 255, 255), (0, 0, 0),
            ])
        """
        data = bytearray([0x04])
        for r, g, b in list(pixels)[:self.LED_COUNT]:
            data += bytes([_c(r), _c(g), _c(b)])
        # Pad with zeros if fewer than 8 pixels provided
        while len(data) < 1 + self.LED_COUNT * 3:
            data += bytes([0, 0, 0])
        self._send(bytes(data))

    def show(self):
        """Explicit show — same as auto-show, useful for timing control."""
        self._send(bytes([0x05]))

    def off(self):
        """Turn all LEDs off."""
        self._send(bytes([0x00]))

    # ── Utilities ─────────────────────────────────────────────────────────────

    def rainbow(self, delay: float = 0.05, cycles: int = 2):
        """Simple rainbow demo. Blocks for duration."""
        import math
        for step in range(256 * cycles):
            h = step % 256
            pixels = []
            for i in range(self.LED_COUNT):
                hue = (h + i * 256 // self.LED_COUNT) % 256
                r, g, b = _hsv_to_rgb(hue, 255, 200)
                pixels.append((r, g, b))
            self.set_all_pixels(pixels)
            time.sleep(delay)
        self.off()

    def identify(self) -> bool:
        """Send identity query. Returns True if the correct module responds."""
        self._ser.reset_input_buffer()
        self._send(bytes([0xF0]))
        resp = self._ser.read(3)
        return resp == bytes([0x4E, 0x4E, 0x04])

    def close(self):
        """Close the serial connection."""
        self._ser.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    # ── Internal ──────────────────────────────────────────────────────────────

    def _send(self, data: bytes):
        self._ser.write(data)


# ── Helpers ───────────────────────────────────────────────────────────────────

def _c(v: int) -> int:
    """Clamp a value to 0–255."""
    return max(0, min(255, int(v)))


def _hsv_to_rgb(h: int, s: int, v: int):
    """Convert HSV (0–255 each) to (r, g, b). Used by rainbow()."""
    if s == 0:
        return v, v, v
    region = h // 43
    remainder = (h - region * 43) * 6
    p = v * (255 - s) // 255
    q = v * (255 - (s * remainder >> 8)) // 255
    t = v * (255 - (s * (255 - remainder) >> 8)) // 255
    if   region == 0: return v, t, p
    elif region == 1: return q, v, p
    elif region == 2: return p, v, t
    elif region == 3: return p, q, v
    elif region == 4: return t, p, v
    else:             return v, p, q


# ── Standalone demo ───────────────────────────────────────────────────────────

if __name__ == "__main__":
    print("Searching for noknok LEDs module...")
    with NoknokLEDsPC.find() as leds:
        print("Connected. Running demo...")

        print("  All red")
        leds.set_all(255, 0, 0)
        time.sleep(1)

        print("  All green")
        leds.set_all(0, 255, 0)
        time.sleep(1)

        print("  All blue")
        leds.set_all(0, 0, 255)
        time.sleep(1)

        print("  Individual pixels")
        leds.set_all_pixels([
            (255, 0,   0),   (255, 80,  0),   (255, 200, 0),   (0, 255, 0),
            (0,   200, 255), (0,   0,   255), (100, 0,   255), (255, 0, 100),
        ])
        time.sleep(2)

        print("  Rainbow")
        leds.rainbow(delay=0.03, cycles=2)

        print("  Off")
        leds.off()
    print("Done.")
