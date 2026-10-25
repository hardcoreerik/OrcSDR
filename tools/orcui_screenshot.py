#!/usr/bin/env python3
"""Pull a PNG screenshot from an OrcUI device over its console UART.

The firmware (screen_capture.cpp) answers `ORCUI_SCREENSHOT <16 hex token> <view>` with
ORCUI_PNG_BEGIN / ORCUI_PNG_DATA (base64) / ORCUI_PNG_END lines. This tool does not
reset the board (DTR/RTS stay low), verifies the byte count and CRC-32 the device
reported, and prints the firmware ELF SHA-256 so the image has build provenance.

    python tools/orcui_screenshot.py COM8 out.png [view]      # view: current|home|drawer
"""
import base64
import os
import sys
import time
import zlib

import serial


def capture(port: str, out_path: str, view: str = "current", timeout: float = 30.0, baud: int = 115200):
    token = os.urandom(8).hex()
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, baud, 0.5
    s.dtr = False
    s.rts = False
    s.open()
    try:
        s.reset_input_buffer()
        s.write(f"ORCUI_SCREENSHOT {token} {view}\n".encode())
        info, chunks, error = None, {}, None
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = s.readline().decode("utf-8", "replace").strip()
            if not line:
                continue
            if f"ORCUI_PNG_ERROR {token}" in line:
                error = line[line.index("ORCUI_PNG_ERROR"):]
                break
            if f"ORCUI_PNG_BEGIN {token}" in line:
                parts = line[line.index("ORCUI_PNG_BEGIN"):].split()
                info = dict(w=int(parts[2]), h=int(parts[3]), size=int(parts[4]),
                            crc=int(parts[5], 16), elf=parts[6])
            elif info and f"ORCUI_PNG_DATA {token} " in line:
                parts = line[line.index("ORCUI_PNG_DATA"):].split(" ", 3)
                chunks[int(parts[2])] = parts[3]
            elif info and f"ORCUI_PNG_END {token}" in line:
                break
        if error:
            raise RuntimeError(error)
        if not info:
            raise RuntimeError("no ORCUI_PNG_BEGIN seen (is the screenshot service running?)")
        png = b"".join(base64.b64decode(chunks[i]) for i in sorted(chunks))
        if len(png) != info["size"]:
            raise RuntimeError(f"size mismatch: got {len(png)}, device reported {info['size']}")
        crc = zlib.crc32(png) & 0xFFFFFFFF
        if crc != info["crc"]:
            raise RuntimeError(f"crc mismatch: got {crc:08x}, device reported {info['crc']:08x}")
        with open(out_path, "wb") as f:
            f.write(png)
        return info
    finally:
        s.close()


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    r = capture(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else "current")
    print(f"saved {sys.argv[2]}: {r['w']}x{r['h']}, {r['size']} bytes, crc {r['crc']:08x}, firmware elf sha256 {r['elf']}")
