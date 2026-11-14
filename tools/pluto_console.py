#!/usr/bin/env python3
"""Run one command on a PlutoSDR's debug UART console.

The password is typed by the person at the terminal (getpass). It is never an argument, never written to a file,
and never echoed. Use it for read-only checks or a reboot while developing; the board's other services stay untouched.

  python tools/pluto_console.py --port COM11 --cmd reboot
  python tools/pluto_console.py --port COM11 --cmd "dmesg | tail -40" --user root
"""
import argparse
import getpass
import sys
import time

import serial


def read_for(ser, seconds):
    end = time.time() + seconds
    out = b""
    while time.time() < end:
        out += ser.read(4096)
    return out.decode("utf-8", "replace")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--user", default="root")
    ap.add_argument("--cmd", required=True)
    ap.add_argument("--wait", type=float, default=4.0, help="seconds to collect output after the command")
    ap.add_argument("--boot-wait", type=float, default=90.0, help="seconds to wait for a login or shell prompt")
    a = ap.parse_args()

    ser = serial.Serial(a.port, a.baud, timeout=0.3)
    # Wait for a login prompt or a shell prompt; never send the command into boot output.
    banner = ""
    end = time.time() + a.boot_wait
    while time.time() < end:
        ser.write(b"\r\n")
        banner = read_for(ser, 1.5)
        if "login:" in banner or banner.rstrip().endswith(("#", "$")):
            break
    else:
        print(f"No login or shell prompt within {a.boot_wait:.0f} s (still booting?); nothing was sent.")
        return 1
    if "login:" in banner:
        ser.write(a.user.encode() + b"\r\n")
        read_for(ser, 1.0)
        ser.write(getpass.getpass(f"Password for {a.user} on {a.port}: ").encode() + b"\r\n")
        after = read_for(ser, 2.5)
        if "incorrect" in after.lower() or "login:" in after:
            print("Login failed; nothing was sent.")
            return 1
    print(f"Sending: {a.cmd}")
    ser.write(a.cmd.encode() + b"\r\n")
    sys.stdout.write(read_for(ser, a.wait))
    return 0


if __name__ == "__main__":
    sys.exit(main())
