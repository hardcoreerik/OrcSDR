#!/usr/bin/env python3
"""Attenuation ladder: for each programmed attenuation, the Pluto transmits the DMR test call for N seconds (conducted) while the Tab5 scores
what it decodes. One wiring statement, one confirmation, covers the whole ladder; the ladder never goes below --min-atten-db and every step
runs through pluto_tx.py's own refusals. usage (operator): sweep.py call.npz --wiring "TX1 -> 30 dB pad -> cable -> V4L" --physical-pad-db 30
  --ladder 60,66,72,78,84,89 --seconds 30 --yes-conducted-no-antenna"""
import argparse, json, subprocess, sys, time
from pathlib import Path
HERE = Path(__file__).resolve().parent

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("call"); ap.add_argument("--wiring", required=True); ap.add_argument("--physical-pad-db", type=float, required=True)
    ap.add_argument("--ladder", default="60,66,72,78,84,89"); ap.add_argument("--seconds", type=float, default=30)
    ap.add_argument("--channel-hz", type=float, default=915.0e6); ap.add_argument("--uri", default="ip:192.168.2.1")
    ap.add_argument("--gain-tenth-db", type=int, default=None); ap.add_argument("--out", default=str(HERE / "sweep_results.jsonl"))
    ap.add_argument("--yes-conducted-no-antenna", action="store_true"); ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    ladder = [float(x) for x in a.ladder.split(",")]
    if not a.yes_conducted_no_antenna and not a.dry_run: sys.exit("refused: pass --yes-conducted-no-antenna after checking the cable chain")
    for att in ladder:
        cmd = [sys.executable, str(HERE / "pluto_tx.py"), a.call, "--channel-hz", str(a.channel_hz), "--atten-db", str(att), "--physical-pad-db", str(a.physical_pad_db),
               "--wiring", a.wiring, "--seconds", str(a.seconds), "--uri", a.uri, "--log", a.out + ".tx", "--yes-conducted-no-antenna"]
        if a.dry_run: cmd.append("--dry-run")
        print("=== attenuation", att, "dB", flush=True)
        plan = subprocess.run(cmd + ["--dry-run"] if not a.dry_run else cmd, capture_output=True, text=True)
        try: calls = json.loads(plan.stdout.split("NOTE")[0])["calls_in_run"]
        except Exception: print(plan.stdout[-300:], plan.stderr[-300:]); sys.exit("plan failed")
        if a.dry_run: print(plan.stdout[:400]); continue
        mon = subprocess.Popen([sys.executable, str(HERE / "tab5_loop_test.py"), "--channel-mhz", str(a.channel_hz / 1e6), "--seconds", str(a.seconds + 20),
                                "--expect-calls", str(calls), "--label", f"atten{att:g}", "--out", a.out] + (["--gain-tenth-db", str(a.gain_tenth_db)] if a.gain_tenth_db else []))
        time.sleep(30)                                   # the Tab5 tunes, holds and takes its baseline first
        tx = subprocess.run(cmd, capture_output=True, text=True)
        print(tx.stdout[-500:], tx.stderr[-300:])
        mon.wait()
        if tx.returncode != 0: sys.exit("transmit step failed; stopping the ladder")

if __name__ == "__main__":
    main()
