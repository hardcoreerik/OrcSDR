#!/usr/bin/env python3
"""Drive the P4 console UI over serial and save a screenshot of each state.

    python tools/orcui_tour.py COM8 out_dir

Uses the firmware's test hook `ORCUI_TAP <x> <y>` (panel pixels) and the screenshot service
(tools/orcui_screenshot.py). The tour opens each Home popup, the frequency keypad and the
dashboard browser, saving a numbered PNG for each, and closes each one afterwards.
"""
import os
import sys
import time

import serial

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import orcui_screenshot as shot  # noqa: E402

SCALE_X, SCALE_Y = 800 / 1280, 480 / 720


def panel(dx, dy):
    """Tab5 design coordinates (1280x720) -> panel pixels."""
    return round(dx * SCALE_X), round(dy * SCALE_Y)


def tap(port, x, y, baud=115200):
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, baud, 0.5
    s.dtr = False
    s.rts = False
    s.open()
    try:
        s.reset_input_buffer()
        s.write(f"ORCUI_TAP {x} {y}\n".encode())
        end = time.time() + 3
        while time.time() < end:
            if b"ORCUI_TAP_OK" in s.readline():
                break
    finally:
        s.close()
    time.sleep(0.9)  # the tap is held ~150 ms, then the UI redraws


# (name, taps in design coordinates executed before the screenshot, taps afterwards to restore Home)
TOUR = [
    ("home", [], []),
    ("mode-popup", [(1190, 530)], [(400, 150)]),
    ("gain-popup", [(1180, 595)], [(400, 150)]),
    ("filter-popup", [(1045, 595)], [(400, 150)]),
    ("keypad", [(400, 530)], [(440, 666)]),
    ("all-dashboards", [(149, 611)], [(1069, 37)]),
]


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    port, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    for index, (name, before, after) in enumerate(TOUR):
        for dx, dy in before:
            tap(port, *panel(dx, dy))
        path = os.path.join(out, f"{index:02d}-{name}.png")
        info = shot.capture(port, path)
        print(f"{path}: {info['w']}x{info['h']} crc {info['crc']:08x} fw {info['elf'][:12]}")
        for dx, dy in after:
            tap(port, *panel(dx, dy))


if __name__ == "__main__":
    main()
