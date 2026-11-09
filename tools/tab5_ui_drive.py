#!/usr/bin/env python3
"""Drive the Tab5 UI hands-off over USB serial: tap through the dashboards, read status, take screenshots, detect crashes.

  python tools/tab5_ui_drive.py --port COM17 list
  python tools/tab5_ui_drive.py --port COM17 sweep --out DIR [--names fm,am,...] [--dwell 8] [--source remote|usb|keep]
  python tools/tab5_ui_drive.py --port COM17 tap 142 243

Uses the firmware's authenticated `UI_TAP x y` (a touch through the normal touch path) and `FT8 SNAP` screenshots, so nothing here changes
what the screens do. A crash is detected when `RTL_HEALTH` uptime goes backwards; the tool then waits for the Tab5 to boot and goes on.
`sweep` writes DIR/<name>.png, DIR/results.jsonl and DIR/headers.png (a contact sheet of the header strips). The boot splash gate is set
OFF for the run and put back at the end (a crash with the gate on would wait for a button nobody presses).
Coordinates are for the 1280x720 panel and the layout of 2026-11-08 (Home, header Home button, ALL DASHBOARDS grid, 3 x 4 tiles per page).
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tab5_ft8 as t  # noqa: E402

HOME_BTN = (1068, 38)
ALL_DASH = (150, 610)
NEXT = (738, 671)
COL = (225, 635, 1045)
ROW = (155, 291, 427, 563)
# name -> (page, column, row)
TILES = {
    "fm": (1, 0, 0), "p25": (1, 1, 0), "adsb": (1, 2, 0),
    "shortwave": (1, 0, 1), "am": (1, 1, 1), "weather": (1, 2, 1),
    "cb": (1, 0, 2), "lora": (1, 1, 2), "pocsag": (1, 2, 2),
    "ft8": (1, 0, 3), "acars": (1, 1, 3), "airband": (1, 2, 3),
    "marine": (2, 0, 0), "satellite": (2, 1, 0), "sstv": (2, 2, 0),
    "deadair": (2, 0, 1), "rflab": (2, 1, 1), "wifi": (2, 2, 1), "settings": (2, 0, 2),
}
EXPECTED_SCREEN = {  # RTL_SCREEN_STATUS active= value that a successful open should show (None = not checked)
    "fm": "fm", "p25": "p25", "adsb": "adsb", "shortwave": "shortwave", "am": "am", "cb": "cb", "lora": "lora",
    "pocsag": "pocsag", "ft8": "ft8", "acars": "acars", "airband": "airband", "sstv": "sstv", "deadair": "dead_air",
    "rflab": "rf_lab", "wifi": "wifi_analysis", "settings": "settings", "weather": None, "marine": None, "satellite": None,
}


class Drive:
    def __init__(self, port):
        self.port = port
        self.dev = t.Tab5(port)
        self.key = t.find_key(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "..", ".orclink", "ui-doc.key")) \
            if False else t.find_key(r"F:\Ai\OrcSDR\.orclink\ui-doc.key")
        self.last = 0.0
        self.shot_py = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tab5_ft8_shot.py")

    def close(self):
        try: self.dev.s.close()
        except Exception: pass

    def reopen(self):
        self.close(); time.sleep(1.0); self.dev = t.Tab5(self.port); self.last = 0.0

    def cmd(self, line, prefixes=(), wait=4.0):
        """Send one command (re-authenticating if the session may have lapsed) and return matching lines seen within `wait` seconds."""
        if time.time() - self.last > 3.0:
            self.dev.authenticate(self.key)
        self.dev.send(line); self.last = time.time()
        out, end = [], time.time() + wait
        while time.time() < end:
            raw = self.dev.s.readline()
            if not raw: continue
            text = raw.decode("utf-8", "replace").rstrip()
            if any(p in text for p in prefixes):
                out.append(text)
                if len(out) >= 1 and not prefixes[0].endswith("*"): break
        return out

    def tap(self, x, y, settle=2.5):
        r = self.cmd(f"UI_TAP {x} {y}", ("UI_TAP_DONE", "UI_TAP_ERROR"), 4)
        time.sleep(settle); self.last = 0.0
        return bool(r) and "DONE" in r[0]

    def kv(self, line, prefix, wait=4.0):
        r = self.cmd(line, (prefix,), wait)
        return dict(re.findall(r'(\w+)=("[^"]*"|\S+)', r[0])) if r else {}

    def health(self):
        h = self.kv("RTL_HEALTH", "RTL_HEALTH_STATUS")
        return int(h.get("uptime_ms", -1)), int(h.get("reset_reason", -1))

    def screen(self):
        return self.kv("RTL_SCREEN_STATUS", "RTL_SCREEN_STATUS").get("active")

    def tcp(self):
        return self.kv("RTL_TCP STATUS", "RTL_TCP mode")

    def shot(self, path):
        self.close()
        r = subprocess.run([sys.executable, self.shot_py, "--port", self.port, path[:-4]], capture_output=True, text=True, timeout=120)
        self.reopen()
        return os.path.exists(path)

    def wait_boot(self, secs=60):
        end = time.time() + secs
        while time.time() < end:
            try:
                self.reopen(); up, _ = self.health()
                if up >= 0: return True
            except Exception: pass
            time.sleep(3)
        return False

    def go_home(self):
        self.tap(*HOME_BTN, settle=2.0)

    def open_dashboard(self, name):
        page, c, r = TILES[name]
        self.go_home()
        self.tap(*ALL_DASH, settle=2.0)
        if page == 2: self.tap(*NEXT, settle=1.5)
        return self.tap(COL[c], ROW[r], settle=0.5)


def sweep(a):
    os.makedirs(a.out, exist_ok=True)
    d = Drive(a.port)
    if a.source in ("remote", "usb"):
        d.cmd(f"RTL_TCP SOURCE {a.source}", ("RTL_TCP SOURCE",), 4)
    d.cmd("RTL_SPLASH_GATE OFF", ("RTL_SPLASH_GATE",), 4)
    names = a.names.split(",") if a.names else list(TILES)
    results = []
    for n in names:
        rec = {"name": n}
        up0, _ = d.health()
        ok = d.open_dashboard(n)
        time.sleep(a.dwell)
        up1, reset = d.health()
        if up1 < up0 or up1 < 0:
            rec.update(result="CRASH", uptime_before=up0, uptime_after=up1, reset_reason=reset)
            d.wait_boot(90); time.sleep(5)
        else:
            rec.update(tapped=ok, screen=d.screen(), tcp_state=d.tcp().get("state"), link=d.tcp().get("link"),
                       mode=d.tcp().get("mode"), uptime_ms=up1)
            want = EXPECTED_SCREEN.get(n)
            rec["result"] = "OK" if (want is None or rec["screen"] == want) else f"WRONG_SCREEN(want {want})"
            rec["screenshot"] = os.path.join(a.out, n + ".png") if d.shot(os.path.join(a.out, n + ".png")) else None
        print(json.dumps(rec), flush=True)
        results.append(rec)
        with open(os.path.join(a.out, "results.jsonl"), "a") as f: f.write(json.dumps(rec) + "\n")
    d.go_home()
    d.cmd("RTL_SPLASH_GATE ON", ("RTL_SPLASH_GATE",), 4)
    try:
        from PIL import Image
        shots = [r["screenshot"] for r in results if r.get("screenshot")]
        if shots:
            strip = Image.new("RGB", (1280, 112 * len(shots)))
            for i, s in enumerate(shots): strip.paste(Image.open(s).convert("RGB").crop((0, 0, 1280, 112)), (0, 112 * i))
            strip.save(os.path.join(a.out, "headers.png"))
    except Exception as e:
        print("contact sheet skipped:", e)
    bad = [r["name"] for r in results if r["result"] != "OK"]
    print(json.dumps({"summary": True, "dashboards": len(results), "not_ok": bad}))
    return 1 if bad else 0


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port", default="COM17")
    sub = p.add_subparsers(dest="what", required=True)
    sub.add_parser("list")
    tp = sub.add_parser("tap"); tp.add_argument("x", type=int); tp.add_argument("y", type=int)
    sw = sub.add_parser("sweep"); sw.add_argument("--out", required=True); sw.add_argument("--names")
    sw.add_argument("--dwell", type=float, default=8.0); sw.add_argument("--source", default="keep", choices=("remote", "usb", "keep"))
    a = p.parse_args()
    if a.what == "list":
        print("\n".join(f"{n}: page {v[0]} col {v[1]} row {v[2]}" for n, v in TILES.items())); return 0
    if a.what == "tap":
        d = Drive(a.port); print("ok" if d.tap(a.x, a.y) else "failed"); return 0
    return sweep(a)


if __name__ == "__main__":
    sys.exit(main())
