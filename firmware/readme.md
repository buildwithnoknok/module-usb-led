# Firmware — noknok LEDs

Firmware for the noknok LEDs module (CH32V203G6U6 — USB, 8× WS2812b RGB).

- Source: `src/` (built with [ch32fun](https://github.com/cnlohr/ch32fun); `Makefile` uses `extralibs/usbd.c`)
- Binaries: `bin/noknok_leds.bin` and `bin/noknok_leds.hex` — current release
- USB identity: VID `0x1209` / PID `0x4E4E`, appears as a CDC serial device ("noknok LEDs")

Firmware is licensed MIT (see `../LICENSE-firmware`). Toolchain and protocol follow the
[noknok Ecosystem guidelines](https://github.com/buildwithnoknok/Ecosystem).

## Flashing (USB bootloader — no programmer needed)
1. Fit the **BOOT0 jumper**, plug into USB-C, power-cycle → the chip enters the WCH USB bootloader.
2. In **WCHISPTool**: select chip **CH32V203 (G6U6)**, load **`bin/noknok_leds.bin`**, click Download. Leave "Enable RRP" **unchecked**.
3. Remove the BOOT0 jumper and power-cycle.

A brief **dim-white boot flash (~0.3 s)** means the firmware is running; a **"noknok LEDs" COM port** then appears.
A *constant* solid-white means the firmware is **not** running (old/failed flash or bootloader mode) — re-flash.

> You can also flash `bin/noknok_leds.hex` the same way, or via SWD with a WCH-Link-E (`make flash`).

## Controlling the module
- From a PC: PowerShell / Python helpers in [`../tools/`](../tools) (`noknok_leds.ps1`, `noknok_leds_pc.py`).
- From a Raspberry Pi Pico (USB host): the `NoknokLEDs` class in the brain-Pico `noknok.py` (`usb.core`, raw bulk to EP `0x02`).

**Command protocol** (raw bytes, host → module):

| Bytes | Action |
|-------|--------|
| `0x00` | all LEDs off |
| `0x01 R G B` | set all 8 LEDs |
| `0x02 i R G B` | set LED `i` (0–7) |
| `0x03 B` | global brightness 0–255 |
| `0x04 [24 bytes]` | set all 8 LEDs: 8 × (R G B) |
| `0x05` | explicit show |
| `0xF0` | identity query → module replies `0x4E 0x4E 0x04` |

---

## Firmware Change Record

### v1.5 — adds firmware version query (current)
- **`0xB1` GET_VERSION command** (noknok ecosystem standard, command range 0xB0–0xBF). The module replies with 4 bytes: `[PROTOCOL_VERSION, FW_MAJOR, FW_MINOR, FW_PATCH]` = `0x01, 1, 5, 0`. Lets the host (Pico or PC) read which firmware a module is running — needed by the OTA update flow. Matches the I2C modules' `0xB1` convention.
- No other behavioural changes from v1.4.

### v1.4 — first working release
- **USB stack switched from USBFS (fsusb) to USBD (FSDEV).** On this board the USBFS controller's D+ pull-up is not host-visible, so it never enumerated; the USBD/FSDEV controller (pull-up via `EXTEN_USBD_PU_EN`, the same path the WCH bootloader uses) enumerates reliably as a USB CDC device.
- **Manual clock bring-up: HSE 24 MHz × 2 = 48 MHz** (crystal-accurate 48 MHz USB clock). ch32fun's automatic CH32V203 clock setup does not work for this part (no flash wait-states above 24 MHz; it disables HSI mid-switch), so the firmware boots on the internal HSI and switches to the crystal + PLL itself.
- **Behaviour:** dim-white boot flash on power-up, then enumerates as "noknok LEDs" (VID `0x1209` / PID `0x4E4E`). LED commands are sent as raw bytes over the CDC data endpoint.
- Driveable from a PC (`../tools/`) or from a Pico acting as USB host (`usb.core`).

### v1.0 – v1.3 — bring-up (superseded, non-functional)
- Early builds used the USBFS (fsusb) stack and ch32fun's default 144 MHz clock. They did not run / did not enumerate due to the CH32V203 clock issues described under v1.4. Superseded.

### Abandoned experiment — full CDC line-coding (not released, no version assigned)
- An attempt (between v1.4 and v1.5) to add full CDC line-coding so standard host serial APIs (e.g. .NET `SerialPort.Open`) could open the port. Caused Windows "Code 10 — device cannot start"; root cause not isolated, so it was dropped. (PC tools instead open the port via a raw handle / pyserial, which works.)

## Known limitations
- The minimal CDC implementation does not fully answer line-coding requests, so .NET `SerialPort.Open()` is fussy. Use the provided PowerShell/Python tools (raw handle / pyserial), or `usb.core` raw bulk on the Pico.
- USB VID/PID are pid.codes **prototype** values (`0x1209` / `0x4E4E`) — assign production IDs before release.
