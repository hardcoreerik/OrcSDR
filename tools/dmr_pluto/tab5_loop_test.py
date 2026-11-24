#!/usr/bin/env python3
"""Tab5 side of the conducted DMR test (COM17, receive only): tunes the Digital Voice receiver to the test channel, holds there, turns on the
development captures (speech WAV and 12 s IQ around call-like events), samples the voice counters while the Pluto transmits, and scores the
deltas against what was sent. usage: tab5_loop_test.py --channel-mhz 915.0 --seconds 45 --expect-calls 6 [--label atten60] [--out results.jsonl]
Expected per call (default test call, 8 superframes): 8 qualified groups, 144 frames (24 speech + 120 silence), identity src>dst."""
import argparse, json, sys, time
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "artifacts/digital-voice-integration/benchmarks"))
from tab5_bench_lib import connect, run, find, kv, dv

def counters(c):
    a = kv(find(run(c, "RTL_DV AUDIO STATUS", 1.0), "RTL_DV_AUDIO "))
    u, r = dv(c)
    iq = kv(find(run(c, "RTL_DV IQ STATUS", 1.0), "RTL_DV_IQ "))
    g = lambda d, k: int(d.get(k, 0) or 0)
    return dict(groups=g(a, "groups"), frames=g(a, "frames"), speech=g(a, "speech"), silence=g(a, "silence"), errors=g(a, "errors"),
                lost_sf=g(a, "lost_sf"), underruns=g(a, "underruns"), qual=g(a, "qual"), unq=g(a, "unq"), prot=g(a, "prot"), calls=g(a, "calls"),
                dmr_acq=g(r, "dmr_acq"), dmr_ev=g(r, "dmr_ev"), dmr_id=g(r, "dmr_id"), loss=g(r, "loss"), ring_drops=g(r, "ring_drops"),
                level=g(r, "level_milli"), iq_saved=g(iq, "saved"), phase=a.get("phase"))

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--channel-mhz", type=float, default=915.0); ap.add_argument("--seconds", type=float, default=45)
    ap.add_argument("--expect-calls", type=int, default=0); ap.add_argument("--frames-per-call", type=int, default=144)
    ap.add_argument("--groups-per-call", type=int, default=8); ap.add_argument("--label", default="run"); ap.add_argument("--out", default=None)
    ap.add_argument("--gain-tenth-db", type=int, default=None, help="manual tuner gain (e.g. 496 = 49.6 dB); default = auto")
    a = ap.parse_args()
    c = connect()
    try:
        run(c, "RTL_UI OPEN DIGITAL_VOICE", 2.0); time.sleep(3)
        run(c, "RTL_DV SCAN STOP", 3.0); run(c, "RTL_DV MASK 1", 1.2); run(c, f"RTL_DV PLAN {a.channel_mhz}:dmr", 1.5)
        run(c, "RTL_DV SCAN START", 5.0); time.sleep(4)
        if a.gain_tenth_db is None: run(c, "RTL_DRIVER GAINMODE AUTO", 1.5)
        else: run(c, "RTL_DRIVER GAINMODE MANUAL", 1.5); run(c, f"RTL_DRIVER GAIN {a.gain_tenth_db}", 1.5)
        run(c, "RTL_DV SCAN HOLD", 1.5)
        run(c, "RTL_DV AUDIO SAVE ON", 1.0); run(c, "RTL_DV IQ ON", 1.0)
        base = counters(c); t0 = time.time(); series = []
        while time.time() - t0 < a.seconds:
            time.sleep(5); series.append(dict(t=round(time.time() - t0), **counters(c)))
        end = counters(c)
        run(c, "RTL_DV SCAN STOP", 3.0)
    finally:
        c.close()
    d = {k: end[k] - base[k] for k in end if isinstance(end[k], int)}
    exp = dict(groups=a.expect_calls * a.groups_per_call, frames=a.expect_calls * a.frames_per_call)
    row = dict(label=a.label, t=time.strftime("%Y-%m-%d %H:%M:%S"), channel_mhz=a.channel_mhz, seconds=a.seconds, delta=d,
               expected=exp, groups_pct=round(100.0 * d["groups"] / exp["groups"], 1) if exp["groups"] else None,
               frames_pct=round(100.0 * d["frames"] / exp["frames"], 1) if exp["frames"] else None, level_milli_end=end["level"])
    print(json.dumps(row))
    if a.out:
        with open(a.out, "a") as f: f.write(json.dumps(row) + "\n")

if __name__ == "__main__":
    main()
