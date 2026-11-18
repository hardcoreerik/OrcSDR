#!/usr/bin/env python3
"""Repeatable Wi-Fi outage test for a Tab5 running a remote rtl_tcp receiver. No router, hotspot or person is involved.

  python tools/rtl_tcp_wifi_outage_test.py --keepalive 1 --outage 360            # expect: Wi-Fi and the stream come back
  python tools/rtl_tcp_wifi_outage_test.py --keepalive 0 --outage 360 --expect wedge   # expect: it gives up (the old bug)

It uses the firmware's test command `RTL_WIFI_OUTAGE <seconds>` (authenticated serial): the access point "vanishes" for that
long. A live link is dropped with a real disassociation, and every connect request fails as if the network were not found, until
the time is up, after which the next real connect works. So the reconnect, give-up, keep-alive re-arm and stream-reconnect code
all run for real; only the radio side of "the access point is gone" is simulated, and the C6 link recovery is not exercised.

Needs: the Tab5 on its normal network with the remote source selected, a reachable rtl_tcp server (the stream must be
streaming), the FT8 dashboard (this script opens it on 14.074 MHz), and the serial key (tools/tab5_ui_drive.py finds it).
It restores the lab afterwards: keep-alive back to ON, and if the Tab5 is still offline it waits for it to come back.

Exit code 0 when the observed outcome matches --expect (default: recover), 1 when it does not, 2 when the preconditions fail.
"""
import argparse
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tab5_ui_drive import Drive  # noqa: E402


def now():
    return time.time()


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port", default="COM17")
    p.add_argument("--keepalive", type=int, choices=(0, 1), default=1)
    p.add_argument("--outage", type=int, default=360, help="seconds the access point stays gone (give-up needs about 230)")
    p.add_argument("--recover-wait", type=int, default=240, help="seconds to wait for recovery after the outage ends")
    p.add_argument("--expect", choices=("recover", "wedge"), default="recover")
    p.add_argument("--log", default="", help="write every serial line seen during the outage to this file (to see where a failure starts)")
    a = p.parse_args()

    d = Drive(a.port)
    tcp = d.tcp()
    if tcp.get("state") != "streaming":
        d.open_dashboard("ft8")
        d.cmd("FT8 BAND 14074000", prefixes=("ORC_FT8",))
        time.sleep(20)
        tcp = d.tcp()
    wifi = d.cmd("RTL_WIFI_STATUS", prefixes=("RTL_WIFI_STATUS",))
    if tcp.get("state") != "streaming" or not wifi or "connected=1" not in wifi[0]:
        print("PRECONDITION FAILED: the Tab5 must be on Wi-Fi with the remote stream streaming:", tcp.get("state"), wifi)
        return 2
    d.cmd(f"RTL_TCP WIFIKEEP {a.keepalive}", prefixes=("RTL_TCP",))
    slots0 = int(re.search(r"slots=(\d+)", (d.cmd("FT8 STATUS", prefixes=("ORC_FT8_STATUS",)) or [""])[0]).group(1) or 0)
    bytes0 = int(tcp["bytes"])
    print(f"baseline: stream streaming, {bytes0} bytes, ft8 slots {slots0}, keep-alive {a.keepalive}, outage {a.outage} s", flush=True)

    d.dev.authenticate(d.key)
    d.dev.send(f"RTL_WIFI_OUTAGE {a.outage}")
    t0 = now()
    logf = open(a.log, "w") if a.log else None
    events = []   # (seconds since the outage started, text)
    marks = {"link_lost": None, "give_up": None, "rearm": None, "wifi_back": None, "stream_back": None}
    end = t0 + a.outage + a.recover_wait
    last_report = t0
    while now() < end:
        try:
            raw = d.dev.s.readline()
        except Exception as e:   # the port can drop while the Tab5 is busy; keep going
            events.append((now() - t0, f"serial error {e}"))
            time.sleep(2)
            continue
        t = now() - t0
        line = raw.decode(errors="replace").strip() if raw else ""
        if logf and line and not line.startswith("{") and "RTL_DRAM_BUDGET" not in line and "ORC_FT8" not in line:
            logf.write(f"{t:7.1f}s {line[:220]}\n")
            logf.flush()
        if "RTL_WIFI_RECONNECT_SCHEDULED reason=link_lost" in line and marks["link_lost"] is None:
            marks["link_lost"] = t
        elif "RTL_WIFI_RECONNECT_GIVE_UP" in line and marks["give_up"] is None:
            marks["give_up"] = t
        elif "RTL_WIFI_KEEPALIVE rearm" in line:
            marks["rearm"] = marks["rearm"] or t
        elif "RTL_WIFI_CONNECTED" in line and t > 5 and marks["wifi_back"] is None:
            marks["wifi_back"] = t
        elif "RTL_TCP_STATE state=streaming" in line and marks["wifi_back"] is not None and marks["stream_back"] is None:
            marks["stream_back"] = t
        if any(k in line for k in ("RTL_WIFI_RECONNECT", "RTL_WIFI_KEEPALIVE", "RTL_WIFI_CONNECTED", "RTL_WIFI_OUTAGE", "RTL_TCP_STATE")):
            events.append((t, line[:140]))
        if marks["stream_back"] is not None and t > a.outage:
            break
        if now() - last_report > 60:
            last_report = now()
            print(f"  t={t:5.0f}s  link_lost={marks['link_lost']} give_up={marks['give_up']} rearm={marks['rearm']} wifi_back={marks['wifi_back']}", flush=True)

    recovered = marks["wifi_back"] is not None
    stream = None
    decoded_after = None
    if recovered:
        time.sleep(30)
        t1 = d.tcp()
        stream = (t1.get("state"), t1.get("link"), int(t1["bytes"]) > bytes0)
        st = (d.cmd("FT8 STATUS", prefixes=("ORC_FT8_STATUS",)) or [""])[0]
        m = re.search(r"slots=(\d+)", st)
        decoded_after = (int(m.group(1)) if m else 0) >= 0
    print("\nEVENTS")
    for t, text in events[:40]:
        print(f"  {t:6.1f}s  {text}")
    print("\nSUMMARY")
    print(f"  keep-alive {a.keepalive}, outage {a.outage} s")
    print(f"  link lost at {marks['link_lost']}, gave up at {marks['give_up']}, keep-alive re-armed at {marks['rearm']}")
    print(f"  Wi-Fi back at {marks['wifi_back']} s ({'+' + str(round(marks['wifi_back'] - a.outage)) + ' s after the outage ended' if recovered else 'not within the wait'})")
    print(f"  stream back at {marks['stream_back']} s; 30 s later: {stream}")
    outcome = "recover" if (recovered and stream and stream[0] == "streaming" and stream[2]) else "wedge"
    print(f"  OUTCOME: {outcome}   EXPECTED: {a.expect}   -> {'PASS' if outcome == a.expect else 'FAIL'}")

    # Leave the lab connected: keep-alive back on, and wait for the Tab5 if it is still offline.
    d.cmd("RTL_TCP WIFIKEEP 1", prefixes=("RTL_TCP",))
    if not recovered:
        print("restoring: keep-alive ON, waiting for the Tab5 to reconnect ...", flush=True)
        for _ in range(40):
            time.sleep(15)
            w = d.cmd("RTL_WIFI_STATUS", prefixes=("RTL_WIFI_STATUS",))
            if w and "connected=1" in w[0]:
                print("  Tab5 back on Wi-Fi", flush=True)
                break
    return 0 if outcome == a.expect else 1


if __name__ == "__main__":
    sys.exit(main())
