#!/usr/bin/env python3
"""Reference ADS-B receiver on the PC: run rtl_adsb for N seconds and keep only CRC-valid DF17/DF18 frames.

Used to give the Tab5 a same-time reference (a mature receiver on another dongle and antenna). Frames are
written as CSV: seconds_since_start, icao, df, frame_hex. Gain is in dB for rtl_adsb's -g option (this build
takes 0 as 0 dB, not automatic).

    python pc_adsb_capture.py out.csv 60 40 [device_index]
"""

from __future__ import annotations

import subprocess
import sys
import threading
import time

POLY = 0xFFF409  # Mode S CRC-24 generator (the 25-bit polynomial 0x1FFF409 without its top bit)


def crc24_remainder(message: bytes) -> int:
    """Remainder of the whole message divided by the Mode S polynomial; 0 for a valid DF17/18 frame."""
    value = int.from_bytes(message, "big")
    bits = len(message) * 8
    for i in range(bits - 1, 23, -1):
        if (value >> i) & 1:
            value ^= (0x1FFF409 << (i - 24))
    return value & 0xFFFFFF


def parse_frame(line: str):
    """Return (icao, df, hex) for a CRC-valid extended-squitter line from rtl_adsb, else None."""
    line = line.strip()
    if not (line.startswith("*") and line.endswith(";")):
        return None
    hexa = line[1:-1]
    if len(hexa) != 28:
        return None
    try:
        raw = bytes.fromhex(hexa)
    except ValueError:
        return None
    df = raw[0] >> 3
    if df not in (17, 18):
        return None
    if crc24_remainder(raw) != 0:
        return None
    return hexa[2:8].upper(), df, hexa.upper()


def main() -> int:
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    out_path, seconds, gain = sys.argv[1], float(sys.argv[2]), sys.argv[3]
    cmd = ["rtl_adsb.exe", "-g", gain]
    if len(sys.argv) > 4:
        cmd += ["-d", sys.argv[4]]
    t0 = time.time()
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
    rows = []

    def reader():
        for line in proc.stdout:
            parsed = parse_frame(line)
            if parsed:
                rows.append((time.time() - t0, *parsed))

    thread = threading.Thread(target=reader, daemon=True)
    thread.start()
    time.sleep(seconds)
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
    thread.join(timeout=2)
    with open(out_path, "w", encoding="utf-8") as out:
        out.write("t,icao,df,frame\n")
        for t, icao, df, frame in rows:
            out.write(f"{t:.2f},{icao},{df},{frame}\n")
    print(f"{len(rows)} valid frames, {len({r[1] for r in rows})} aircraft")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
