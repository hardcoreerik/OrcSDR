#!/usr/bin/env python3
"""Open the Digital Voice dashboard on the Tab5 over serial and capture the real framebuffer.
Usage: dv_open_capture.py --port COM17 --key F:/Ai/OrcSDR/.orclink/ui-doc.key [--taps x,y ...] --slug dv0"""
import argparse, re, sys, time
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from help_media import Tab5

ap = argparse.ArgumentParser()
ap.add_argument("--port", default="COM17")
ap.add_argument("--key", type=Path, required=True)
ap.add_argument("--slug", default="dv0")
ap.add_argument("--screen", default="digital_voice.scan")
ap.add_argument("--taps", nargs="*", default=[])
ap.add_argument("--out", type=Path, default=ROOT / "artifacts/digital-voice-integration/captures")
a = ap.parse_args()
a.out.mkdir(parents=True, exist_ok=True)
c = Tab5(a.port, a.key)
try:
    c.authenticate(create_key=False)
    c.send(f"UI_DOC_SHOW {a.screen} live"); print(c.wait(("UI_DOC_SHOW_DONE", "UI_DOC_ERROR"), 25))
    time.sleep(3)
    for t in a.taps:
        x, y = t.split(","); c.authenticate(create_key=False)
        c.send(f"RTL_UI TAP {x} {y}"); print(c.wait(("RTL_UI_TAP",), 10)); time.sleep(1.5)
    c.authenticate(create_key=False)
    c.send(f"UI_CAPTURE {a.slug}")
    r = c.wait(("UI_CAPTURE_DONE", "UI_CAPTURE_ERROR"), 45); print(r)
    if r.startswith("UI_CAPTURE_DONE"):
        remote = re.search(r'path="([^"]+)"', r).group(1)
        c.get_file(remote, a.out / f"{a.slug}.bmp"); print("saved", a.out / f"{a.slug}.bmp")
finally:
    c.close()
