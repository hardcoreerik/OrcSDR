#!/usr/bin/env python3
"""JS8 soak: run the Tab5 JS8 Normal receiver headless on one band for a fixed time, log every decode stage, and sample the PSK Reporter
public JS8 spots in parallel as ground truth for what is on the air.

  python tools/js8-soak.py --port COM17 --band 40m --minutes 60 --out F:/AI/OrcSDR-TEMP/js8-soak/run1

Receive only. It sends JS8 BAND / JS8 START / JS8 STATS / JS8 STOP over the USB serial console (PAIR/AUTH as tools/tab5_ft8.py does) and
reads ORC_JS8_* lines. It makes ONE PSK Reporter query at start, one every 10 minutes and one at the end (the service blocks rapid
repeated queries); nothing about the device or its location is sent. Analyse the result with tools/js8-soak-analyze.py.
"""
import argparse
import os
import sys
import time
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tab5_ft8  # noqa: E402

PSK_URL = "https://retrieve.pskreporter.info/query?mode=JS8&flowStartSeconds=-1800&appcontact=hardcoreerik@gmail.com"


def fetch_psk(out_dir, tag):
    path = os.path.join(out_dir, "psk_%s_%d.xml" % (tag, int(time.time())))
    try:
        req = urllib.request.Request(PSK_URL, headers={"User-Agent": "OrcSDR-js8-soak/0.1 (hardcoreerik@gmail.com)"})
        with urllib.request.urlopen(req, timeout=60) as r:
            data = r.read()
        with open(path, "wb") as f:
            f.write(data)
        return path, len(data)
    except Exception as e:  # keep the soak running if the service is unreachable
        return None, str(e)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--band", default="40m")
    ap.add_argument("--minutes", type=float, default=60)
    ap.add_argument("--out", required=True)
    ap.add_argument("--key")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    log = open(os.path.join(a.out, "device.log"), "w", encoding="utf-8")

    def note(msg):
        line = "%.3f HOST %s" % (time.time(), msg)
        log.write(line + "\n")
        log.flush()
        print(line, flush=True)

    dev = tab5_ft8.Tab5(a.port)
    dev.authenticate(tab5_ft8.find_key(a.key))
    note("authenticated")
    for cmd in ("JS8 START", "JS8 BAND " + a.band, "JS8 STATUS"):
        for line in dev.command(cmd, timeout=10):
            log.write("%.3f DEV %s\n" % (time.time(), line))
    path, info = fetch_psk(a.out, "start")
    note("psk start %s %s" % (path, info))

    end = time.time() + a.minutes * 60
    next_stats = time.time() + 60
    next_psk = time.time() + 600
    try:
        while time.time() < end:
            raw = dev.s.readline()
            now = time.time()
            if raw:
                t = raw.decode("utf-8", "replace").rstrip()
                if "AUTH_OK " in t:
                    continue
                if "ORC_JS8" in t or "ORC_FT8_RT" in t or "ORC_FT8_LOG_ERR" in t:
                    log.write("%.3f DEV %s\n" % (now, t))
            if now >= next_stats:
                dev.send("JS8 STATS")
                next_stats = now + 60
            if now >= next_psk:
                path, info = fetch_psk(a.out, "mid")
                note("psk mid %s %s" % (path, info))
                next_psk = now + 600
            log.flush()
    finally:
        path, info = fetch_psk(a.out, "end")
        note("psk end %s %s" % (path, info))
        for line in dev.command("JS8 STATUS", timeout=5):
            log.write("%.3f DEV %s\n" % (time.time(), line))
        for line in dev.command("JS8 STOP", timeout=10):
            log.write("%.3f DEV %s\n" % (time.time(), line))
        note("stopped (receiver returned to FT8 mode)")
        log.close()


if __name__ == "__main__":
    main()
