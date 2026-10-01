#!/usr/bin/env python3
"""Capture the Tab5 serial console to a file, surviving board resets and USB re-enumeration.

Opens the port without toggling DTR/RTS (so it never resets the board), writes complete lines as they
arrive, and reopens the port if it disappears. Every line is stamped with seconds since start.

    python serial_capture.py COM17 out.txt 120
"""

from __future__ import annotations

import sys
import time

import serial  # pyserial


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    port, out_path, seconds = sys.argv[1], sys.argv[2], float(sys.argv[3])
    t0 = time.time()
    with open(out_path, "w", encoding="utf-8", buffering=1) as out:
        def log(message: str) -> None:
            out.write(f"[{time.time() - t0:7.1f}] {message}\n")

        ser = None
        buf = b""
        while time.time() - t0 < seconds:
            if ser is None:
                try:
                    ser = serial.Serial()
                    ser.port, ser.baudrate, ser.timeout = port, 115200, 0.3
                    ser.dtr = False
                    ser.rts = False
                    ser.open()
                    log("#PORT_OPEN")
                except Exception as exc:  # port absent while the board re-enumerates
                    ser = None
                    time.sleep(0.5)
                    continue
            try:
                chunk = ser.read(4096)
            except Exception as exc:
                log(f"#PORT_LOST {exc!r}")
                try:
                    ser.close()
                except Exception:
                    pass
                ser = None
                time.sleep(0.5)
                continue
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                out.write(f"[{time.time() - t0:7.1f}] {line.decode('utf-8', 'replace').rstrip()}\n")
        log("#DONE")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
