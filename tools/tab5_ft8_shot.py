#!/usr/bin/env python3
"""Capture the Tab5 screen to a PNG without pausing reception (uses FT8 SHOT, then the SD transfer).

  python tools/tab5_ft8_shot.py --port COM17 [--tab HEARD] name

Needs Pillow. Pairs from the same key file as tab5_ft8.py. Opening the port does not reset the board.
"""
import argparse
import hashlib
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tab5_ft8 as t  # noqa: E402


def keepalive(dev, seconds):
    """The authenticated session expires after a few quiet seconds, so ping while waiting."""
    end = time.time() + seconds
    while time.time() < end:
        dev.send("FT8 TIME")
        dev.read_until(("ORC_FT8_TIME",), 2, echo=False)
        time.sleep(0.8)


def get_file(dev, remote, destination):
    dev.send("SD_GET_BEGIN " + remote.encode().hex())
    ready = dev.read_until(("SD_GET_READY", "SD_GET_ERROR"), 15, echo=False)
    line = next((ln for ln in ready if "SD_GET_READY" in ln or "SD_GET_ERROR" in ln), "")
    if "SD_GET_ERROR" in line or "SD_GET_READY" not in line:
        raise SystemExit("transfer refused: " + line)
    expected = int(re.search(r"bytes=(\d+)", line).group(1))
    digest = hashlib.sha256()
    received = 0
    with open(destination, "wb") as out:
        while received < expected:
            dev.send("SD_GET_CHUNK")
            hdr = dev.read_until(("SD_GET_DATA", "SD_GET_ERROR"), 15, echo=False)
            rec = next((ln for ln in hdr if "SD_GET_DATA" in ln or "SD_GET_ERROR" in ln), "")
            if "SD_GET_DATA" not in rec:
                raise SystemExit("transfer error: " + rec)
            count = int(re.search(r"bytes=(\d+)", rec).group(1))
            chunk = dev.s.read(count)
            while len(chunk) < count:
                chunk += dev.s.read(count - len(chunk))
            out.write(chunk)
            digest.update(chunk)
            received += count
    done = dev.read_until(("SD_GET_DONE", "SD_GET_ERROR"), 30, echo=False)
    line = next((ln for ln in done if "SD_GET_DONE" in ln), "")
    want = re.search(r"sha256=([0-9a-fA-F]{64})", line)
    if not want or want.group(1).lower() != digest.hexdigest():
        raise SystemExit("hash mismatch on the transferred file")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--key")
    ap.add_argument("--tab")
    ap.add_argument("--open", action="store_true", help="open the FT8 dashboard first")
    ap.add_argument("name")
    a = ap.parse_args()
    dev = t.Tab5(a.port)
    dev.authenticate(t.find_key(a.key))
    if a.open:
        dev.send("FT8 OPEN")
        dev.read_until(("ORC_FT8_OPEN_OK",), 5, echo=False)
        keepalive(dev, 4.0)
    if a.tab:
        dev.send("FT8 TAB " + a.tab)
        dev.read_until(("ORC_FT8_TAB_OK", "ORC_FT8_ERROR"), 5, echo=False)
        keepalive(dev, 1.5)
    dev.authenticate(t.find_key(a.key))   # opening a dashboard can end the authenticated session
    dev.send("FT8 SHOT " + a.name)
    lines = dev.read_until(("ORC_FT8_SHOT_OK", "ORC_FT8_ERROR"), 60, echo=False)
    shot = next((ln for ln in lines if "ORC_FT8_SHOT_OK" in ln), None)
    if not shot:
        raise SystemExit("capture failed: " + (" | ".join(l[-100:] for l in lines[-3:]) if lines else "no reply"))
    remote = re.search(r"path=(\S+)", shot).group(1)
    bmp = a.name + ".bmp"
    # The SD transfer is refused while the receiver runs, so stop it, copy the file, and tune the band back.
    dev.send("FT8 STATUS")
    status = " ".join(dev.read_until(("ORC_FT8_STATUS",), 4, echo=False))
    band = re.search(r" band=(\S+)", status)
    dev.send("RTL_STOP")
    dev.read_until(("RTL_STOP",), 4, echo=False)
    keepalive(dev, 2.0)
    try:
        get_file(dev, remote, bmp)
    finally:
        if band:
            dev.send("FT8 BAND " + band.group(1))
            dev.read_until(("ORC_FT8_BAND_OK", "ORC_FT8_BAND_FAILED"), 10, echo=False)
    try:
        from PIL import Image
        Image.open(bmp).convert("RGB").save(a.name + ".png")
        os.remove(bmp)
        print("saved", a.name + ".png")
    except ImportError:
        print("saved", bmp, "(install Pillow for PNG)")


if __name__ == "__main__":
    main()
