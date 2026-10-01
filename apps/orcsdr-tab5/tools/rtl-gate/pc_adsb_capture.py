#!/usr/bin/env python3
"""Reference ADS-B receiver on the PC, scored by the same decoder the Tab5 runs.

Default mode (--iq): record N seconds of raw IQ with rtl_sdr.exe at 1090 MHz / 2.048 MS/s, then decode it with
OrcSDR's own ADS-B decoder (ui/adsb_decoder.cpp, built as adsb_decode_cu8 and run under WSL). Any difference
from the Tab5 is then the dongle, antenna and site, never the decoder. The IQ file is deleted unless --keep-iq.
CSV columns: t,icao,df,tc,altitude_ft,signal.

Legacy mode (--rtl-adsb): run the stock rtl_adsb and keep only CRC-valid DF17/18 frames. That tool allows up to
five bit errors per frame and has a fixed bit alignment, so it hears less than our decoder.

    python pc_adsb_capture.py out.csv 60 40 [device_index] [--keep-iq] [--rtl-adsb]
"""

from __future__ import annotations

import os
import subprocess
import sys
import threading
import time
from pathlib import Path

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


HERE = Path(__file__).resolve().parent
DECODER_SOURCES = [HERE / "adsb_decode_cu8.cpp", HERE.parent.parent / "ui" / "adsb_decoder.cpp"]
RATE = 2_048_000  # the Tab5 decoder's bit timing is built for exactly 2.048 MS/s; any other rate fails the CRC


def wsl_path(path: Path) -> str:
    """Convert a Windows path (drive letter and backslashes) to its /mnt/<drive>/... form for WSL."""
    drive, rest = os.path.splitdrive(str(path.resolve()))
    return "/mnt/" + drive.rstrip(":").lower() + rest.replace("\\", "/")


def ensure_decoder() -> None:
    """Build the host decoder into ~/.orcsdr under WSL if it is missing or older than its sources."""
    newest = max(p.stat().st_mtime for p in DECODER_SOURCES)
    script = (
        'mkdir -p "$HOME/.orcsdr" && out="$HOME/.orcsdr/adsb_decode_cu8" && '
        f'if [ ! -x "$out" ] || [ "$(stat -c %Y "$out")" -lt {int(newest)} ]; then '
        f'g++ -O2 -std=c++17 -I"{wsl_path(HERE.parent.parent / "ui")}" "{wsl_path(DECODER_SOURCES[0])}" '
        f'"{wsl_path(DECODER_SOURCES[1])}" -o "$out"; fi'
    )
    subprocess.run(["wsl.exe", "-e", "bash", "-c", script], check=True, capture_output=True, text=True)


def decode_iq(iq_path: Path, rate: int = RATE) -> list[str]:
    result = subprocess.run(
        ["wsl.exe", "-e", "bash", "-c", f'"$HOME/.orcsdr/adsb_decode_cu8" "{wsl_path(iq_path)}" {rate}'],
        check=True, capture_output=True, text=True)
    return [line for line in result.stdout.splitlines() if line and not line.startswith("#") and not line.startswith("t,")]


def capture_iq_mode(out_path: str, seconds: float, gain: str, device: str, keep_iq: bool) -> int:
    ensure_decoder()
    iq_path = Path(out_path).with_suffix(".cu8")
    samples = int(RATE * seconds)
    subprocess.run(["rtl_sdr.exe", "-f", "1090000000", "-s", str(RATE), "-g", gain, "-d", device, "-n", str(samples), str(iq_path)],
                   capture_output=True, text=True, timeout=seconds + 60)
    rows = decode_iq(iq_path)
    if not keep_iq:
        iq_path.unlink(missing_ok=True)
    with open(out_path, "w", encoding="utf-8") as out:
        out.write("t,icao,df,tc,altitude_ft,signal" + chr(10))
        for row in rows:
            out.write(row + chr(10))
    print(f"{len(rows)} valid frames, {len({r.split(',')[1] for r in rows})} aircraft (our decoder on raw IQ)")
    return 0


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = {a for a in sys.argv[1:] if a.startswith("--")}
    if len(args) < 3:
        print(__doc__)
        return 2
    out_path, seconds, gain = args[0], float(args[1]), args[2]
    device = args[3] if len(args) > 3 else "0"
    if "--rtl-adsb" not in flags:
        return capture_iq_mode(out_path, seconds, gain, device, "--keep-iq" in flags)
    cmd = ["rtl_adsb.exe", "-g", gain, "-d", device]
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
