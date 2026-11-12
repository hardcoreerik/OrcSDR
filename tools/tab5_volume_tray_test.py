#!/usr/bin/env python3
"""On-device check of the shared volume tray and its touch gate, on every dashboard that shows the header.

  python tools/tab5_volume_tray_test.py --port COM17 --out evidence_dir [--names HOME,FM,...] [--key key_file]

For each dashboard it: opens it, notes the screen, band and tuned frequency, taps the header speaker, checks the tray is drawn
(cyan outline at its known place, from a screenshot), sets the volume by tapping the slider, double-taps the speaker (the
second tap must not mute), then taps OUTSIDE the tray over the dashboard's own content. After that tap the tray must be gone
and the screen, band and frequency must be exactly what they were: a tap that dismisses the tray never reaches the dashboard.
Finally a Home-button tap must work (touches are handed back to dashboards). It restores the volume it found.

Uses serial UI_TAP (synthetic taps through the same touch reader as real ones), RTL_UI STATUS and screenshots.
Needs Pillow for the screenshot checks. Flashes and reboots nothing.
"""
import argparse
import os
import re
import subprocess
import sys
import atexit
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tab5_ft8 as t  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
TRAY = (223, 72, 840, 280)          # x, y, w, h (3:1, centred below the status bar)
SPEAKER = (1165, 39)                # header speaker button
SLIDER_X0, SLIDER_W, SLIDER_Y = 223 + 176, 496, 72 + 150
HOME_BTN = (1087, 38)
DEFAULT_NAMES = ["HOME", "FM", "AM", "SHORTWAVE", "ADSB", "AIS", "ACARS", "FT8", "POCSAG", "AIRBAND", "CB", "P25"]
# Taps outside the tray, over content: Home's tuner area, scope / list areas, a mid-screen spot, a lower-left spot.
OUTSIDE_TAPS = [(640, 560), (110, 300), (1160, 450)]   # all outside the tray rectangle


def kv(line):
    return dict(re.findall(r'(\w+)=("[^"]*"|\S+)', line or ""))


class Dev:
    def __init__(self, port, key):
        self.port, self.key = port, t.find_key(key)
        self.keyfile = key                      # the path, handed to child processes (never the key itself)
        self.dev = t.Tab5(port)
        self.last = 0.0

    def reopen(self):
        try:
            self.dev.s.close()
        except Exception:
            pass
        time.sleep(1.0)
        self.dev = t.Tab5(self.port)
        self.last = 0.0

    def cmd(self, line, prefix, wait=5.0):
        if time.time() - self.last > 2.5:
            self.dev.authenticate(self.key)
        self.dev.s.reset_input_buffer()
        self.dev.send(line)
        self.last = time.time()
        out, end = [], time.time() + wait
        while time.time() < end:
            raw = self.dev.s.readline()
            if not raw:
                continue
            text = raw.decode("utf-8", "replace").rstrip()
            if prefix in text:
                out.append(text)
                end = min(end, time.time() + 0.5)
        return out

    def first(self, line, prefix, wait=5.0):
        r = self.cmd(line, prefix, wait)
        return r[0] if r else ""

    def tap(self, x, y, settle=1.0):
        for _ in range(4):                      # a tap while the previous one is still live is refused with "busy": try again
            r = self.first(f"UI_TAP {x} {y}", "UI_TAP", 4)
            if "DONE" in r:
                break
            time.sleep(0.3)
        time.sleep(settle)
        return "DONE" in r

    def double_tap(self, x, y, gap=0.22):
        """Two taps `gap` seconds apart (UI_TAP refuses a new tap for about 190 ms), sent without waiting for replies."""
        if time.time() - self.last > 2.5:
            self.dev.authenticate(self.key)
        self.dev.s.reset_input_buffer()
        self.dev.send(f"UI_TAP {x} {y}")
        time.sleep(gap)
        self.dev.send(f"UI_TAP {x} {y}")
        self.last = time.time()
        done, end = 0, time.time() + 1.5
        while time.time() < end and done < 2:
            raw = self.dev.s.readline()
            if raw and b"UI_TAP" in raw and b"DONE" in raw:
                done += 1
        return done                              # the caller requires 2: a refused ("busy") tap would test nothing

    def ensure_tray(self, want):
        """Bring the tray to open/closed using taps that are safe in either state; returns True when it got there."""
        for _ in range(4):
            cur = self.tray()
            if cur == (1 if want else 0):
                return True
            if want:
                self.tap(*SPEAKER, settle=0.5)
            elif cur:
                self.tap(110, 300, settle=0.8)
        return self.tray() == (1 if want else 0)

    def state(self):
        s = kv(self.first("RTL_UI STATUS", "RTL_UI_STATUS"))
        return {"screen": s.get("screen"), "band": s.get("band"), "frequency_hz": s.get("frequency_hz")}

    def full(self):
        return kv(self.first("RTL_UI STATUS", "RTL_UI_STATUS"))

    def volume(self):
        v = self.full().get("volume")
        return int(v) if v and v.isdigit() else None

    def muted(self):
        v = self.full().get("audio_enabled")
        return None if v is None else 1 - int(v)

    def tray(self):
        v = self.full().get("tray")
        return None if v is None else int(v)

    def shot(self, path):
        try:
            self.dev.s.close()
        except Exception:
            pass
        if os.path.exists(path):
            os.remove(path)                     # a stale screenshot from an earlier run must never pass for this capture
        cmd = [sys.executable, os.path.join(HERE, "tab5_ft8_shot.py"), "--port", self.port]
        if self.keyfile:
            cmd += ["--key", self.keyfile]
        res = subprocess.run(cmd + [path[:-4]], capture_output=True, text=True, timeout=180)
        self.reopen()
        return res.returncode == 0 and os.path.exists(path)


def tray_drawn(path):
    """True when the tray's cyan outline is present along its top edge in the screenshot."""
    from PIL import Image
    im = Image.open(path).convert("RGB")
    x0, y0, w, h = TRAY
    hits = total = 0
    for x in range(x0 + 24, x0 + w - 24):
        total += 1
        r, g, b = im.getpixel((x, y0))
        if r < 90 and g > 150 and b > 200:
            hits += 1
    return hits / max(1, total) > 0.85


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--key")
    p.add_argument("--names", default=",".join(DEFAULT_NAMES))
    a = p.parse_args()
    os.makedirs(a.out, exist_ok=True)
    d = Dev(a.port, a.key)
    results = []

    def check(name, ok, detail):
        results.append((name, ok, detail))
        print(f"[{'PASS' if ok else 'FAIL'}] {name}: {detail}", flush=True)

    d.cmd("RTL_SPLASH_GATE OFF", "RTL_SPLASH_GATE", 4)
    start_volume = d.volume()
    start_audio = d.full().get("audio_enabled")      # slider touches switch audio on: put it back as found
    restored = []

    restore_failures = []

    def restore():                                   # also runs when the test dies half way; every step runs on its own
        if restored:
            return
        steps = [("close the tray", lambda: d.ensure_tray(False)),
                 ("volume", lambda: start_volume is None or d.cmd(f"RTL_VOLUME {start_volume}", "RTL_VOLUME", 4)),
                 ("audio", lambda: start_audio not in ("0", "1") or
                  d.cmd("RTL_SOUND ON" if start_audio == "1" else "RTL_SOUND OFF", "RTL_SOUND", 4)),
                 ("splash gate", lambda: d.cmd("RTL_SPLASH_GATE ON", "RTL_SPLASH_GATE", 4))]
        for label, action in steps:
            try:
                action()
            except Exception:
                try:                                 # the serial link may be the problem: reopen once and retry
                    d.reopen()
                    action()
                except Exception as exc:
                    restore_failures.append(f"restore {label}: {exc}")
                    print("restore failed:", label, exc, flush=True)
        if not restore_failures:
            restored.append(True)

    atexit.register(restore)
    for name in [n.strip().upper() for n in a.names.split(",") if n.strip()]:
        label = name
        d.first(f"RTL_UI OPEN {name}", "RTL_UI_OPEN", 5)
        time.sleep(5)
        before = d.state()
        if before["screen"] != name.lower():
            check(label, False, f"did not open: wanted screen {name.lower()}, got {before}")
            continue
        # 1. open the tray
        d.tap(*SPEAKER, settle=1.2)
        shot_open = os.path.join(a.out, f"{name.lower()}_tray_open.png")
        opened = d.shot(shot_open) and tray_drawn(shot_open)
        check(f"{label}: tray opens below the status bar", opened, "outline found" if opened else "no tray outline in the screenshot")
        # 2. slider: middle, then high (the tray closes itself after 5 s and the screenshot is slow: make sure it is open)
        d.ensure_tray(True)
        d.tap(SLIDER_X0 + SLIDER_W // 2, SLIDER_Y, settle=0.8)
        v_mid = d.volume()
        d.ensure_tray(True)
        d.tap(SLIDER_X0 + int(SLIDER_W * 0.9), SLIDER_Y, settle=0.8)
        v_high = d.volume()
        ok = v_mid is not None and v_high is not None and 100 <= v_mid <= 156 and v_high >= 200
        check(f"{label}: slider sets the volume", ok, f"middle -> {v_mid}, 90% -> {v_high}")
        mid_state = d.state()
        check(f"{label}: slider taps did not touch the dashboard", mid_state == before, f"{before} -> {mid_state}")
        # 3. outside tap(s) dismiss the tray and nothing else
        for i, (x, y) in enumerate(OUTSIDE_TAPS):
            if not opened:
                break
            if i > 0:
                d.tap(*SPEAKER, settle=1.0)      # the previous dismiss closed it; open it again for the next one
            d.tap(x, y, settle=0.9)
            after = d.state()
            check(f"{label}: dismiss tap at ({x},{y}) did not reach the dashboard", after == before, f"{before} -> {after}")
        # 4. double tap on the speaker opens and does not mute
        muted_before = d.muted()
        d.ensure_tray(False)                       # start from a closed tray
        time.sleep(0.5)
        accepted = 0
        for _ in range(3):
            accepted = d.double_tap(*SPEAKER, gap=0.22)
            if accepted == 2:
                break
            d.ensure_tray(False)
            time.sleep(0.6)
        time.sleep(0.8)
        check(f"{label}: both taps of the double tap were accepted", accepted == 2, f"{accepted} of 2 UI_TAP_DONE")
        muted_after = d.muted()
        check(f"{label}: double tap on the speaker does not mute", muted_before is not None and muted_before == muted_after,
              f"muted {muted_before} -> {muted_after}")
        # 5. close it (outside tap) and confirm touches are handed back: Home must work
        d.tap(110, 300, settle=1.0)
        shot_closed = os.path.join(a.out, f"{name.lower()}_tray_closed.png")
        closed = d.shot(shot_closed) and not tray_drawn(shot_closed)
        check(f"{label}: tray closes", closed, "outline gone" if closed else "outline still present")
        after_close = d.state()
        check(f"{label}: state unchanged after the tray closed", after_close == before, f"{before} -> {after_close}")
        if name != "HOME":
            time.sleep(0.4)
            d.tap(*HOME_BTN, settle=2.0)
            screen = d.state()["screen"]
            check(f"{label}: Home button works afterwards", screen == "home", f"screen {screen}")
    restore()
    for problem in restore_failures:
        results.append(("restore", False, problem))
    bad = [r for r in results if not r[1]]
    print(f"\nSUMMARY {len(results) - len(bad)} passed, {len(bad)} failed")
    for n, _, detail in bad:
        print("  FAIL", n, "-", detail)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
