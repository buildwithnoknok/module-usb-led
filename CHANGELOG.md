# Changelog - noknok LEDs (8x) module firmware

Newest first. One entry per version bump, written in the same commit as the bump. Format:
`## <version> - <date>` then `- DEV-xx - what changed and why. Breaking: yes/no.`
Convention: Confluence "Development Conventions" (SD space, page 120160257).

## 1.8.3 - 2026-11-19

- DEV-269 - LED output hardened like the LEDs 16x 2.2.2 (DEV-78): compare preload on, each frame starts from a stopped bit timer with a leading low slot, interrupts off during the frame. Removes a race that could shift a quickly re-sent frame by one bit (not seen on the 8x bench, seen on the 16x). New `tools/leds8_stream.py` fast-host test. Breaking: no.

## Baseline - 2026-10-02

- Changelog starts here. Current: usb_leds firmware 1.8.2 (released 2026-09-29). Earlier history: git log and the README.
