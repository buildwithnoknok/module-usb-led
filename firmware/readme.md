# Firmware — noknok LEDs

Firmware for the noknok LEDs module (CH32V203G6U6 — USB, 8× WS2812b RGB).

- Source: `src/` (built with [ch32fun](https://github.com/cnlohr/ch32fun); `Makefile` uses `extralibs/usbd.c`)
- Binaries: `bin/noknok_leds.bin` and `bin/noknok_leds.hex` — current release
- USB identity: VID `0x1209` / PID `0x4E4E` (app) / PID `0x4E42` (bootloader). Appears as a CDC serial device ("noknok LEDs"). Each unit has a **unique serial number** derived from its chip UID (v1.6+).

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
| `0x10 i R G B BR dLo dHi` | **SET_LED** — LED `i` (`0xFF` = all), colour, brightness, duration ms little-endian (0 = hold) |
| `0x20 preset speed R G B` | **PLAY_PRESET** 1–6 (rainbow / breathe / theatre-chase / colour-wipe / twinkle / **sundown**); `speed` = ms per step (0 = default) — **EXCEPT preset 6 (sundown), where `speed` means MINUTES instead (0 = default 30)**; `R G B` = base colour |
| `0xF0` | identity query → module replies `0x4E 0x4E 0x04` |
| `0xB1` | version query → replies `[PROTOCOL, MAJOR, MINOR, PATCH]` |

---

## Firmware Change Record

### v1.8.1 — sundown preset (current) — released 2026-07-02
- **New preset 6 = SUNDOWN.** `0x20 6 minutes R G B` plays a **one-shot** fade from full
  brightness to off in the caller-supplied colour, over `minutes` minutes (not the usual
  `speed`-as-ms/step — see below). Unlike presets 1–5, which loop forever until the next
  command, SUNDOWN runs once and stops itself (LEDs off) at the end instead of repeating.
  Intended for a bedtime/wind-down light (e.g. blue, 30 min).
- **Easing is CONCAVE (quadratic ease-out), not linear:** brightness follows
  `(1 - elapsed/total)²`, so it dims fast at the start and slows to a crawl near the end
  (e.g. at the halfway point in time, brightness is at 25%, not 50%). Computed in Q10
  fixed-point integer math — no FPU on this MCU, same approach as the rest of the firmware.
- **`speed` field is repurposed for this preset only:** every other preset's `speed` byte
  means "ms per animation step", which can't encode a useful multi-minute total duration in
  one byte. For preset 6 only, `speed` means **minutes** (1–255; 0 defaults to 30). This is a
  deliberate, documented exception — not a silent redefinition of the field ecosystem-wide.
- Flash usage 6328 B (26% of 24 KB app region), RAM 1060 B. No other behavioural changes
  from v1.8.0 (see v1.8.0 below for the OTA/bootloader-hosted architecture change).

### v1.8.0 — OTA-capable, bootloader-hosted — released 2026-06-23
- App relinked at `0x2000` (`app.ld`) to run under the noknok USB bootloader
  (`module-USB-bootloader`), with the top 16 B of RAM reserved as the handoff cell.
- `0xB0` ENTER_BOOTLOADER: writes the handoff magic + resets; the bootloader catches it and
  stays in USB flashing mode. Updates now flash over USB via `usb_flash.ps1` / the Pico's
  `UsbModuleFlasher` — no BOOT0 jumper needed after the bootloader's one-time flash.

### v1.6 — unique serial + rich LED control & presets — released 2026-06-18
- **Unique USB serial number.** `iSerialNumber` is now built at boot from the CH32V203's 96-bit hardware chip UID (ESIG at `0x1FFFF7E8`) as 24 hex characters, so every physical module enumerates with a **distinct serial**. This is the USB counterpart of the I2C hardware UID — it lets the Conductor tell identical modules apart and map them to roles. (Previously all modules shared the hardcoded serial `"007"`.)
- **`0x10` SET_LED — full per-LED control in one command:** `0x10 i R G B brightness durLo durHi`. `i` = LED index (`0xFF` = all); `duration` is 16-bit ms (little-endian; `0` = hold indefinitely, otherwise the LED(s) auto-off after the duration). Non-blocking.
- **`0x20` PLAY_PRESET — 5 built-in non-blocking animations** that run on the module (fire-and-forget, mirroring the buzzer's preset tunes): `1` rainbow rotate, `2` breathe, `3` theatre chase, `4` colour wipe, `5` twinkle. `0x20 preset speed R G B` — `speed` = ms per animation step (`0` = default 40 ms); `R G B` = base colour (ignored by rainbow). Any immediate command (`0x00`–`0x05`, `0x10`) cancels a running animation.
- Added a **TIM3 free-running 1 ms timebase** for durations and animation stepping.
- Flash usage 6052 B (18% of 32 KB), RAM 1056 B.

### v1.5 — adds firmware version query — released 2026-06-17
- **`0xB1` GET_VERSION command** (noknok ecosystem standard, command range 0xB0–0xBF). The module replies with 4 bytes: `[PROTOCOL_VERSION, FW_MAJOR, FW_MINOR, FW_PATCH]` = `0x01, 1, 5, 0`. Lets the host (Pico or PC) read which firmware a module is running — needed by the OTA update flow. Matches the I2C modules' `0xB1` convention.
- No other behavioural changes from v1.4.

### v1.4 — first working release — released 2026-06-04
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
