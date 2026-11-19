# Changelog - noknok LEDs (8x) module firmware

Newest first. One entry per version bump, written in the same commit as the bump. Format:
`## <version> - <date>` then `- DEV-xx - what changed and why. Breaking: yes/no.`
Convention: Confluence "Development Conventions" (SD space, page 120160257).

## Full image - 2026-11-19

- DEV-197 - new `firmware/bin/usb_leds_full.bin`: the full flash image (USB bootloader 1.1.0 + usb_leds firmware 1.8.3 + valid-app marker) for a first flash or recovery over SWD, recorded in `firmware/full.json`. Built from released parts by the new `tools/mk_full_image.py`; `--check` fails when the full image no longer matches `firmware/index.json`, which still points at the app-only OTA image. README: how to flash it, and why the app image must never go to `0x08000000`. No firmware change. Breaking: no.

## 1.8.3 - 2026-11-19

- DEV-269 - LED output hardened like the LEDs 16x 2.2.2 (DEV-78): compare preload on, each frame starts from a stopped bit timer with a leading low slot, interrupts off during the frame. Removes a race that could shift a quickly re-sent frame by one bit (not seen on the 8x bench, seen on the 16x). New `tools/leds8_stream.py` fast-host test. Breaking: no.

## Baseline - 2026-10-02

- Changelog starts here. Current: usb_leds firmware 1.8.2 (released 2026-09-29). Earlier history: git log and the README.
