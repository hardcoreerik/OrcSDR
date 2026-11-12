#!/usr/bin/env python3
"""Join an OrcSDR P4 console to a Wi-Fi network over USB serial. The password is typed at a hidden prompt.

  python tools/p4_wifi_connect.py --port COM31 --ssid hardcorewifi

Pairs with the same key file the other tools use (tab5_ft8.find_key), sends RTL_WIFI CONNECT, then prints RTL_WIFI STATUS
a few times. The password is never printed, logged or passed on a command line. The board stores it in its own NVS.
SSIDs with spaces are not supported by the RTL_WIFI CONNECT command. It never resets the board (DTR/RTS stay low).
"""
import argparse, getpass, os, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tab5_ft8


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--ssid", required=True)
    ap.add_argument("--key")
    args = ap.parse_args()
    if " " in args.ssid:
        sys.exit("The console command cannot take an SSID with spaces.")
    password = getpass.getpass(f"Wi-Fi password for {args.ssid} (hidden, empty for an open network): ")
    t = tab5_ft8.Tab5(args.port)
    key = tab5_ft8.find_key(args.key)
    for _ in range(6):
        try:
            t.authenticate(key)
            break
        except SystemExit:
            time.sleep(0.8)
    t.send(f"RTL_WIFI CONNECT {args.ssid} {password}".rstrip())
    password = None
    end = time.time() + 6
    while time.time() < end:
        raw = t.s.readline()
        if raw:
            line = raw.decode("utf-8", "replace").rstrip()
            if line.startswith("RTL_WIFI"):
                print(line)
    for _ in range(6):
        t.send("RTL_WIFI STATUS")
        stop = time.time() + 3
        while time.time() < stop:
            raw = t.s.readline()
            if raw and raw.startswith(b"RTL_WIFI_STATUS"):
                print(raw.decode("utf-8", "replace").rstrip())
                break
        time.sleep(2)


if __name__ == "__main__":
    main()
