#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# stress_leds8.py - USB receive-path stress test for the noknok LEDs (8x RGB), run from
# a Linux host (the Pi4 bench) with the module plugged in directly.
#
# Blasts N back-to-back 25-byte 0x04 frames with NO pauses (far more than one 64-byte
# USB packet per main-loop pass). Firmware <= 1.8.1 loses bytes under this load
# (ch32fun usbd.c discards the oldest bytes of its 64-byte buffer), the parser desyncs
# and stray 0x00 bytes execute as ALL OFF - the ring typically ends dark or scrambled.
# Firmware >= 1.8.2 flow-controls the host and must show the exact final frame.
# (29 Sep 2026: the identical receive code was reproduced failing on the LEDs 16x from a
# Pi4's native USB. On this 8x, 1.8.1 did NOT fail through the Pico's slower PIO-USB host
# at this load; 1.8.2 passes both from the Pi4 directly and through the Pico.)
#
# A version reply alone does NOT prove anything (the parser resyncs by itself) -
# check the LEDs against the printed expected frame.
import glob, sys, time, serial

N = int(sys.argv[1]) if len(sys.argv) > 1 else 300
port = [p for p in glob.glob("/dev/serial/by-id/*") if "noknok_LEDs" in p][0]
s = serial.Serial(port, 115200, timeout=1.0)
s.write(bytes([0x03, 60])); s.flush(); time.sleep(0.05)       # ~25 % brightness

cols = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 160, 0)]   # red green blue yellow
names = ["red", "green", "blue", "yellow"]
blob = bytearray()
for k in range(N):
    frame = bytearray([0x04])
    for i in range(8):
        frame += bytes(cols[(i // 2 + k) % 4])                  # 2-LED colour blocks, shifted per frame
    blob += frame
t0 = time.time(); s.write(blob); s.flush(); dt = time.time() - t0
time.sleep(0.2)
s.reset_input_buffer(); s.write(bytes([0xB1])); s.flush()
v = list(s.read(4))
last = N - 1
print("sent %d frames (%d bytes) in %.2f s" % (N, len(blob), dt))
print("version:", v)
print("EXPECTED final frame: " + ", ".join(
    "LEDs %d-%d %s" % (2 * b, 2 * b + 1, names[(b + last) % 4]) for b in range(4)))
