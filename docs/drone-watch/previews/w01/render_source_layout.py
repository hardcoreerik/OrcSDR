#!/usr/bin/env python3
"""Source-aligned W01 visual reference, NOT a native M5GFX framebuffer capture.

Manually translated from apps/orcsdr-tab5/ui/drone_watch_dashboard.cpp,
Git blob b222a1e0f9fe19e9dbc2f9e11fad9878e7000b21 on the existing
chatgpt/drone-watch-phasing-20261026 branch. Fonts and badge approximate native
rendering. Do not call these pixel-perfect or hardware-verified screenshots.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

OUT = Path(__file__).resolve().parent
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"

def rgb565(value):
    return ((value >> 11 & 31) * 255 // 31,
            (value >> 5 & 63) * 255 // 63,
            (value & 31) * 255 // 31)

BG, PANEL, EDGE = map(rgb565, (0x0000, 0x0841, 0x2945))
CYAN, GREEN, MUTED, AMBER = map(rgb565, (0x2e7f, 0x6fe8, 0x8c71, 0xfde0))
WHITE, GRAY = map(rgb565, (0xffff, 0xc618))

class Display:
    def __init__(self, width=1280):
        self.w = width
        self.im = Image.new("RGB", (width, 720), BG)
        self.d = ImageDraw.Draw(self.im)

    def fill(self, x, y, w, h, color):
        if w > 0 and h > 0:
            self.d.rectangle((x, y, x + w - 1, y + h - 1), fill=color)

    def rect(self, x, y, w, h, color, radius=0, fill=None):
        if w <= 0 or h <= 0:
            return
        box = (x, y, x + w - 1, y + h - 1)
        if radius:
            self.d.rounded_rectangle(box, radius=radius, outline=color, fill=fill, width=1)
        else:
            self.d.rectangle(box, outline=color, fill=fill, width=1)

    def line(self, x1, y1, x2, y2, color, width=1):
        self.d.line((x1, y1, x2, y2), fill=color, width=width)

    def text(self, value, x, y, color, scale=2, center=False):
        # The native dashboard uses M5GFX's default bitmap font.
        # DejaVuSansMono here is an illustration, NOT its exact glyph atlas.
        font = ImageFont.truetype(FONT, 8 * scale)
        left, top, right, bottom = self.d.textbbox((0, 0), value, font=font)
        text_width = right - left
        self.d.text((x - text_width / 2 if center else x, y - (top + bottom) / 2),
                    value, font=font, fill=color)

    def panel(self, x, y, w, h):
        self.rect(x, y, w, h, EDGE, 10, PANEL)

    def button(self, x, y, w, name, selected=False):
        self.rect(x, y, w, 54, GREEN if selected else CYAN, 8, PANEL)
        self.text(name, x + w / 2, y + 27, GREEN if selected else WHITE, 2, True)

    def header(self):
        if self.w >= 1000:
            # The badge is a placeholder. Firmware calls orc_badge instead.
            self.fill(20, 12, 350, 92, BG)
            self.rect(24, 14, 88, 88, GREEN, 10, PANEL)
            self.text("O", 68, 58, GREEN, 4, True)
            self.text("OrcSDR", 128, 38, WHITE, 5)
            self.text("DRONE WATCH", 128, 82, CYAN, 2)
            self.fill(866, 12, 174, 54, BG)
            self.text("75%", 910, 37, WHITE, 2, True)
            self.rect(950, 21, 68, 32, WHITE, 5)
            self.fill(1018, 30, 6, 14, WHITE)
            for i in range(4):
                self.fill(957 + 14 * i, 27, 11, 20, GREEN if i < 3 else EDGE)
            for x, w, border in ((1040, 58, CYAN), (1099, 54, GREEN),
                                 (1158, 54, MUTED), (1217, 51, GRAY)):
                self.rect(x, 8 if x == 1040 else 12, w,
                          58 if x == 1040 else 54, border, 8, PANEL)
            self.d.polygon([(1069, 17), (1049, 38), (1089, 38)], fill=GREEN)
            self.fill(1057, 35, 24, 21, GREEN)
            self.fill(1065, 43, 8, 13, PANEL)
            self.d.polygon([(1111, 32), (1118, 32), (1128, 25),
                            (1128, 53), (1118, 47), (1111, 47)], fill=GREEN)
            self.line(1135, 30, 1139, 39, GREEN)
            self.line(1139, 39, 1135, 48, GREEN)
            self.text("VIS", 1185, 39, MUTED, 2, True)
            self.d.ellipse((1231, 26, 1253, 49), outline=CYAN, width=1)
            self.d.ellipse((1239, 34, 1245, 40), outline=CYAN, width=1)
        else:
            self.text("OrcSDR  /  DRONE WATCH", 22, 44, CYAN, 3)
            self.button(self.w - 142, 18, 116, "HOME")
        self.line(20, 109, self.w - 20, 109, EDGE)
        self.text("W01   LIVE WATCH", 24, 147, CYAN, 2 if self.w < 1000 else 3)
        if self.w >= 1000:
            self.text("PASSIVE REMOTE ID  /  RECEIVE ONLY", 635, 146, MUTED)

    def footer(self):
        categories = ("WATCH", "AIRCRAFT", "RF LAB", "MEDIA", "SYSTEM")
        gap = 12
        tab_width = (self.w - 40 - gap * 4) // 5
        for i, name in enumerate(categories):
            x = 20 + i * (tab_width + gap)
            self.rect(x, 649, tab_width, 54, GREEN if i == 0 else EDGE, 8, PANEL)
            self.text(name, x + tab_width / 2, 676,
                      GREEN if i == 0 else MUTED, 2, True)

    def main_panel(self, state="no_source"):
        small = self.w < 1000
        headline, explanation, monitoring = {
            "no_source": ("SOURCE UNAVAILABLE",
                          "No verified Remote ID acquisition source is connected.", False),
            "scanning": ("NO RID OBSERVED",
                         "Receiver listening; no recent valid packet received.", True),
            "received": ("REMOTE ID RECEIVED",
                         "A qualified source reported a recent broadcast.", True)
        }[state]
        y = 187 if small else 191
        height = 198 if small else 302
        self.panel(20, y, self.w - 40 if small else 802, height)
        self.text("RECEPTION STATUS", 42, y + 38, CYAN)
        self.text(headline, 42, y + (91 if small else 112),
                  GREEN if monitoring else AMBER, 3 if small else 4)
        if small:
            self.text("Qualified capture source listening." if monitoring
                      else "No verified receiver is active.", 42, y + 148, MUTED)
        else:
            self.text(explanation, 42, y + 178, MUTED)
            self.text("A receiver's silence does not mean no aircraft nearby.",
                      42, y + 236, MUTED)

    def source_panel(self, details=False, help=False, c6_ready=False, state="no_source"):
        small = self.w < 1000
        x, y = (20, 399) if small else (840, 191)
        w, h = (self.w - 40, 163) if small else (self.w - 860, 302)
        self.panel(x, y, w, h)
        self.text("HOW IT WORKS" if help else
                  "SOURCE DETAILS" if details else "RECEIVER CAPABILITY",
                  x + 20, y + 32, CYAN)
        if help:
            self.text("REMOTE ID is broadcast data.", x + 20, y + 77, WHITE)
            self.text("No RF signal proves identity.", x + 20, y + 112, MUTED)
            if not small:
                self.text("Silence is not an all-clear.", x + 20, y + 156, MUTED)
        elif details and state in ("scanning", "received"):
            self.text("QUALIFIED SOURCE ADAPTER", x + 20, y + 79, GREEN)
            self.text("REMOTE ID RECEIVED" if state == "received" else
                      "NO RID OBSERVED", x + 20, y + 122, CYAN)
            if not small:
                self.text("Source measurement, not aircraft proof.",
                          x + 20, y + 167, MUTED)
        elif details:
            self.text("C6 LINK: HOSTED READY" if c6_ready else
                      "C6 LINK: NOT STARTED", x + 20, y + 79, MUTED)
            self.text("BLE: HCI BUILD DISABLED", x + 20, y + 122, AMBER)
            if not small:
                self.text("WIFI BEACON: UNVERIFIED", x + 20, y + 167, MUTED)
                self.text("WIFI NAN: UNVERIFIED", x + 20, y + 213, MUTED)
        else:
            self.text("REMOTE ID SOURCE", x + 20, y + 86, MUTED)
            self.text("QUALIFIED" if state in ("scanning", "received")
                      else "NOT IMPLEMENTED", x + 20, y + 127,
                      GREEN if state in ("scanning", "received") else AMBER)
            if not small:
                self.text("BLE: HCI BUILD DISABLED", x + 20, y + 179, MUTED)
                self.text("RTL V4: NO 2.4 / 5.8 GHZ", x + 20, y + 216, MUTED)

    def activity(self, details=False, help=False, state="no_source"):
        if self.w < 1000:
            bw = (self.w - 52) // 2
            self.button(20, 576, bw, "CLOSE SOURCES" if details else "SOURCES")
            self.button(32 + bw, 576, bw, "CLOSE HELP" if help else "HELP")
        else:
            self.panel(20, 512, 802, 121)
            self.text("RECENT ACTIVITY", 42, 543, CYAN)
            self.text("VALID RID MESSAGES: 1" if state == "received"
                      else "NO RECENT REMOTE ID OBSERVED" if state == "scanning"
                      else "NO OBSERVATIONS AVAILABLE",
                      42, 589, GREEN if state == "received" else MUTED)
            self.button(840, 520, 195, "CLOSE" if details else "SOURCES")
            self.button(1047, 520, self.w - 1067, "CLOSE" if help else "HELP")
            self.text("ADDITIONAL VIEWS IN DEVELOPMENT", 846, 604, MUTED)

    def render(self, details=False, help=False, c6_ready=False, state="no_source"):
        self.header()
        self.footer()
        self.main_panel(state)
        self.source_panel(details, help, c6_ready, state)
        self.activity(details, help, state)
        return self

if __name__ == "__main__":
    scenes = {
        "W01_as_built_1280_default.png": (1280, False, False, False, "no_source"),
        "W01_as_built_1280_sources.png": (1280, True, False, True, "no_source"),
        "W01_as_built_720_default.png": (720, False, False, False, "no_source"),
        "W01_as_built_720_sources.png": (720, True, False, False, "no_source"),
    }
    for file, (w, details, help, c6, state) in scenes.items():
        result = Display(w).render(details, help, c6, state)
        result.im.save(OUT / file)
        print(file, result.im.size)
    images = [Image.open(OUT / name) for name in scenes]
    sheet = Image.new("RGB", (1280, 1080), BG)
    for i, im in enumerate(images):
        if i < 2:
            sheet.paste(im.resize((640, 360), Image.Resampling.LANCZOS), (i * 640, 0))
        else:
            sheet.paste(im.resize((360, 360), Image.Resampling.LANCZOS),
                        ((i - 2) * 640 + 140, 360))
    sheet.save(OUT / "W01_source_aligned_comparison.png")
