#!/usr/bin/env python3
"""Open a Digital Voice view in doc mode, start an on-device SD replay of a real capture, wait, then capture the framebuffer.
usage: dv_replay_capture.py <view> <sd_path> <channel_hz> <slug> [wait_s]"""
import re, sys, time
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from help_media import Tab5
view, sd, hz, slug = sys.argv[1:5]; wait = float(sys.argv[5]) if len(sys.argv) > 5 else 40
c = Tab5("COM17", Path("F:/Ai/OrcSDR/.orclink/ui-doc.key"))
try:
    c.authenticate(create_key=False)
    c.send(f"UI_DOC_SHOW digital_voice.{view} live"); print(c.wait(("UI_DOC_SHOW_DONE", "UI_DOC_ERROR"), 25)); time.sleep(3)
    c.authenticate(create_key=False)
    c.send("RTL_DV MASK 1"); print(c.wait(("RTL_DV_MASK",), 8))
    c.send(f"RTL_DV REPLAY {sd} {hz}"); print(c.wait(("RTL_DV_REPLAY",), 10))
    time.sleep(wait)
    c.authenticate(create_key=False)
    for v in ("signal" if view != "signal" else "setup", view):
        c.send(f"UI_DOC_SHOW digital_voice.{v} live"); print(c.wait(("UI_DOC_SHOW_DONE", "UI_DOC_ERROR"), 25)); time.sleep(2.5)
    import os
    for t in [q for q in os.environ.get("DV_TAPS", "").split(";") if q]:
        c.authenticate(create_key=False); c.send(f"RTL_UI TAP {t.replace(',', ' ')}"); print(c.wait(("RTL_UI_TAP",), 10)); time.sleep(1)
        c.send(f"UI_DOC_SHOW digital_voice.{view} live"); print(c.wait(("UI_DOC_SHOW_DONE", "UI_DOC_ERROR"), 25)); time.sleep(2.5)
    c.authenticate(create_key=False)
    c.send("RTL_DV STATUS"); print(c.wait(("RTL_DV_UI",), 8)[:160])
    c.send(f"UI_CAPTURE {slug}"); r = c.wait(("UI_CAPTURE_DONE", "UI_CAPTURE_ERROR"), 45); print(r[:120])
    if r.startswith("UI_CAPTURE_DONE"):
        remote = re.search(r'path="([^"]+)"', r).group(1)
        out = Path(__file__).parent / "captures" / f"{slug}.bmp"; c.get_file(remote, out); print("saved", out)
finally:
    try: run(c, 'UI_DOC_EXIT', 3.0) if 'run' in globals() else (c.send('UI_DOC_EXIT'), c.wait(('UI_DOC_EXIT',), 5))   # documentation mode blocks touch until it is exited
    except Exception: pass
    c.close()
