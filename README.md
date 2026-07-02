# noknok LEDs

A USB-controlled ring of 8 WS2812b RGB LEDs for the noknok ecosystem. The Raspberry Pi
Pico drives it as a **USB host**; it can also be driven directly from a PC.

## Features
- 8× WS2812b RGB LEDs (ring)
- CH32V203G6U6 microcontroller, USB 1.1 (CDC) device
- USB-C power + data; 2× Qwiic/Stemma-QT I2C pass-through connectors
- **Unique per-unit USB serial** derived from the chip UID — so the Conductor can tell
  identical modules apart and map them to roles (the USB counterpart of the I2C UID)
- Rich control: per-LED colour/brightness/timed-off in one command, plus 6 built-in
  animations (rainbow, breathe, theatre chase, colour wipe, twinkle, sundown)

## Status
- **Firmware: v1.8.1 — complete** (see [firmware/readme.md](firmware/readme.md) for the
  full command protocol, flashing instructions, and version history)
- Hardware: v1.0

## How it's controlled

### From a Raspberry Pi Pico (USB host) — the noknok Conductor
The Pico hosts the module over PIO-USB (D+ → GP16, D− → GP17, behind a **powered** hub
such as the DataHub). The `NoknokLEDs` driver lives in
[`brain-Pico/software/noknok_usb.py`](https://github.com/buildwithnoknok/brain-Pico/tree/main/software)
and is discovered by the Conductor:

```python
from noknok import Conductor

c = Conductor()
c.enumerate_usb()          # brings up the USB host port, finds all LED modules
# (use c.enumerate_all() for a product that mixes I2C + USB modules)

leds = c.leds[0]                                  # or c.by_uid("<serial>"), or c.role["lamp"]
leds.set_all(255, 0, 0)                           # all red
leds.set_pixel(3, 0, 255, 0)                      # LED 3 green
leds.set_brightness(128)                          # global brightness
leds.set_led(0xFF, 0, 0, 255, brightness=200, duration_ms=1000)  # all blue 1 s, then off
leds.play_preset(leds.PRESET_RAINBOW, speed=40)  # non-blocking animation on the module
leds.play_preset(leds.PRESET_SUNDOWN, speed=30, r=0, g=0, b=255)  # 30 min fade to off, blue
leds.off()
```

Each module carries a unique serial, so multiple LED modules on one hub are addressed
individually. See **[Ecosystem → enumeration.md](https://github.com/buildwithnoknok/Ecosystem/blob/main/software/enumeration.md)**
(USB Module Discovery & Identity) and **[roles.md](https://github.com/buildwithnoknok/Ecosystem/blob/main/software/roles.md)**
(role assignment — this is an *output* module, assigned by cue-and-confirm).

### From a PC
PowerShell / Python helpers in [`tools/`](tools) (`noknok_leds.ps1`, `noknok_leds_pc.py`)
auto-detect the module and expose the same commands.

## Repository Structure
- `/hardware` — KiCad files, schematics, 3D models
- `/firmware` — source (`src/`), built binaries (`bin/`), and the firmware change record (`readme.md`)
- `/tools` — PC control scripts

![USB LED Module](hardware/module-usb-led-front.png)
![USB LED Module](hardware/module-usb-led-back.png)
