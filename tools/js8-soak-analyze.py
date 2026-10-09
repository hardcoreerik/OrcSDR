#!/usr/bin/env python3
"""Analyse a tools/js8-soak.py run: what the Tab5 decoded versus what PSK Reporter says was on the air.

  python tools/js8-soak-analyze.py F:/AI/OrcSDR-TEMP/js8-soak/run1 --my-grid CN84 --near-km 1500

Definitions (all printed so the numbers can be audited):
  decoded      distinct callsigns in 'text=' of ORC_JS8_RT_FRAME lines (parity + CRC verified, message rendered)
  on-air       distinct senders in PSK Reporter JS8 spots inside the soak window on the same band
  should-hear  on-air senders that at least one receiver within --near-km of this station also reported (a rough "same sky" proxy)
Caveats: PSK Reporter only lists stations whose software uploads spots, so decoded-but-unreported is not an error; and our decoder renders
only directed HEARTBEAT SNR frames so far, so most JS8 traffic (other frame types) cannot be counted as a decode yet.
"""
import argparse
import collections
import glob
import math
import os
import re


def grid_to_latlon(g):
    g = g.strip()
    if len(g) < 4:
        return None
    lon = (ord(g[0].upper()) - 65) * 20 - 180 + int(g[2]) * 2
    lat = (ord(g[1].upper()) - 65) * 10 - 90 + int(g[3])
    if len(g) >= 6:
        lon += (ord(g[4].upper()) - 65) * (2 / 24) + (1 / 24)
        lat += (ord(g[5].upper()) - 65) * (1 / 24) + (1 / 48)
    else:
        lon += 1
        lat += 0.5
    return lat, lon


def km(a, b):
    la1, lo1, la2, lo2 = map(math.radians, (a[0], a[1], b[0], b[1]))
    h = math.sin((la2 - la1) / 2) ** 2 + math.cos(la1) * math.cos(la2) * math.sin((lo2 - lo1) / 2) ** 2
    return 6371 * 2 * math.asin(math.sqrt(h))


def attrs(s):
    return dict(re.findall(r'(\w+)="([^"]*)"', s))


def main():
    import argparse as ap_
    ap = ap_.ArgumentParser()
    ap.add_argument("run")
    ap.add_argument("--my-grid", default="CN84")
    ap.add_argument("--near-km", type=float, default=1500)
    ap.add_argument("--band-lo", type=int, default=7000000)
    ap.add_argument("--band-hi", type=int, default=7300000)
    a = ap.parse_args()
    me = grid_to_latlon(a.my_grid)

    t_first = t_last = None
    decoded = collections.OrderedDict()
    frames = attempts = crc_ok = 0
    methods = collections.Counter()
    for line in open(os.path.join(a.run, "device.log"), encoding="utf-8", errors="replace"):
        parts = line.split(" ", 2)
        if len(parts) < 3:
            continue
        ts = float(parts[0])
        body = parts[2]
        if "ORC_JS8_RT_FRAME" in body:
            attempts += 1
            t_first = ts if t_first is None else t_first
            t_last = ts
            if "crc=ok" in body:
                crc_ok += 1
            m = re.search(r'text=(\S+): (\S+) HEARTBEAT SNR ([+-]\d+)', body)
            if m:
                call = m.group(1)
                hz = re.search(r'hz=([\d.]+)', body)
                bp = re.search(r'bp_iter=(\d+)', body)
                osd = re.search(r'osd_order=(\d+)', body)
                d = decoded.setdefault(call, {"first": ts, "last": ts, "n": 0, "hz": []})
                d["last"] = ts
                d["n"] += 1
                d["hz"].append(float(hz.group(1)) if hz else 0)
                d["to"] = m.group(2)
                d["snr_reported"] = m.group(3)
    window = (t_first or 0, t_last or 0)

    spots = {}
    for path in sorted(glob.glob(os.path.join(a.run, "psk_*.xml"))):
        x = open(path, encoding="utf-8", errors="replace").read()
        for r in re.findall(r"<receptionReport ([^>]*)/>", x):
            d = attrs(r)
            try:
                f = int(d["frequency"])
                t = int(d["flowStartSeconds"])
            except (KeyError, ValueError):
                continue
            if d.get("mode") != "JS8" or not (a.band_lo <= f <= a.band_hi):
                continue
            spots[(d["senderCallsign"], d["receiverCallsign"], t)] = d
    log_start = None
    for line in open(os.path.join(a.run, "device.log"), encoding="utf-8", errors="replace"):
        if " HOST authenticated" in line:
            log_start = float(line.split(" ", 1)[0])
            break
    log_end = float(open(os.path.join(a.run, "device.log"), encoding="utf-8", errors="replace").read().strip().splitlines()[-1].split(" ", 1)[0])
    in_win = [d for d in spots.values() if log_start - 120 <= int(d["flowStartSeconds"]) <= log_end]
    on_air = collections.defaultdict(list)
    for d in in_win:
        on_air[d["senderCallsign"]].append(d)

    near_senders = set()
    for call, rows in on_air.items():
        for d in rows:
            loc = grid_to_latlon(d.get("receiverLocator", "")) if d.get("receiverLocator") else None
            if loc and km(me, loc) <= a.near_km:
                near_senders.add(call)
                break

    dec = set(decoded)
    print("run:", a.run)
    print("soak window: %.0f min (device log %.0f..%.0f)" % ((log_end - log_start) / 60, log_start, log_end))
    print("frames that reached the soft decoder: %d; parity+CRC valid: %d" % (attempts, crc_ok))
    print("decoded distinct callsigns: %d  (messages: %d)" % (len(dec), sum(v["n"] for v in decoded.values())))
    print("PSK on-air distinct senders (JS8, %d-%d Hz, in window): %d from %d spots" % (a.band_lo, a.band_hi, len(on_air), len(in_win)))
    print("PSK 'should hear' (reported by a receiver within %.0f km of %s): %d" % (a.near_km, a.my_grid, len(near_senders)))
    print("decoded AND on air:", len(dec & set(on_air)), " decoded AND should-hear:", len(dec & near_senders))
    print("decoded but never spotted on PSK in the window:", sorted(dec - set(on_air)))
    miss = sorted(near_senders - dec)
    print("should-hear but not decoded: %d (first 40: %s)" % (len(miss), miss[:40]))
    print("--- decoded stations ---")
    for call, d in decoded.items():
        tag = "psk" if call in on_air else "no-psk"
        print("  %-10s n=%-3d first=+%4.0fs last=+%4.0fs hz=%s reported_snr=%s %s%s" % (
            call, d["n"], d["first"] - log_start, d["last"] - log_start, ",".join("%.0f" % h for h in sorted(set(d["hz"]))[:3]), d["snr_reported"], tag,
            " near" if call in near_senders else ""))


if __name__ == "__main__":
    main()
