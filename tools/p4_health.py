#!/usr/bin/env python3
"""Health check for the OrcSDR Waveshare P4 console, run after every flash.

  python tools/p4_health.py --port COM8

Watches the serial log for ~25 s (Home screen, receiver streaming) and checks, with numbers:
  * the receiver is streaming at the expected rate
  * no task-watchdog or panic output (the ~5 s screen flash)
  * core 1 keeps idle headroom (RTL_CPU) so the receiver/audio cannot starve the watchdog
  * Home plot draw time and frame period (HOME_PLOT line): the frame-rate regression guard
  * audio: blocks flowing, nothing dropped
Exit code 0 = all pass. Reuses the pairing key like tools/tab5_ft8.py. It never resets the board.
"""
import argparse, os, re, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tab5_ft8


def wait_for(t, prefixes, timeout):
    return t.read_until(prefixes, timeout, echo=False)


def command(t, line, prefix, timeout=8):
    t.send(line)
    lines = t.read_until((prefix,), timeout, echo=False)
    return [l for l in lines if prefix in l]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--key")
    args = ap.parse_args()
    t = tab5_ft8.Tab5(args.port)
    key = tab5_ft8.find_key(args.key)
    for _ in range(6):
        try:
            t.authenticate(key)
            break
        except SystemExit:
            time.sleep(0.8)
    failures = []

    def check(name, ok, detail):
        print(("PASS " if ok else "FAIL ") + name + ": " + detail)
        if not ok:
            failures.append(name)

    # Passive window: watchdog / panic / frame timing.
    log = t.read_until(("\x00never",), 22, echo=False)
    text = "\n".join(log)
    check("no watchdog or panic", not re.search(r"task_wdt|Guru Meditation|abort\(\)", text),
          "clean" if not re.search(r"task_wdt|Guru Meditation|abort\(\)", text) else "watchdog/panic in the log")
    plots = re.findall(r"HOME_PLOT .*?draw_us=(\d+).*?p50_ms=(\d+)", text)
    if plots:
        draw_us, p50 = map(int, plots[-1])
        check("Home draw time", draw_us < 40000, f"{draw_us / 1000:.1f} ms (limit 40)")
        check("Home frame period", p50 < 90, f"{p50} ms p50 (limit 90)")
    else:
        check("Home frame timing", False, "no HOME_PLOT line seen (is Home on screen?)")

    st = command(t, "RTL_STATUS", "RTL_SDR_STATUS")
    m = re.search(r"streaming=(\d+) effective_sps=(\d+)", st[0]) if st else None
    check("receiver streaming", bool(m) and m.group(1) == "1" and int(m.group(2)) > 900000,
          st[0].split("driver")[0] if st else "no reply")

    t.send("RTL_CPU")
    cpu = t.read_until(("\x00never",), 4.5, echo=False)
    idle1 = [float(re.search(r"([\d.]+)%", l).group(1)) for l in cpu if "IDLE1" in l]
    check("core 1 idle headroom", bool(idle1) and idle1[0] >= 10.0, f"IDLE1 {idle1[0] if idle1 else '?'}% (limit >=10)")

    a1 = command(t, "RTL_AUDIO STATUS", "RTL_AUDIO_STATUS")
    time.sleep(2)
    a2 = command(t, "RTL_AUDIO STATUS", "RTL_AUDIO_STATUS")
    if a1 and a2:
        b1 = int(re.search(r"blocks=(\d+)", a1[0]).group(1))
        b2 = int(re.search(r"blocks=(\d+)", a2[0]).group(1))
        dropped = int(re.search(r"dropped=(\d+)", a2[0]).group(1))
        check("audio flowing", b2 > b1 and dropped == 0, f"{b2 - b1} blocks in 2 s, dropped {dropped}")
    else:
        check("audio flowing", False, "no RTL_AUDIO STATUS reply")

    print("\nHEALTH: " + ("PASS" if not failures else "FAIL (" + ", ".join(failures) + ")"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
