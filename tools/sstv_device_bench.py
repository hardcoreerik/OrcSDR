#!/usr/bin/env python3
"""On-device SSTV bench for the Tab5 (no RF, no transmitter).

Authenticates, opens the SSTV dashboard, plays a synthetic transmission generated inside the
firmware through the real audio tap, decoder, display and SD archive (paced at real time unless
--speed is given), samples the counters while it runs, and records everything.

  python tools/sstv_device_bench.py --port COM17 --scenario ROBOT36
  python tools/sstv_device_bench.py --port COM17 --scenario PD120 --speed 2 --key .orclink/ui-doc.key

A timestamped log with every line received is written to the directory the tool is launched
from, plus a one-line JSON summary. The tool only talks to the port it is given: there is no
auto-detection, so it can never touch another board.
"""
import argparse
import json
import sys
import time
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from help_media import Tab5  # noqa: E402


def fields(line: str) -> dict:
    """key=value or key="quoted value" pairs after the leading tag word."""
    line = line.split(" ", 1)[1] if " " in line else ""
    out, i, n = {}, 0, len(line)
    while i < n:
        eq = line.find("=", i)
        if eq < 0:
            break
        space = line.rfind(" ", i, eq)
        start = i if space < 0 else space + 1
        key = line[start:eq]
        if eq + 1 < n and line[eq + 1] == '"':
            end = line.find('"', eq + 2)
            end = n if end < 0 else end
            out[key] = line[eq + 2:end]
            i = end + 1
        else:
            end = line.find(" ", eq)
            end = n if end < 0 else end
            out[key] = line[eq + 1:end]
            i = end + 1
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True, help="the Tab5's own serial port (never auto-detected)")
    ap.add_argument("--key", type=Path, default=Path(".orclink/ui-doc.key"))
    ap.add_argument("--scenario", default="ROBOT36", choices=["ROBOT36", "ROBOT36_NOVIS", "PD120"])
    ap.add_argument("--speed", type=int, default=1, help="1..8, multiples of real time")
    ap.add_argument("--sample", type=float, default=5.0, help="seconds between STATUS samples")
    ap.add_argument("--timeout", type=float, default=240.0)
    ap.add_argument("--no-save-check", action="store_true", help="skip waiting for the SD archive result")
    args = ap.parse_args()

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    log_path = Path(f"sstv-device-{args.scenario.lower()}-{stamp}.log")
    log = log_path.open("w", encoding="utf-8")

    def note(text: str) -> None:
        log.write(f"{time.strftime('%H:%M:%S')} {text}\n")
        log.flush()

    tab5 = Tab5(args.port, args.key)
    tab5.authenticate()
    note(f"# port={args.port} scenario={args.scenario} speed={args.speed}")

    def command(text: str, wait: tuple, timeout: float = 15.0) -> str:
        note(f"> {text}")
        tab5.send(text)
        line = tab5.wait(wait, timeout)
        note(line)
        return line

    def status() -> dict:
        """One STATUS round: collects the SSTV_* lines that follow."""
        note("> SSTV STATUS")
        tab5.send("SSTV STATUS")
        merged, results = {}, []
        deadline = time.monotonic() + 6
        while time.monotonic() < deadline:
            raw = tab5.serial.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", "replace").strip()
            if not line.startswith("SSTV_"):
                continue
            note(line)
            tag = line.split(" ", 1)[0]
            if tag == "SSTV_RESULT":
                results.append(fields(line))
            elif tag in ("SSTV_STATUS", "SSTV_PERF", "SSTV_MEM", "SSTV_ARCHIVE", "SSTV_SELFTEST"):
                merged.update({f"{tag[5:].lower()}.{k}": v for k, v in fields(line).items()})
            if tag == "SSTV_SELFTEST":
                break
        merged["results"] = results
        return merged

    command("SSTV OPEN", ("SSTV_OK dashboard", "SSTV_ERROR"))
    # Opening the dashboard retunes the radio first, so right after a boot it can take several seconds
    # before the receiver is really running. Wait for it instead of guessing a delay.
    before = {}
    for _ in range(30):
        time.sleep(1.0)
        before = status()
        if before.get("status.dashboard") == "1" and before.get("status.active") == "1":
            break
    else:
        note("! dashboard did not become active in 30 s")
        raise SystemExit("SSTV dashboard did not open")
    time.sleep(1.0)
    saved_before = int(before.get("archive.saved", 0) or 0) + int(before.get("archive.failed", 0) or 0)
    reply = command(f"SSTV SELFTEST {args.scenario} SPEED {args.speed}", ("SSTV_OK selftest", "SSTV_ERROR"))
    if reply.startswith("SSTV_ERROR"):
        raise SystemExit(reply)

    samples, done, started = [], False, time.monotonic()
    worst = {"service_us_max": 0, "draw_us_max": 0, "ring_max": 0, "service_cpu_pct": 0}
    internal_min, psram_min = None, None
    next_sample = started + args.sample
    while time.monotonic() - started < args.timeout:
        raw = tab5.serial.readline()
        if raw:
            line = raw.decode("utf-8", "replace").strip()
            if line.startswith("SSTV_SELFTEST_DONE"):
                note(line)
                done = True
        if time.monotonic() >= next_sample:
            s = status()
            samples.append((round(time.monotonic() - started, 1), s))
            for key, field in (("service_us_max", "perf.service_us_max"), ("draw_us_max", "perf.draw_us_max"),
                               ("service_cpu_pct", "perf.service_cpu_pct")):
                worst[key] = max(worst[key], int(s.get(field, 0) or 0))
            ring = str(s.get("perf.ring_max", "0/4096")).split("/")[0]
            worst["ring_max"] = max(worst["ring_max"], int(ring or 0))
            for name, field in (("internal_min", "mem.internal_min"), ("psram_min", "mem.psram_min")):
                if field in s:
                    value = int(s[field])
                    if name == "internal_min":
                        internal_min = value if internal_min is None else min(internal_min, value)
                    else:
                        psram_min = value if psram_min is None else min(psram_min, value)
            print(f"t={samples[-1][0]:6.1f}s state={s.get('status.state','?'):<18} row={s.get('status.row','?'):<8} "
                  f"svc_max={s.get('perf.service_us_max','?')}us cpu={s.get('perf.service_cpu_pct','?')}% "
                  f"ring={s.get('perf.ring_max','?')} draw_max={s.get('perf.draw_us_max','?')}us "
                  f"int_min={s.get('mem.internal_min','?')} psram_min={s.get('mem.psram_min','?')}", flush=True)
            next_sample = time.monotonic() + args.sample
        if done:
            break

    final = {}
    if not args.no_save_check:
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            final = status()
            saved_now = int(final.get("archive.saved", 0) or 0) + int(final.get("archive.failed", 0) or 0)
            if int(final.get("archive.pending", 1) or 0) == 0 and saved_now > saved_before:
                break
            time.sleep(3)
    else:
        final = status()
    note("> SSTV INDEX")
    tab5.send("SSTV INDEX")
    index_rows, end = [], time.monotonic() + 6
    while time.monotonic() < end:
        raw = tab5.serial.readline()
        if raw:
            line = raw.decode("utf-8", "replace").strip()
            if line.startswith("SSTV_INDEX"):
                note(line)
                if line.startswith("SSTV_INDEX_ROW"):
                    index_rows.append(line[len("SSTV_INDEX_ROW "):])
    tab5.close()

    summary = {
        "scenario": args.scenario, "speed": args.speed, "selftest_done": done, "log": str(log_path),
        "worst": worst, "internal_free_min_bytes": internal_min, "psram_free_min_bytes": psram_min,
        "final": {k: v for k, v in final.items() if k != "results"},
        "results": final.get("results", []), "index_tail": index_rows[-3:],
    }
    note("# summary " + json.dumps(summary))
    log.close()
    print(json.dumps({k: summary[k] for k in ("scenario", "selftest_done", "worst", "internal_free_min_bytes",
                                              "psram_free_min_bytes", "results", "index_tail")}, indent=1))
    print(f"log: {log_path}")
    return 0 if done else 1


if __name__ == "__main__":
    raise SystemExit(main())
