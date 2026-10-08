#!/usr/bin/env python3
"""Scriptable control of the Tab5 FT8/FT4 receive-only decoder over the USB serial console.

  python tools/tab5_ft8.py --port COM17 status
  python tools/tab5_ft8.py --port COM17 band 40m          # index, label (40m) or dial Hz
  python tools/tab5_ft8.py --port COM17 mode FT8
  python tools/tab5_ft8.py --port COM17 run 1             # keep the decoder running with no FT8 screen
  python tools/tab5_ft8.py --port COM17 config 32 16 0 11000
  python tools/tab5_ft8.py --port COM17 decodes 20
  python tools/tab5_ft8.py --port COM17 save slot_test    # last slot as /sd/ft8/slot_test.wav
  python tools/tab5_ft8.py --port COM17 dump slot.wav     # last decoded slot as a 12 kHz WAV (about 45 s over serial)
  python tools/tab5_ft8.py --port COM17 watch 120         # print decoder log lines for 120 s
  python tools/tab5_ft8.py --port COM17 raw "RTL_DRIVER STATUS"

State-changing commands need the PAIR/AUTH session; this script performs it from a 32-byte hex key file
(--key, default: the ORC_UI_DOC_KEY environment variable, else .orclink/ui-doc.key under the repo root). The key is never
printed. Opening the port does not reset the board (DTR/RTS are held low). Receive only: nothing here transmits.
"""
import argparse
import hmac
import hashlib
import os
import secrets
import sys
import time

import serial

TERMINATORS = (
    "ORC_FT8_STATUS", "ORC_FT8_BANDS_END", "ORC_FT8_DECODES_END", "ORC_FT8_OPEN_OK", "ORC_FT8_BAND_OK", "ORC_FT8_BAND_FAILED",
    "ORC_FT8_MODE_OK", "ORC_FT8_CLEAR_OK", "ORC_FT8_RUN_OK", "ORC_FT8_CONFIG_OK", "ORC_FT8_SAVE_OK", "ORC_FT8_ADIF_OK", "ORC_FT8_TAB_OK", "ORC_FT8_SHOT_OK", "ORC_FT8_ERROR",
    "ORC_FT8_HELP control", "ORC_FT8_DUMP_END", "ORC_FT8_TIME",
)


class Tab5:
    def __init__(self, port, baud=115200):
        self.s = serial.Serial()
        self.s.port = port
        self.s.baudrate = baud
        self.s.timeout = 0.2
        self.s.dtr = False       # keep the board out of reset
        self.s.rts = False
        self.s.open()
        self.s.reset_input_buffer()
        self.authed = False

    def send(self, line):
        self.s.write((line + "\n").encode())

    def read_until(self, prefixes, timeout, echo=True):
        end = time.time() + timeout
        lines = []
        while time.time() < end:
            raw = self.s.readline()
            if not raw:
                continue
            text = raw.decode("utf-8", "replace").rstrip()
            if "AUTH_OK " in text:
                text = "AUTH_OK " + text.split("AUTH_OK ", 1)[1]
            lines.append(text)
            if echo:
                print(text.split("AUTH_OK ")[0] + "AUTH_OK [redacted]" if "AUTH_OK " in text else text)
            if any(p in text for p in prefixes):
                return lines
        return lines

    def authenticate(self, key_hex):
        key = bytes.fromhex(key_hex.strip())
        self.send("PAIR " + key_hex.strip())
        reply = self.read_until(("PAIR_OK", "PAIR_LOCKED", "PAIR_INVALID"), 5, echo=False)
        if not any("PAIR_OK" in r for r in reply):
            raise SystemExit("pairing failed: " + (reply[-1] if reply else "no reply"))
        nonce = secrets.token_bytes(16)
        proof = hmac.new(key, b"host" + nonce, hashlib.sha256).digest()
        self.send("AUTH %s %s" % (nonce.hex(), proof.hex()))
        reply = self.read_until(("AUTH_OK", "AUTH_DENIED", "AUTH_ERROR", "AUTH_INVALID"), 5, echo=False)
        ok = [r for r in reply if "AUTH_OK " in r]
        if not ok:
            raise SystemExit("authentication failed: " + (reply[-1] if reply else "no reply"))
        expected = hmac.new(key, b"device" + nonce, hashlib.sha256).hexdigest()
        if ok[0].split("AUTH_OK ", 1)[1].split()[0].lower() != expected:
            raise SystemExit("device proof mismatch")
        self.authed = True

    def command(self, line, timeout=8):
        self.send(line)
        return self.read_until(TERMINATORS, timeout)


def find_key(path):
    candidates = [path, os.environ.get("ORC_UI_DOC_KEY"), os.path.join(os.getcwd(), ".orclink", "ui-doc.key"),
                  r"F:\Ai\OrcSDR\.orclink\ui-doc.key"]
    for c in candidates:
        if c and os.path.isfile(c):
            return open(c).read().strip()
    raise SystemExit("no pairing key file found (use --key)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True)
    ap.add_argument("--key", help="pairing key file (64 hex characters)")
    ap.add_argument("verb")
    ap.add_argument("args", nargs="*")
    a = ap.parse_args()

    verb = a.verb.lower()
    readonly = verb in ("status", "decodes", "bands", "help", "watch", "dump", "time")
    dev = Tab5(a.port)
    if not readonly or verb == "raw":
        dev.authenticate(find_key(a.key))

    if verb == "watch":
        seconds = float(a.args[0]) if a.args else 60.0
        end = time.time() + seconds
        while time.time() < end:
            raw = dev.s.readline()
            if raw:
                t = raw.decode("utf-8", "replace").rstrip()
                if "ORC_FT8" in t:
                    print(time.strftime("%H:%M:%S ") + t)
    elif verb == "dump":
        out = a.args[0] if a.args else "ft8_slot.wav"
        import base64, wave
        dev.send("FT8 DUMP")
        chunks = {}
        done = False
        end = time.time() + 180
        while time.time() < end and not done:
            raw = dev.s.readline()
            if not raw:
                continue
            t = raw.decode("utf-8", "replace").strip()
            if t.startswith("ORC_FT8_DUMP_BEGIN") or t.startswith("ORC_FT8_ERROR"):
                print(t)
            if t.startswith("ORC_FT8_DUMP_DATA "):
                parts = t.split(" ", 2)
                if len(parts) == 3 and parts[1].isdigit() and len(parts[2]) % 4 == 0:
                    try:
                        chunks[int(parts[1])] = base64.b64decode(parts[2], validate=True)
                    except Exception:
                        pass
            if t.startswith("ORC_FT8_DUMP_END"):
                done = True
        if not chunks:
            raise SystemExit("no dump data received")
        last = max(chunks)
        missing = [i for i in range(last + 1) if i not in chunks]
        if missing:
            print("WARNING: %d of %d lines missing (corrupt or lost); gaps are zero-filled" % (len(missing), last + 1))
        data = bytearray()
        for i in range(last + 1):
            data += chunks.get(i, bytes(72))
        with wave.open(out, "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(12000)
            w.writeframes(bytes(data))
        print("wrote %s (%d samples)" % (out, len(data) // 2))
    elif verb == "time":
        # Offset of the Tab5 wall clock from this PC (positive = Tab5 ahead), from several round trips.
        samples = []
        for _ in range(7):
            t0 = time.time()
            dev.send("FT8 TIME")
            lines = dev.read_until(("ORC_FT8_TIME",), 3, echo=False)
            t1 = time.time()
            for ln in lines:
                if "ORC_FT8_TIME utc_ms=" in ln:
                    ms = int(ln.split("utc_ms=")[1].split()[0])
                    samples.append(ms - ((t0 + t1) / 2.0) * 1000.0)
            time.sleep(0.2)
        if not samples:
            raise SystemExit("no reply")
        samples.sort()
        print("ORC_TAB5_CLOCK_OFFSET_MS median=%.0f min=%.0f max=%.0f n=%d (Tab5 minus PC)" % (samples[len(samples) // 2], samples[0], samples[-1], len(samples)))
    elif verb == "settime":
        # Sets the hardware clock from this PC, timed so the new second begins when the device handles the command.
        now = time.time()
        target = int(now) + 2
        time.sleep(max(0.0, target - time.time() - 0.012))
        dev.send("ORC_RTC_SET %d" % target)
        dev.read_until(("ORC_RTC_SET_OK", "ORC_RTC_SET_ERROR"), 4)
    elif verb == "raw":
        dev.send(" ".join(a.args))
        dev.read_until(("zzzz",), 5)
    elif verb in ("open", "clear", "status", "bands", "help", "decodes", "band", "mode", "run", "config", "save", "adif", "tab", "shot"):
        line = "FT8 " + verb.upper() + ((" " + " ".join(a.args)) if a.args else "")
        dev.command(line)
    else:
        raise SystemExit("unknown verb: " + verb)


if __name__ == "__main__":
    main()
