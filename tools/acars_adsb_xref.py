#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Cross-check a decoded ACARS position report against a public ADS-B source (adsb.lol, no key needed).

Usage: acars_adsb_xref.py <registration> <lat> <lon> <alt_ft> [--when HH:MM:SS]
The ADS-B lookup is live, so it only means something when run within a minute or two of the report. The script prints the aircraft's
ADS-B position, the distance to the reported position and the altitude difference, extrapolating the ADS-B position along its track
to the report time when --when is given (UTC; the day nearest the ADS-B fix is used, so a report from just before midnight still works).
"""
import argparse, json, math, sys, time, urllib.request
from datetime import datetime, timezone


def fetch(url):
    req = urllib.request.Request(url, headers={"User-Agent": "orcsdr-acars-xref/1"})
    with urllib.request.urlopen(req, timeout=20) as r:
        return json.load(r)


def nm(lat1, lon1, lat2, lon2):
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dlat, dlon = p2 - p1, math.radians(lon2 - lon1)
    a = math.sin(dlat / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dlon / 2) ** 2
    return 2 * 3440.065 * math.asin(math.sqrt(a))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("reg"); ap.add_argument("lat", type=float); ap.add_argument("lon", type=float); ap.add_argument("alt", type=float)
    ap.add_argument("--when")
    a = ap.parse_args()
    d = fetch("https://api.adsb.lol/v2/reg/" + a.reg)
    now = d.get("now", time.time() * 1000) / 1000.0
    if not d.get("ac"):
        print("%s: not in the ADS-B feed right now" % a.reg); return 2
    ac = d["ac"][0]
    lat, lon = ac.get("lat"), ac.get("lon")
    if lat is None:
        print("%s: in the feed but no position" % a.reg); return 2
    seen = ac.get("seen_pos", 0)
    t_pos = now - seen
    # Move the ADS-B fix along its track to the report time.
    if a.when and ac.get("gs") is not None and ac.get("track") is not None:
        h, m, s = (int(x) for x in a.when.split(":"))
        today = datetime.now(timezone.utc).replace(hour=h, minute=m, second=s, microsecond=0).timestamp()
        while today - t_pos > 43200:          # pick the UTC day nearest the fix, not blindly today
            today -= 86400
        while t_pos - today > 43200:
            today += 86400
        dt = today - t_pos
        dist = ac["gs"] / 3600.0 * dt        # nautical miles
        trk = math.radians(ac["track"])
        lat += (dist * math.cos(trk)) / 60.0
        lon += (dist * math.sin(trk)) / (60.0 * max(0.01, math.cos(math.radians(lat))))
    err = nm(lat, lon, a.lat, a.lon)
    alt = ac.get("alt_baro")
    print("%s %s  ADS-B %.3f,%.3f alt %s gs %s trk %s (fix %.0fs old)" % (a.reg, ac.get("flight", "").strip(), lat, lon, alt, ac.get("gs"), ac.get("track"), seen))
    print("reported %.3f,%.3f alt %.0f  -> position difference %.1f nm, altitude difference %s ft" % (
        a.lat, a.lon, a.alt, err, "n/a" if not isinstance(alt, (int, float)) else int(alt - a.alt)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
