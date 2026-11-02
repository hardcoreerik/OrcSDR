#!/usr/bin/env python3
"""Capture the OrcSDR screen to a PNG over serial, without pausing the radio, the decoder or the SD card.

  python tools/screenshot.py --port COM17 [--tab HEARD] name        -> name.png

Uses the firmware's `SCREENSHOT` (M5GFX's PNG encoder; the PNG is streamed as base64 lines and checked against a SHA-256). Nothing is stopped
or retuned. Older firmware without SCREENSHOT can still be captured with --sd, which saves a BMP on the SD card, STOPS THE RADIO to copy it,
and retunes the band afterwards (a slot or two of decoding is lost).

--tab switches the FT8 dashboard tab first (LIVE, DECODES, MAP, HUNTER, HEARD, SETUP) and waits for the screen to repaint.
Needs Pillow only for --sd. Pairs from the same key file as tab5_ft8.py. Opening the port does not reset the board.
"""
import argparse
import base64
import hashlib
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tab5_ft8 as t  # noqa: E402

TABS = ("LIVE", "DECODES", "MAP", "HUNTER", "HEARD", "SETUP")


def keepalive(dev, seconds):
    """The authenticated session expires after a few quiet seconds, so ping while waiting."""
    end = time.time() + seconds
    while time.time() < end:
        dev.send("FT8 TIME")
        dev.read_until(("ORC_FT8_TIME",), 2, echo=False)
        time.sleep(0.8)


def switch_tab(dev, key, tab):
    """Switches the dashboard tab and waits for the TAB_OK reply; re-pairs and retries, because opening a dashboard can end the session."""
    for _ in range(3):
        dev.authenticate(key)
        dev.send("FT8 TAB " + tab)
        lines = dev.read_until(("ORC_FT8_TAB_OK", "ORC_FT8_ERROR"), 5, echo=False)
        if any("ORC_FT8_TAB_OK" in ln for ln in lines):
            time.sleep(0.5)   # the repaint runs in the UI loop right after the reply
            return
    raise SystemExit("could not switch to the %s tab (is the FT8 screen open? try `tab5_ft8.py open`)" % tab)


def screenshot(dev, key, destination, tries=3, *, command="SCREENSHOT"):
    """Streams the screen as a PNG with SCREENSHOT; returns the byte count. Interleaved log lines are ignored, a hash mismatch retries."""
    for attempt in range(tries):
        dev.authenticate(key)
        dev.s.reset_input_buffer()
        dev.send(command)
        header = None
        chunks = {}
        end = time.time() + 40
        done = False
        pending = b""
        while time.time() < end and not done:
            # Bulk reads: pyserial's readline() takes one byte at a time and falls behind a fast USB stream, which loses chunks.
            pending += dev.s.read(max(dev.s.in_waiting, 1))
            *complete, pending = pending.split(b"\n")
            for raw in complete:
                text = raw.decode("utf-8", "replace").rstrip()
                if "ORC_SNAP_ERROR" in text or "ORC_FT8_ERROR" in text:
                    raise SystemExit("device refused the capture: " + text[text.find("ORC_"):])
                m = re.search(r"ORC_SNAP_BEGIN width=(\d+) height=(\d+) bytes=(\d+) sha256=([0-9a-f]{64})", text)
                if m:
                    header = (int(m.group(3)), m.group(4))
                    continue
                m = re.match(r"\s*ORC_SNAP_DATA (\d+) ([A-Za-z0-9+/=]+)$", text)
                if m and header:
                    chunks[int(m.group(1))] = m.group(2)
                elif "ORC_SNAP_END" in text and header:
                    done = True
                    break
        if not header or not done:
            print("capture attempt %d incomplete, retrying" % (attempt + 1), file=sys.stderr)
            continue
        data = bytearray()
        try:
            for offset in sorted(chunks):
                if offset != len(data):
                    raise ValueError("missing data before offset %d" % offset)
                data += base64.b64decode(chunks[offset], validate=True)
        except ValueError as error:
            print("capture attempt %d damaged (%s), retrying" % (attempt + 1, error), file=sys.stderr)
            continue
        if len(data) != header[0] or hashlib.sha256(data).hexdigest() != header[1] or data[:8] != b"\x89PNG\r\n\x1a\n":
            print("capture attempt %d failed its length or hash check, retrying" % (attempt + 1), file=sys.stderr)
            continue
        with open(destination, "wb") as out:
            out.write(data)
        return len(data)
    raise SystemExit("capture failed after %d tries" % tries)


# Import compatibility; new callers use screenshot().
snap_png = screenshot

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


def capture_via_sd(dev, key, name):
    """Older firmware: FT8 SHOT writes a BMP to the SD card; the radio is stopped for the copy and the band is tuned back afterwards."""
    dev.authenticate(key)
    dev.send("FT8 SHOT " + name)
    lines = dev.read_until(("ORC_FT8_SHOT_OK", "ORC_FT8_ERROR"), 60, echo=False)
    shot = next((ln for ln in lines if "ORC_FT8_SHOT_OK" in ln), None)
    if not shot:
        raise SystemExit("capture failed: " + (" | ".join(l[-100:] for l in lines[-3:]) if lines else "no reply"))
    remote = re.search(r"path=(\S+)", shot).group(1)
    bmp = name + ".bmp"
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
        Image.open(bmp).convert("RGB").save(name + ".png")
        os.remove(bmp)
        print("saved", name + ".png")
    except ImportError:
        print("saved", bmp, "(install Pillow for PNG)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True)
    ap.add_argument("--key")
    ap.add_argument("--baud",type=int,default=115200)
    ap.add_argument("--tab", type=str.upper, choices=TABS, help="switch the FT8 dashboard to this tab first")
    ap.add_argument("--open", action="store_true", help="open the FT8 dashboard first")
    ap.add_argument("--sd", action="store_true", help="old method (SD card; stops the radio): for firmware without SCREENSHOT")
    ap.add_argument("name")
    a = ap.parse_args()
    key = t.find_key(a.key)
    dev = t.Tab5(a.port,baud=a.baud)
    dev.authenticate(key)
    if a.open:
        dev.send("FT8 OPEN")
        dev.read_until(("ORC_FT8_OPEN_OK",), 5, echo=False)
        keepalive(dev, 4.0)
    if a.tab:
        switch_tab(dev, key, a.tab)
    if a.sd:
        capture_via_sd(dev, key, a.name)
        return
    size = screenshot(dev, key, a.name + ".png")
    print("saved %s.png (%d bytes)" % (a.name, size))


if __name__ == "__main__":
    main()
