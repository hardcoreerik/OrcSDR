#!/usr/bin/env python3
"""Render Dead Air screen mockups with the firmware's own palette, geometry and font.

Everything here is copied from the firmware, not guessed:
  * colours and layout constants: ui/dead_air_dashboard.cpp, ui/dashboard_audio_control.cpp, ui/freq_keypad.cpp
  * the font: the M5GFX built-in font0 (the Adafruit 5x7 "glcd" table in glcdfont.h, a 6x8 cell), scaled by
    setTextSize exactly as the firmware does, so a size-2 character is 12x16 px
  * the screen: 1280x720, RGB565 colours converted to 8 bits per channel

Stand-ins (marked in the code): the OrcSDR logo badge and the orb are sprites in the firmware; they are drawn as
plain shapes here. Event values are illustrative.

Run from the repository root; output is deterministic (same script, same PNG bytes):
    python tools/dead_air_mockup.py [output_dir]
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
FONT_H = ROOT / "apps/orcsdr-tab5/managed_components/m5stack__m5gfx/src/lgfx/Fonts/glcdfont.h"


def rgb(c: int) -> tuple[int, int, int]:
    r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
    return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)


# dead_air_dashboard.cpp
BG, PANEL, EDGE = 0x0000, 0x0841, 0x2945
GREEN, CYAN, VIOLET = 0x07E0, 0x2E7F, 0xA15F
MUTED, YELLOW, RED = 0x8C71, 0xFFE0, 0xF800
ORANGE, WHITE, NAVY, LIGHTGREY = 0xFD20, 0xFFFF, 0x000F, 0xD69A
# dashboard_audio_control.cpp (the shared header uses its own, slightly different green)
HDR_GREEN = 0x6FE8
# freq_keypad.cpp
KP_CYAN, KP_GREEN = 0x05FF, 0x6FE0


def load_font() -> list[bytes]:
    text = FONT_H.read_text()
    body = text[text.index("font[]"):]
    body = body[body.index("{") + 1:body.index("};")]
    values = [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]{2}", body)]
    return [bytes(values[i * 5:i * 5 + 5]) for i in range(len(values) // 5)]


GLYPHS = load_font()


class Screen:
    def __init__(self) -> None:
        self.img = Image.new("RGB", (1280, 720), rgb(BG))
        self.d = ImageDraw.Draw(self.img)

    def rect(self, x, y, w, h, c):
        self.d.rectangle([x, y, x + w - 1, y + h - 1], fill=rgb(c))

    def hline(self, x, y, w, c):
        self.rect(x, y, w, 1, c)

    def vline(self, x, y, h, c):
        self.rect(x, y, 1, h, c)

    def line(self, x0, y0, x1, y1, c):
        self.d.line([x0, y0, x1, y1], fill=rgb(c))

    def round_fill(self, x, y, w, h, r, c):
        self.d.rounded_rectangle([x, y, x + w - 1, y + h - 1], radius=r, fill=rgb(c))

    def round_edge(self, x, y, w, h, r, c):
        self.d.rounded_rectangle([x, y, x + w - 1, y + h - 1], radius=r, outline=rgb(c))

    def circle(self, cx, cy, r, c, fill=False):
        box = [cx - r, cy - r, cx + r, cy + r]
        self.d.ellipse(box, fill=rgb(c) if fill else None, outline=rgb(c))

    def tri(self, pts, c):
        self.d.polygon(pts, fill=rgb(c))

    def text(self, s, x, y, c, size=2, datum="ml"):
        """Draw like M5GFX font0: 6x8 cell scaled by `size`; datum m=middle, t=top; l/c = left/centre."""
        w, h = 6 * size * len(s), 8 * size
        left = x - w // 2 if datum[1] == "c" else x
        top = y - h // 2 if datum[0] == "m" else y
        for i, ch in enumerate(s):
            glyph = GLYPHS[ord(ch)] if ord(ch) < len(GLYPHS) else GLYPHS[63]
            for col in range(5):
                for row in range(8):
                    if glyph[col] >> row & 1:
                        px, py = left + (i * 6 + col) * size, top + row * size
                        self.d.rectangle([px, py, px + size - 1, py + size - 1], fill=rgb(c))


# ---- shared pieces, as in dead_air_dashboard.cpp -------------------------------------------------------------------
def box(s, x, y, w, h, border=EDGE):
    s.round_fill(x, y, w, h, 8, PANEL)
    s.round_edge(x, y, w, h, 8, border)


def label(s, v, x, y, c=WHITE, size=2):
    s.text(v, x, y, c, size, "ml")


def heading(s, v, x, y):
    label(s, v, x, y, CYAN, 2)


def button(s, v, x, y, w, accent=CYAN, enabled=True):
    box(s, x, y, w, 52, accent if enabled else EDGE)
    label(s, v, x + 16, y + 26, WHITE if enabled else MUTED)


def reading(s, name, value, x, y):
    label(s, name, x, y, MUTED)
    label(s, value, x + 230, y, WHITE)


def header(s, battery=87, orb=None):
    # audio_header::draw_brand / draw_battery / buttons
    s.round_edge(24, 14, 88, 88, 10, HDR_GREEN)  # STAND-IN for the OrcSDR logo badge sprite
    s.text("O", 24 + 44, 14 + 44, HDR_GREEN, 4, "mc")
    s.text("OrcSDR", 128, 38, WHITE, 5, "ml")
    s.text("DEAD AIR", 128, 82, CYAN, 3, "ml")
    s.text(f"{battery}%", 910, 37, WHITE, 2, "mc")
    s.round_edge(950, 21, 68, 32, 5, WHITE)
    s.rect(950 + 68, 21 + 9, 6, 14, WHITE)
    for i in range(4):
        s.rect(950 + 7 + i * 14, 21 + 6, 11, 20, HDR_GREEN if i < (battery + 24) // 25 else EDGE)
    # home
    s.round_fill(1040, 8, 58, 58, 8, PANEL)
    s.round_edge(1040, 8, 58, 58, 8, CYAN)
    cx = 1040 + 29
    s.tri([(cx, 17), (1040 + 9, 38), (1040 + 58 - 9, 38)], HDR_GREEN)
    s.rect(1040 + 17, 35, 24, 21, HDR_GREEN)
    s.rect(cx - 4, 43, 8, 13, PANEL)
    # mute (sound on)
    s.round_fill(1099, 12, 54, 54, 8, PANEL)
    s.round_edge(1099, 12, 54, 54, 8, HDR_GREEN)
    sx, sy = 1099 + 13, 12 + 27
    s.tri([(sx - 13, sy - 7), (sx - 5, sy - 7), (sx - 5, sy + 7)], HDR_GREEN)
    s.tri([(sx - 5, sy - 7), (sx + 5, sy - 14), (sx + 5, sy + 14)], HDR_GREEN)
    # VIS
    s.round_fill(1158, 12, 54, 54, 8, PANEL)
    s.round_edge(1158, 12, 54, 54, 8, MUTED)
    s.text("VIS", 1158 + 27, 12 + 27, MUTED, 2, "mc")
    # settings
    s.round_fill(1217, 12, 51, 54, 8, PANEL)
    s.round_edge(1217, 12, 51, 54, 8, LIGHTGREY)
    cx, cy = 1217 + 25, 12 + 27
    s.circle(cx, cy, 13, CYAN)
    s.circle(cx, cy, 5, CYAN)
    for a, b, c2, d2 in ((-21, 0, -13, 0), (13, 0, 21, 0), (0, -21, 0, -13), (0, 13, 0, 21)):
        s.line(cx + a, cy + b, cx + c2, cy + d2, CYAN)
    # (i) button
    s.circle(396, 39, 21, PANEL, True)
    s.circle(396, 39, 21, CYAN)
    s.circle(396, 39, 20, CYAN)
    s.text("i", 396, 39, CYAN, 3, "mc")
    if orb:  # the header beacon (52 px sprite in the firmware); STAND-IN disc
        s.circle(800 + 26, 13 + 26, 24, orb, True)


def nav(s, active=0):
    tabs = ["Overview", "Spectrum", "Audio", "Environment", "Events", "Review"]
    for i, name in enumerate(tabs):
        x = 20 + i * (198 + 12)
        box(s, x, 649, 198, 52, GREEN if i == active else EDGE)
        label(s, name, x + 17, 649 + 26, GREEN if i == active else WHITE)


def overlay_frame(s, title, close="CLOSE"):
    box(s, 20, 120, 1240, 511)
    heading(s, title, 40, 144)
    button(s, close, 1070, 128, 170, CYAN)


def orb_standin(s, x, y, size, color):
    """STAND-IN for the Fx orb sprite: a core disc and two rings, flat colour."""
    cx, cy, r = x + size // 2, y + size // 2, size // 2
    s.circle(cx, cy, r - 2, color)
    s.circle(cx, cy, r - 3, color)
    s.circle(cx, cy, r * 6 // 10, PANEL, True)
    s.circle(cx, cy, r * 6 // 10, color)
    s.circle(cx, cy, r * 3 // 10, color, True)


# ---- screens ----------------------------------------------------------------------------------------------------
def overview_base(s, with_tune):
    box(s, 20, 120, 612, 252)
    heading(s, "INVESTIGATION", 40, 143)
    label(s, "READY", 40, 190, GREEN, 3)
    label(s, "ELAPSED 07:41:12", 40, 240)
    label(s, "908.570 MHz  LSB", 40, 291, CYAN, 3)
    label(s, "EVENT LOG: SAVING TO SD", 40, 343, GREEN, 2)
    orb_standin(s, 500, 138, 120, GREEN)
    button(s, "SESSION", 498, 305, 122, CYAN)
    if with_tune:
        button(s, "TUNE", 366, 305, 122, CYAN)
    box(s, 646, 120, 614, 252)
    heading(s, "LIVE OBSERVATIONS", 668, 143)
    label(s, "RF 60 s", 920, 143, MUTED)
    label(s, "BASE", 1030, 143, GREEN)
    label(s, "LIMIT", 1100, 143, YELLOW)
    reading(s, "RF (dBFS)", "-28.3", 670, 178)
    reading(s, "AUDIO (dBFS)", "-15.4", 670, 210)
    s.rect(670, 236, 566, 88, 0x0000)
    s.d.rectangle([670, 236, 670 + 565, 236 + 87], outline=rgb(EDGE))
    label(s, "Sensitivity", 670, 350, MUTED)
    label(s, "STANDARD", 935, 350, GREEN)
    box(s, 20, 390, 612, 241)
    heading(s, "LATEST EVENTS", 40, 413)
    rows = [("03:12:44  RF RISE  +9.2 dB  908.570 MHz", GREEN),
            ("01:30:07  AUDIO   +8.1 dB", WHITE),
            ("00:48:51  MARK    you", WHITE)]
    for i, (t, c) in enumerate(rows):
        label(s, t, 40, 462 + i * 38, c, 2)
    box(s, 646, 390, 614, 241)
    heading(s, "ENV III / OPTIONAL", 668, 413)
    label(s, "MODULE DETECTED", 668, 464, GREEN, 2)
    reading(s, "TEMP (C)", "22.9", 668, 505)
    reading(s, "RH (%)", "56.2", 668, 550)
    reading(s, "PRESSURE (hPa)", "1001.9", 668, 594)


def screen_overview_tune() -> Image.Image:
    s = Screen()
    header(s)
    overview_base(s, with_tune=True)
    nav(s, 0)
    return s.img


def screen_tune_popup() -> Image.Image:
    s = Screen()
    s.rect(0, 114, 1280, 720 - 114, BG)  # freq_keypad::draw clears from the header down
    header(s)
    # freq_keypad.cpp
    s.round_fill(220, 140, 840, 572, 16, PANEL)
    s.round_edge(220, 140, 840, 572, 16, KP_CYAN)
    s.text("ENTER FREQUENCY", 220 + 420, 166, KP_CYAN, 3, "mc")
    s.round_fill(250, 192, 780, 70, 10, NAVY)
    s.text("162.550 MHz", 250 + 390, 192 + 35, WHITE, 5, "mc")
    keys = ["1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0", "<"]
    for i, k in enumerate(keys):
        x, y = 250 + (i % 3) * 265, 276 + (i // 3) * 88
        s.round_fill(x, y, 250, 78, 10, EDGE)
        s.round_edge(x, y, 250, 78, 10, KP_CYAN)
        s.text(k, x + 125, y + 39, WHITE, 5, "mc")
    s.round_fill(250, 630, 380, 72, 10, PANEL)
    s.round_edge(250, 630, 380, 72, 10, RED)
    s.text("CANCEL", 250 + 190, 630 + 36, RED, 4, "mc")
    s.round_fill(650, 630, 380, 72, 10, KP_GREEN)
    s.round_edge(650, 630, 380, 72, 10, KP_GREEN)
    s.text("TUNE", 650 + 190, 630 + 36, BG, 4, "mc")
    return s.img


def screen_detected() -> Image.Image:
    s = Screen()
    header(s, orb=RED)
    overlay_frame(s, "DETECTED", close="DISMISS")
    orb_standin(s, 50, 180, 120, RED)
    label(s, "POLTERGEIST-LIKE BURST", 200, 205, WHITE, 5)
    label(s, "03:12:44   MODERATE", 200, 262, YELLOW, 3)
    label(s, "6 RF impulses in 2.4 s, audio +11 dB at +0.4 s", 200, 312, MUTED, 2)
    tiles = [("RF", ["+9.2 dB over baseline", "120 ms impulse"], CYAN),
             ("SESSION VS CONTROL", ["3.1 per min  vs  0.4 per min", "beats the control run"], GREEN),
             ("RULED OUT", ["retune: no   sweep: no", "sensor stale: no"], CYAN)]
    for i, (title, lines, edge) in enumerate(tiles):
        x = 40 + i * 400
        box(s, x, 350, 380, 130, edge)
        label(s, title, x + 16, 374, MUTED, 2)
        label(s, lines[0], x + 16, 414, WHITE, 2)
        label(s, lines[1], x + 16, 446, MUTED, 2)
    label(s, "ENV III  22.9 C   56.2 %   1001.9 hPa   steady", 40, 512, WHITE, 2)
    label(s, "INTERPRETIVE LABEL. NOT EVIDENCE OF A PARANORMAL CAUSE.", 40, 546, MUTED, 2)
    button(s, "VIEW EVENT", 40, 566, 260, CYAN)
    button(s, "SCRUB", 320, 566, 200, CYAN)
    button(s, "MARK", 540, 566, 200, VIOLET)
    nav(s, 0)
    return s.img


def screen_night_report() -> Image.Image:
    s = Screen()
    header(s)
    overlay_frame(s, "NIGHT REPORT  /  22:14 TO 07:02")
    stats = [("EVENTS", "14"), ("STRONGEST", "COINCIDENT 03:12"), ("BUSIEST HOUR", "03:00"), ("VS EMPTY CONTROL", "1.6 vs 0.3 /h")]
    for i, (k, v) in enumerate(stats):
        x = 40 + i * 300
        box(s, x, 190, 285, 84)
        label(s, k, x + 14, 212, MUTED, 2)
        label(s, v, x + 14, 248, WHITE if i != 1 else YELLOW, 2 if len(v) > 8 else 3)
    label(s, "EVENTS PER HOUR", 40, 290, MUTED, 2)
    hours = ["23", "00", "01", "02", "03", "04", "05", "06", "07"]
    counts = [1, 0, 2, 4, 14, 8, 2, 0, 1]  # illustrative
    peak = max(counts)
    for i, (h, c) in enumerate(zip(hours, counts)):
        x = 40 + i * 130
        bh = 0 if c == 0 else max(4, 110 * c // peak)
        if bh:
            s.rect(x, 424 - bh, 100, bh, YELLOW if c == peak else GREEN)
        s.hline(x, 424, 100, EDGE)
        label(s, h, x + 50 - 12, 442, MUTED, 2)
    rows = [("03:12", "POLTERGEIST-LIKE BURST", "MODERATE", YELLOW),
            ("03:47", "COINCIDENT: RF + TEMP -1.2 C", "WEAK", WHITE),
            ("01:30", "ANOMALY: AUDIO +8 dB", "WEAK", WHITE)]
    for i, (t, name, tier, c) in enumerate(rows):
        y = 478 + i * 38
        s.hline(40, y - 12, 1200, EDGE)
        label(s, t, 40, y + 6, MUTED, 2)
        label(s, name, 150, y + 6, WHITE, 2)
        label(s, tier, 1060, y + 6, c, 2)
    nav(s, 5)
    return s.img


def main() -> None:
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "docs/dead-air/mockups"
    out.mkdir(parents=True, exist_ok=True)
    for name, fn in (("overview_tune", screen_overview_tune), ("tune_popup", screen_tune_popup),
                     ("detected", screen_detected), ("night_report", screen_night_report)):
        fn().save(out / f"{name}.png", optimize=False)
        print(out / f"{name}.png")


if __name__ == "__main__":
    main()
