#!/usr/bin/env python3
"""Send serial commands to an OrcSDR P4 Core board after the serial pairing handshake (PAIR/AUTH, as tools/help_media.py does).

  python tools/p4_serial_cmd.py COM28 "RTL_WEB ON" "RTL_WEB" [--key .orclink/ui-doc.key]

The key file is created on first use (32 random bytes, hex) and is never committed (.orclink/ is ignored). A board already paired with a
different key answers PAIR_LOCKED and is left alone. Prints the RTL_*, *_OK and error replies to each command.
"""
import argparse
import hashlib
import hmac
import secrets
import sys
import time
from pathlib import Path

import serial

ap = argparse.ArgumentParser()
ap.add_argument("port")
ap.add_argument("commands", nargs="+")
ap.add_argument("--key", type=Path, default=Path(__file__).resolve().parents[1] / ".orclink" / "ui-doc.key")
a = ap.parse_args()
a.key.parent.mkdir(parents=True, exist_ok=True)
if a.key.exists():
    key = bytes.fromhex(a.key.read_text().strip())
else:
    key = secrets.token_bytes(32)
    a.key.write_text(key.hex())
s = serial.Serial()
s.port, s.baudrate, s.timeout = a.port, 115200, 0.5
s.dtr = False
s.rts = False  # keep the auto-reset lines idle so opening the port does not restart the board
s.open()


def send(text):
    s.write((text + "\n").encode())
    s.flush()


def read(secs):
    end = time.time() + secs
    out = []
    while time.time() < end:
        line = s.readline().decode("utf-8", "replace").strip()
        if line:
            out.append(line)
    return out


for _ in range(40):  # wait for the command loop (a board that just booted needs a few seconds)
    send("RTL_STATUS")
    if any(x.startswith("RTL_SDR_STATUS") for x in read(1)):
        break
send("PAIR " + key.hex())
r = read(4)
if not any("PAIR_OK" in x for x in r):
    sys.exit("pairing refused: " + " | ".join(x for x in r if "PAIR" in x) or "no PAIR reply")
n = secrets.token_bytes(16)
send("AUTH %s %s" % (n.hex(), hmac.new(key, b"host" + n, hashlib.sha256).hexdigest()))
r = read(2)
if not any(x.startswith("AUTH_OK") for x in r):
    sys.exit("auth failed: " + " | ".join(r[-3:]))
for c in a.commands:
    send(c)
    for line in read(3):
        if line.startswith("RTL_") or "_OK" in line or "ERROR" in line:
            print(line)
