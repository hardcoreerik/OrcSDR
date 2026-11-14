#!/usr/bin/env python3
"""Long soak logger for the Tab5 + PlutoSDR source. It never resets the Tab5 and never writes to either device.

Watches from two sides:
  - the Tab5 serial log (resets, asserts, USB errors, recovery cycles, the once-a-minute PLUTO_HEALTH line, stalls);
  - the PC: a ping of the Pluto every 10 s and a read-only ssh probe every 5 minutes (uptime, UDC state, journal error count).

Output, in --out: soak_raw.log (every serial line, host time stamped), events.log (only notable lines), health.csv, probe.csv,
status.json (rewritten every minute) and summary.json (at the end). Create a file named STOP in --out to end early and release the port.

  python tools/pluto_soak.py --port COM39 --hours 8 --out F:/Ai/_pluto_soak/run1 --host fishball.local --key C:/Users/<you>/.ssh/orcsdr_pluto
"""
import argparse
import csv
import json
import os
import re
import subprocess
import threading
import time

import serial

EVENT_PATTERNS = {
    "reset": re.compile(r"SPLASH_BOOT begin|rst:0x|ESP-ROM:|Rebooting"),
    "assert": re.compile(r"assert failed|Guru Meditation|abort\(\)"),
    "usb_ep0": re.compile(r"EP 0 Error"),
    "usb_open_fail": re.compile(r"USB pipe did not open|iiod handshake failed|buffer read ->"),
    "recovery": re.compile(r"PLUTO_RECOVERY|PLUTO_POWER_CYCLE|BOOT_RTL_RECOVERY"),
    "removed": re.compile(r"PlutoSDR removed|PLUTO_DETACHED"),
    "attached": re.compile(r"PlutoSDR attached|PLUTO_ATTACHED"),
    "start_ok": re.compile(r"RTL_START ESP_OK"),
    "start_fail": re.compile(r"RTL_START ESP_ERR|RTL_START cancelled"),
    "wdt": re.compile(r"Task watchdog|WDT|stack overflow|Stack canary"),
}
HEALTH = re.compile(r"PLUTO_HEALTH present=(\d) running=(\d) alive=(\d) state=(-?\d+) samples=(\d+) src_drops=(\d+) usb_err=(\d+) "
                    r"ring_drops=(\d+) cycles=(\d+) int_free=(\d+) dma_largest=(\d+) psram_free=(\d+)")
HEALTH_KEYS = ["present", "running", "alive", "state", "samples", "src_drops", "usb_err", "ring_drops", "cycles", "int_free", "dma_largest",
               "psram_free"]


def ssh_probe(host, key):
    cmd = ["ssh", "-i", key, "-o", "IdentitiesOnly=yes", "-o", "BatchMode=yes", "-o", "ConnectTimeout=8", "-o", "StrictHostKeyChecking=accept-new",
           f"root@{host}", "cut -d. -f1 /proc/uptime; cat /sys/class/udc/*/state; journalctl -p err -b --no-pager 2>/dev/null | wc -l; "
           "for d in /sys/bus/iio/devices/iio:device*; do t=$(cat $d/in_temp0_input 2>/dev/null); [ -n \"$t\" ] && echo \"$(cat $d/name 2>/dev/null)=$t\"; done"]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=25)
        parts = r.stdout.split()
        if r.returncode == 0 and len(parts) >= 3:
            # Temperatures (milli-degrees C) of every IIO device that reports one (the Zynq XADC and the AD9361), joined into one cell.
            return {"ok": True, "uptime_s": parts[0], "udc": parts[1], "journal_errors": parts[2], "temps": ";".join(parts[3:])}
        return {"ok": False, "error": (r.stderr or "no output").strip()[:120]}
    except Exception as e:  # noqa: BLE001
        return {"ok": False, "error": str(e)[:120]}


def ping_ok(host):
    try:
        r = subprocess.run(["ping", "-n", "1", "-w", "1500", host], capture_output=True, text=True, timeout=5)
        return "Reply from" in r.stdout and "unreachable" not in r.stdout
    except Exception:  # noqa: BLE001
        return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--hours", type=float, default=8.0)
    ap.add_argument("--out", required=True)
    ap.add_argument("--host", default="fishball.local")
    ap.add_argument("--key", default="")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    t_start = time.time()
    t_end = t_start + a.hours * 3600
    counts = {k: 0 for k in EVENT_PATTERNS}
    state = {"last_health": None, "last_health_at": 0.0, "stalls": 0, "health_missing": 0, "ping_fail": 0, "ping_total": 0, "probe_fail": 0,
             "probe_total": 0, "min_int_free": None, "min_dma_largest": None, "min_psram_free": None, "last_samples": None, "first_health": None}
    lock = threading.Lock()
    raw = open(os.path.join(a.out, "soak_raw.log"), "w", encoding="utf-8", errors="replace", buffering=1)
    ev = open(os.path.join(a.out, "events.log"), "w", encoding="utf-8", errors="replace", buffering=1)
    hcsv = open(os.path.join(a.out, "health.csv"), "w", newline="")
    hw = csv.writer(hcsv)
    hw.writerow(["elapsed_s"] + HEALTH_KEYS)
    pcsv = open(os.path.join(a.out, "probe.csv"), "w", newline="")
    pw = csv.writer(pcsv)
    pw.writerow(["elapsed_s", "ping_ok", "ssh_ok", "pluto_uptime_s", "udc", "journal_errors", "error", "temps_mdegC"])

    def event(kind, line):
        with lock:
            counts[kind] = counts.get(kind, 0) + 1
        ev.write(f"[{time.time() - t_start:9.1f}s] {kind}: {line}\n")

    stop_flag = threading.Event()

    def pc_side():
        last_probe = 0.0
        while not stop_flag.is_set():
            now = time.time()
            ok = ping_ok(a.host)
            with lock:
                state["ping_total"] += 1
                state["ping_fail"] += 0 if ok else 1
            if not ok:
                event("ping_fail", a.host)
            if now - last_probe >= 300 and a.key:
                last_probe = now
                p = ssh_probe(a.host, a.key)
                with lock:
                    state["probe_total"] += 1
                    state["probe_fail"] += 0 if p["ok"] else 1
                pw.writerow([round(now - t_start), int(ok), int(p["ok"]), p.get("uptime_s", ""), p.get("udc", ""), p.get("journal_errors", ""),
                             p.get("error", ""), p.get("temps", "")])
                pcsv.flush()
                if not p["ok"]:
                    event("probe_fail", p.get("error", ""))
            stop_flag.wait(10)

    threading.Thread(target=pc_side, daemon=True).start()

    def write_status(final=False):
        with lock:
            s = {"elapsed_s": round(time.time() - t_start), "of_s": round(a.hours * 3600), "counts": dict(counts), **{k: v for k, v in state.items()}}
        with open(os.path.join(a.out, "summary.json" if final else "status.json"), "w") as f:
            json.dump(s, f, indent=1)

    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = a.port, 115200, 0.5
    ser.dtr = False  # opening the port must not reset the Tab5
    ser.rts = False
    ser.open()
    buf = b""
    last_status = 0.0
    while time.time() < t_end and not os.path.exists(os.path.join(a.out, "STOP")):
        chunk = ser.read(4096)
        now = time.time()
        if chunk:
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode("utf-8", "replace").rstrip("\r")
                raw.write(f"[{now - t_start:9.1f}s] {text}\n")
                for kind, pat in EVENT_PATTERNS.items():
                    if pat.search(text):
                        event(kind, text[:200])
                m = HEALTH.search(text)
                if m:
                    h = dict(zip(HEALTH_KEYS, (int(x) for x in m.groups())))
                    hw.writerow([round(now - t_start)] + [h[k] for k in HEALTH_KEYS])
                    hcsv.flush()
                    with lock:
                        if state["first_health"] is None:
                            state["first_health"] = h
                        prev = state["last_samples"]
                        if prev is not None and h["running"] and h["samples"] <= prev:
                            state["stalls"] += 1
                            event("stall", f"samples not increasing: {h['samples']} <= {prev}")
                        state["last_samples"] = h["samples"]
                        state["last_health"], state["last_health_at"] = h, now
                        for k, key in (("min_int_free", "int_free"), ("min_dma_largest", "dma_largest"), ("min_psram_free", "psram_free")):
                            if state[k] is None or h[key] < state[k]:
                                state[k] = h[key]
        if state["last_health_at"] and now - state["last_health_at"] > 150:
            with lock:
                state["health_missing"] += 1
                state["last_health_at"] = now  # report once per missed window, not every loop
            event("health_missing", "no PLUTO_HEALTH for 150 s")
        if now - last_status > 60:
            last_status = now
            write_status()
    stop_flag.set()
    write_status(final=True)
    ser.close()


if __name__ == "__main__":
    main()
