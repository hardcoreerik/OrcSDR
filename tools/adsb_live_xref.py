#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Run the Tab5 ADS-B receiver for N minutes and cross-check every aircraft it decodes against the live adsb.lol feed.

Usage: adsb_live_xref.py <port> <minutes> <out_dir>
It authenticates with the project key (tab5_ft8.find_key), sets serial verbosity to TRACE (the firmware then prints one RTL_ADSB_FRAME
line per 250 ms with ICAO, callsign, altitude, speed and heading), opens the ADS-B screen, and once a minute asks adsb.lol for each ICAO
heard in that minute. Results go to <out_dir>/xref.jsonl and a summary prints at the end. The previous verbosity is restored on exit.
Research use only; no credentials are involved (adsb.lol needs none).
"""
import json, os, re, sys, time, urllib.request
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tab5_ft8 as t
import tab5_ft8_shot as sh

MAX_FRAME_AGE_S = 15        # a frame older than this is not compared with the live feed (the aircraft has moved on)
MAX_LOOKUPS_PER_PASS = 10   # feed lookups block; aircraft beyond the cap are carried into the next pass, not dropped
FRAME = re.compile(r"RTL_ADSB_FRAME raw=(\S+) icao=(\w+) tc=(\d+) callsign=(\S*) altitude_ft=(-?\d+) altitude_valid=(\d) "
                   r"speed_kts=(-?\d+) speed_valid=(\d) heading_deg=(-?\d+) heading_valid=(\d)")


def lookup(hexid):
    try:
        req = urllib.request.Request("https://api.adsb.lol/v2/hex/" + hexid.lower(), headers={"User-Agent": "orcsdr-xref/1"})
        with urllib.request.urlopen(req, timeout=15) as r:
            d = json.load(r)
        return d.get("ac", [None])[0] if d.get("ac") else None
    except Exception:
        return None


def main():
    port, minutes, out = sys.argv[1], float(sys.argv[2]), sys.argv[3]
    os.makedirs(out, exist_ok=True)
    key = t.find_key(None)
    dev = t.Tab5(port)

    def cmd(c, pre, to=6):
        dev.authenticate(key); dev.send(c)
        return [x for x in dev.read_until((pre,), to, echo=False) if pre in x]

    old = cmd("RTL_SERIAL VERBOSITY", "RTL_SERIAL_VERBOSITY")
    if not old:                       # never guess the stored mode: a wrong restore would overwrite the device's setting
        sys.exit("could not read the current serial verbosity; nothing was changed")
    old_mode = re.search(r"mode=(\w+)", old[-1]).group(1)
    print("previous verbosity", old_mode, flush=True)
    seen = {}          # icao -> last frame dict
    pending = {}       # icao -> time first queued; heard and not yet looked up (oldest is looked up first)
    rows = []
    f = None

    def check(icao):
        ours = seen[icao]
        web = lookup(icao)
        age = time.time() - ours["t"]
        row = dict(utc=time.strftime("%H:%M:%S", time.gmtime()), icao=icao, ours=ours, age_s=round(age, 1),
                   web=None if not web else {k: web.get(k) for k in ("r", "flight", "t", "alt_baro", "gs", "track", "lat", "lon", "seen")})
        if web:
            feed_seen = web.get("seen")        # seconds since the feed last heard the aircraft
            fresh = age <= MAX_FRAME_AGE_S and isinstance(feed_seen, (int, float)) and feed_seen <= MAX_FRAME_AGE_S
            wa = web.get("alt_baro")
            row["d_alt"] = (wa - ours["alt"]) if fresh and isinstance(wa, (int, float)) and ours["alt"] is not None else None
            row["d_gs"] = (web.get("gs") - ours["gs"]) if fresh and web.get("gs") is not None and ours["gs"] is not None else None
            row["callsign_match"] = bool(ours["cs"] not in ("-", "") and (web.get("flight") or "").strip() == ours["cs"].strip())
        f.write(json.dumps(row) + "\n"); f.flush(); rows.append(row)
        print(row["utc"], icao, ours["cs"], "ours alt/gs/hdg", ours["alt"], ours["gs"], ours["hdg"], "age %.0fs" % age,
              "| web", None if not web else (web.get("r"), (web.get("flight") or "").strip(), web.get("alt_baro"), web.get("gs"), web.get("track")),
              "| d_alt", row.get("d_alt"), "d_gs", row.get("d_gs"), flush=True)

    def drain(limit):
        for icao in sorted(pending, key=pending.get)[:limit]:   # oldest queued first, so no aircraft is starved
            del pending[icao]                                   # removed before the lookup: a new frame queues it again
            check(icao)

    try:
        if not cmd("RTL_SERIAL VERBOSITY TRACE", "RTL_SERIAL_VERBOSITY_OK"):
            sys.exit("the device did not confirm TRACE verbosity; nothing was started")
        opened = cmd("RTL_UI OPEN ADSB", "RTL_UI_OPEN_OK")
        print(opened, flush=True)
        if not opened:
            sys.exit("the ADS-B screen did not open")
        f = open(os.path.join(out, "xref.jsonl"), "w")
        end = time.time() + minutes * 60
        next_check = time.time() + 60
        shots = [time.time() + 20 * 60, time.time() + 40 * 60]
        while time.time() < end:
            raw = dev.s.readline()
            if raw:
                m = FRAME.search(raw.decode("utf-8", "replace"))
                if m:
                    icao = m.group(2).upper()
                    seen[icao] = dict(t=time.time(), cs=m.group(4), alt=int(m.group(5)) if m.group(6) == "1" else None,
                                      gs=int(m.group(7)) if m.group(8) == "1" else None,
                                      hdg=int(m.group(9)) if m.group(10) == "1" else None, tc=int(m.group(3)))
                    pending.setdefault(icao, time.time())
            if time.time() >= next_check:
                next_check = time.time() + 60
                drain(MAX_LOOKUPS_PER_PASS)
            if shots and time.time() >= shots[0]:
                shots.pop(0)
                try:
                    dev.authenticate(key); sh.snap_png(dev, key, os.path.join(out, "adsb_%s.png" % time.strftime("%H%M%S", time.gmtime())))
                except BaseException as e:
                    print("snap failed", e, flush=True)
        drain(MAX_LOOKUPS_PER_PASS)       # the last partial interval, so a short run still compares what it heard
    finally:
        if f:
            f.close()
        try:
            if not cmd("RTL_SERIAL VERBOSITY " + old_mode, "RTL_SERIAL_VERBOSITY_OK"):
                print("WARNING: the device did not confirm restoring serial verbosity %s; set it by hand" % old_mode, flush=True)
        except BaseException as e:
            print("WARNING: could not restore serial verbosity %s (%s); set it by hand" % (old_mode, e), flush=True)
    ok = [r for r in rows if r.get("web")]
    print("SUMMARY aircraft heard %d, lookups %d, found on adsb.lol %d, not looked up %d" % (len(seen), len(rows), len(ok), len(pending)), flush=True)
    da = [abs(r["d_alt"]) for r in ok if r.get("d_alt") is not None]
    dg = [abs(r["d_gs"]) for r in ok if r.get("d_gs") is not None]
    if da: print("altitude difference median %d ft, max %d ft (n=%d)" % (sorted(da)[len(da) // 2], max(da), len(da)))
    if dg: print("speed difference median %d kt, max %d kt (n=%d)" % (sorted(dg)[len(dg) // 2], max(dg), len(dg)))
    print("callsign matches", sum(1 for r in ok if r.get("callsign_match")), "of", len(ok), flush=True)


if __name__ == "__main__":
    main()
