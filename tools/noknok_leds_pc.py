#!/usr/bin/env python3
"""
noknok_leds_pc.py â€” drive the noknok LEDs module from a PC (Windows / macOS / Linux).

The module is a USB CDC device (VID 0x1209, PID 0x4E4E). It appears as a virtual
serial port; this library talks the binary LED protocol over it.

Install once:
    pip install pyserial

Library use:
    from noknok_leds_pc import NoknokLEDs
    leds = NoknokLEDs.find()          # auto-detect by VID/PID (or identity query)
    leds.set_all(255, 0, 0)           # all red
    leds.set_pixel(3, 0, 255, 0)      # LED 3 green
    leds.set_brightness(64)           # dim
    leds.off()
    leds.close()

    # or as a context manager:
    with NoknokLEDs.find() as leds:
        leds.fill(0x00FF80)

Command line:
    python noknok_leds_pc.py list                 # list candidate ports
    python noknok_leds_pc.py all 255 0 0          # all red
    python noknok_leds_pc.py pixel 3 0 255 0      # LED 3 green
    python noknok_leds_pc.py bright 64            # brightness
    python noknok_leds_pc.py off                  # all off
    python noknok_leds_pc.py rainbow              # rainbow demo (Ctrl-C to stop)
    python noknok_leds_pc.py demo                 # full self-test
    python noknok_leds_pc.py --port COM12 all 0 0 255   # force a specific port

Protocol (host -> device):
    0x00              all off
    0x01 R G B        set all 8 LEDs
    0x02 i R G B      set LED i (0-7)
    0x03 B            global brightness 0-255
    0x04 [24 bytes]   set all 8 LEDs: 8 x (R G B)
    0x05              explicit show
    0xF0              identity query -> device replies [0x4E, 0x4E, 0x04]
"""

import sys
import time

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    sys.stderr.write("This tool needs pyserial.  Install it with:\n    pip install pyserial\n")
    raise

# â”€â”€ Module identity â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
USB_VID      = 0x1209
USB_PID      = 0x4E4E
LED_COUNT    = 8
MODULE_TYPE  = 0x04
IDENTITY     = bytes([0x4E, 0x4E, MODULE_TYPE])   # reply to the 0xF0 query


def _c(v: int) -> int:
    """Clamp to a 0-255 byte."""
    return 0 if v < 0 else 255 if v > 255 else int(v)


class NoknokLEDsError(RuntimeError):
    pass


class NoknokLEDs:
    """Driver for the noknok LEDs module over its USB CDC serial port."""

    LED_COUNT = LED_COUNT

    # â”€â”€ Construction / discovery â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    def __init__(self, port: str, *, verify: bool = True, timeout: float = 0.5):
        """
        Open the module on a known port, e.g.:
            NoknokLEDs("COM12")              # Windows
            NoknokLEDs("/dev/ttyACM0")       # Linux
            NoknokLEDs("/dev/cu.usbmodemXX") # macOS
        Set verify=False to skip the identity handshake.
        """
        self.port = port
        self._ser = serial.Serial(port, baudrate=115200, timeout=timeout, write_timeout=2)
        time.sleep(0.1)  # let the CDC settle after open
        if verify and not self.identify():
            self._ser.close()
            raise NoknokLEDsError(f"{port} did not respond as a noknok LEDs module")

    @classmethod
    def find(cls, *, timeout_sec: float = 5.0) -> "NoknokLEDs":
        """
        Auto-detect the module. First tries ports matching VID/PID; if none match
        (some OSes don't expose VID/PID on CDC), falls back to probing every port
        with the 0xF0 identity query. Returns a connected instance or raises.
        """
        deadline = time.monotonic() + timeout_sec
        while time.monotonic() < deadline:
            ports = list(serial.tools.list_ports.comports())
            # Pass 1: VID/PID match (fast, reliable on Windows/Linux)
            for p in ports:
                if (p.vid, p.pid) == (USB_VID, USB_PID):
                    try:
                        return cls(p.device, verify=True)
                    except (serial.SerialException, OSError, NoknokLEDsError):
                        pass
            # Pass 2: identity probe on the rest
            for p in ports:
                if (p.vid, p.pid) == (USB_VID, USB_PID):
                    continue
                try:
                    return cls(p.device, verify=True)
                except (serial.SerialException, OSError, NoknokLEDsError):
                    pass
            time.sleep(0.2)
        raise NoknokLEDsError("noknok LEDs module not found - is it plugged in?")

    @staticmethod
    def list_ports():
        """Return a list of (device, description, vid, pid) for all serial ports."""
        return [(p.device, p.description, p.vid, p.pid)
                for p in serial.tools.list_ports.comports()]

    # â”€â”€ Core commands â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    def off(self):
        """Turn all LEDs off."""
        self._send(bytes([0x00]))

    def set_all(self, r: int, g: int, b: int):
        """Set all 8 LEDs to one colour (R, G, B each 0-255)."""
        self._send(bytes([0x01, _c(r), _c(g), _c(b)]))

    def set_pixel(self, index: int, r: int, g: int, b: int):
        """Set a single LED (0-7) to a colour."""
        if not 0 <= index < LED_COUNT:
            raise ValueError(f"LED index must be 0-{LED_COUNT - 1}")
        self._send(bytes([0x02, index, _c(r), _c(g), _c(b)]))

    def set_brightness(self, brightness: int):
        """Global brightness 0-255, applied on top of the per-LED colours."""
        self._send(bytes([0x03, _c(brightness)]))

    def set_pixels(self, pixels):
        """
        Set all 8 LEDs at once. `pixels` is an iterable of up to 8 (r, g, b)
        tuples; any missing LEDs are turned off.
        """
        data = bytearray([0x04])
        for r, g, b in list(pixels)[:LED_COUNT]:
            data += bytes([_c(r), _c(g), _c(b)])
        while len(data) < 1 + LED_COUNT * 3:
            data += b"\x00\x00\x00"
        self._send(bytes(data))

    def fill(self, hex_color: int):
        """Set all LEDs to a 24-bit hex colour, e.g. fill(0xFF8000)."""
        self.set_all((hex_color >> 16) & 0xFF, (hex_color >> 8) & 0xFF, hex_color & 0xFF)

    def show(self):
        """Explicit show (setters already auto-show; here for completeness)."""
        self._send(bytes([0x05]))

    def identify(self) -> bool:
        """Send 0xF0 and check for the [0x4E,0x4E,0x04] identity reply."""
        try:
            self._ser.reset_input_buffer()
            self._send(bytes([0xF0]))
            return self._ser.read(3) == IDENTITY
        except (serial.SerialException, OSError):
            return False

    def version(self):
        """
        Send 0xB1 GET_VERSION; returns (protocol, major, minor, patch) or None.
        Requires firmware v1.5+ (older firmware does not answer 0xB1).
        """
        try:
            self._ser.reset_input_buffer()
            self._send(bytes([0xB1]))
            r = self._ser.read(4)
            return tuple(r) if len(r) == 4 else None
        except (serial.SerialException, OSError):
            return None

    # â”€â”€ Animations (blocking; Ctrl-C to stop) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    def rainbow(self, *, delay: float = 0.02, cycles: int = 0):
        """Rolling rainbow. cycles=0 runs until interrupted."""
        step = 0
        try:
            while cycles == 0 or step < 256 * cycles:
                base = step & 0xFF
                self.set_pixels(
                    _hsv((base + i * 256 // LED_COUNT) & 0xFF, 255, 200)
                    for i in range(LED_COUNT)
                )
                time.sleep(delay)
                step += 1
        except KeyboardInterrupt:
            pass
        self.off()

    def chase(self, r=0, g=80, b=255, *, delay: float = 0.08, loops: int = 0):
        """A single lit LED chasing around the ring. loops=0 = forever."""
        n = 0
        try:
            while loops == 0 or n < loops * LED_COUNT:
                px = [(0, 0, 0)] * LED_COUNT
                px[n % LED_COUNT] = (r, g, b)
                self.set_pixels(px)
                time.sleep(delay)
                n += 1
        except KeyboardInterrupt:
            pass
        self.off()

    # â”€â”€ Plumbing â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    def _send(self, data: bytes):
        self._ser.write(data)

    def close(self):
        try:
            self._ser.close()
        except Exception:
            pass

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def _hsv(h: int, s: int, v: int):
    """HSV (each 0-255) -> (r, g, b)."""
    if s == 0:
        return (v, v, v)
    region = h // 43
    rem = (h - region * 43) * 6
    p = (v * (255 - s)) >> 8
    q = (v * (255 - ((s * rem) >> 8))) >> 8
    t = (v * (255 - ((s * (255 - rem)) >> 8))) >> 8
    return [(v, t, p), (q, v, p), (p, v, t), (p, q, v), (t, p, v), (v, p, q)][region % 6]


# â”€â”€ Command-line interface â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
def _main(argv):
    args = list(argv)
    port = None
    if "--port" in args:
        i = args.index("--port")
        port = args[i + 1]
        del args[i:i + 2]

    if not args or args[0] in ("-h", "--help", "help"):
        print(__doc__)
        return 0

    cmd = args[0].lower()

    if cmd == "list":
        for dev, desc, vid, pid in NoknokLEDs.list_ports():
            tag = "  <-- noknok LEDs" if (vid, pid) == (USB_VID, USB_PID) else ""
            vp = f"{vid:04X}:{pid:04X}" if vid else "----:----"
            print(f"{dev:12} {vp}  {desc}{tag}")
        return 0

    leds = NoknokLEDs(port) if port else NoknokLEDs.find()
    try:
        if cmd == "off":
            leds.off()
        elif cmd == "version":
            v = leds.version()
            print(f"firmware v{v[1]}.{v[2]}.{v[3]} (protocol {v[0]})" if v
                  else "no version reply (firmware older than v1.5?)")
        elif cmd == "all":
            leds.set_all(int(args[1]), int(args[2]), int(args[3]))
        elif cmd == "pixel":
            leds.set_pixel(int(args[1]), int(args[2]), int(args[3]), int(args[4]))
        elif cmd == "bright":
            leds.set_brightness(int(args[1]))
        elif cmd == "fill":
            leds.fill(int(str(args[1]), 16))
        elif cmd == "rainbow":
            print("Rainbow - Ctrl-C to stop.")
            leds.rainbow()
        elif cmd == "chase":
            print("Chase - Ctrl-C to stop.")
            leds.chase()
        elif cmd == "demo":
            print("Self-test...")
            for name, col in (("red", (255, 0, 0)), ("green", (0, 255, 0)), ("blue", (0, 0, 255))):
                print(" ", name); leds.set_all(*col); time.sleep(0.7)
            print("  per-pixel")
            leds.set_pixels(_hsv(i * 32, 255, 200) for i in range(LED_COUNT)); time.sleep(1.5)
            print("  rainbow"); leds.rainbow(cycles=2)
            print("  off"); leds.off()
        else:
            print(f"Unknown command: {cmd}\nRun with --help for usage.")
            return 2
    finally:
        leds.close()
    return 0


if __name__ == "__main__":
    sys.exit(_main(sys.argv[1:]))
