#!/usr/bin/env python3
"""Save a real Waveshare LVGL screen as PNG over its existing serial console."""
import argparse
import base64
import datetime
import json
from pathlib import Path
import secrets
import struct
import time
import zlib


def validate_png(data, width, height):
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise ValueError("Invalid PNG signature")
    offset = 8
    first = True
    while offset + 12 <= len(data):
        size = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4:offset + 8]
        end = offset + 8 + size
        if end + 4 > len(data):
            raise ValueError("Truncated PNG chunk")
        if zlib.crc32(data[offset + 4:end]) & 0xffffffff != struct.unpack_from(">I", data, end)[0]:
            raise ValueError("PNG chunk checksum mismatch")
        if first:
            if kind != b"IHDR" or size != 13 or struct.unpack_from(">II", data, offset + 8) != (width, height):
                raise ValueError("PNG dimensions/header mismatch")
            first = False
        offset = end + 4
        if kind == b"IEND":
            if size or offset != len(data):
                raise ValueError("Invalid PNG end")
            return
    raise ValueError("Missing PNG end")


def receive(port, token, timeout):
    deadline = time.monotonic() + timeout
    meta = None
    output = bytearray()
    seq = 0
    while time.monotonic() < deadline:
        line = port.readline().decode("ascii", errors="replace").strip()
        start = line.find("ORCUI_PNG_")
        if start < 0:
            continue  # Radio/boot logs can coexist with framed screenshot data.
        parts = line[start:].split()
        if len(parts) < 2 or parts[1] != token:
            continue
        if parts[0] == "ORCUI_PNG_ERROR":
            raise ValueError("Device screenshot error: " + " ".join(parts[2:]))
        if parts[0] == "ORCUI_PNG_BEGIN":
            if meta is not None or len(parts) != 7:
                raise ValueError("Invalid/repeated screenshot header")
            width, height, size = map(int, parts[2:5])
            if not (0 < width <= 4096 and 0 < height <= 4096 and 0 < size <= 4 * 1024 * 1024):
                raise ValueError("Screenshot exceeds limits")
            meta = dict(width=width, height=height, bytes=size,
                        crc32=int(parts[5], 16), firmware_elf_sha256=parts[6])
        elif parts[0] == "ORCUI_PNG_DATA":
            if meta is None or len(parts) != 4 or int(parts[2]) != seq:
                raise ValueError("Missing/out-of-order screenshot chunk")
            output.extend(base64.b64decode(parts[3], validate=True))
            if len(output) > meta["bytes"]:
                raise ValueError("Screenshot exceeds declared length")
            seq += 1
        elif parts[0] == "ORCUI_PNG_END":
            if meta is None or len(output) != meta["bytes"]:
                raise ValueError("Incomplete screenshot")
            if zlib.crc32(output) & 0xffffffff != meta["crc32"]:
                raise ValueError("Screenshot checksum mismatch")
            validate_png(output, meta["width"], meta["height"])
            return bytes(output), meta
    raise TimeoutError("Screenshot timed out; no PNG saved")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Explicit verified port, e.g. COM40")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--view", choices=("current", "home", "drawer"), default="current")
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--output-dir", type=Path,
                        default=Path(__file__).resolve().parents[2] / "artifacts/screenshots/waveshare-4b")
    args = parser.parse_args()
    import serial
    token = secrets.token_hex(8)
    # Set control lines before opening: screenshots never reset or flash a board.
    port = serial.Serial(port=None, baudrate=args.baud, timeout=.5)
    port.dtr = False
    port.rts = False
    port.port = args.port
    port.open()
    try:
        port.write(f"\nORCUI_SCREENSHOT {token} {args.view}\n".encode("ascii"))
        port.flush()
        png, meta = receive(port, token, args.timeout)
    finally:
        port.close()
    now = datetime.datetime.now(datetime.timezone.utc)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    dest = args.output_dir / f"{now:%Y%m%dT%H%M%S.%fZ}-{args.port}-{args.view}.png"
    temporary = dest.with_suffix(".part")
    temporary.write_bytes(png)
    temporary.replace(dest)
    meta.update(timestamp_utc=now.isoformat(), port=args.port, view=args.view)
    dest.with_suffix(".json").write_text(json.dumps(meta, indent=2), encoding="utf-8")
    print(dest.resolve())


if __name__ == "__main__":
    main()
