#!/usr/bin/env python3
"""Repeated Tab5 reboot test with every boot-time feature enabled.

Each cycle sends the authenticated RTL_RESET command to the Tab5 (P4), then
records how long Wi-Fi, OrcDial and audio take to come back, while passively
logging the P4 (native USB), the OrcDial and the ESP32-C6 UART side by side.

Safety:
  * Serial ports are opened with DTR/RTS deasserted; nothing is flashed.
  * The pairing key is read locally and never printed or logged.
  * The splash button gate is turned OFF for the run and restored ON at the end.
  * WIFI_BOOT, WIFI_POWER and AUTO_START are left enabled afterwards (they are the "at boot" items under test).
  * Trust is never changed. Only RTL_RESET and settings toggles are sent.

Usage:
  python run-tab5-boot-cycles.py --cycles 20
  python run-tab5-boot-cycles.py --selftest
"""
import argparse
import hashlib
import hmac
import json
import random
import re
import secrets
import sys
import threading
import time
from datetime import datetime, timezone
from pathlib import Path

try:
    import serial
except ImportError:  # selftest does not need pyserial
    serial = None

FATAL = re.compile(
    r"Guru Meditation|panic|assert failed|abort\(|task watchdog|interrupt wdt|"
    r"brownout detector|0x107|link_failed=1|SDIO.*timeout", re.I)  # link_failed=1 is deliberate: a failed link must fail the cycle
KV = re.compile(r"(\w+)=(\S+)")
DEFAULT_KEY = Path(r"F:\Ai\OrcSDR\.orclink\ui-doc.key")


def parse_kv(line):
    return {k: v for k, v in KV.findall(line)}


def evaluate_status(wifi, bus, dial):
    """Map the three status lines to boolean milestones."""
    return {
        "wifi": wifi.get("connected") == "1",
        "hosted": bus.get("hosted_ready") == "1" and bus.get("link_failed") == "0",
        "capture": wifi.get("capture_state") == "3",
        "orcdial": dial.get("connection") == "Connected" and dial.get("trust") == "1",
    }


class Port:
    """Passive reader that reopens the port when it returns."""

    def __init__(self, label, name, baud, out_dir, events):
        self.label, self.name, self.baud = label, name, baud
        self.events = events
        self.lines = []          # (mono, text)
        self.lock = threading.Lock()
        self.file = (out_dir / f"{label}.log").open("w", encoding="utf-8")
        self.s = None
        self.opens = 0
        self.stop = False
        self.partial = ""
        self.t = threading.Thread(target=self.run, daemon=True)

    def open(self):
        s = serial.Serial(None, self.baud, timeout=0, write_timeout=2)
        s.dtr = False
        s.rts = False
        s.port = self.name
        s.open()
        return s

    def run(self):
        retry = 0.0
        while not self.stop:
            if self.s is None:
                if time.monotonic() < retry:
                    time.sleep(0.05)
                    continue
                try:
                    self.s = self.open()
                    self.opens += 1
                    self.events(f"{self.label} PORT_OPEN {self.name}")
                except (OSError, serial.SerialException):
                    retry = time.monotonic() + 0.5
                    continue
            try:
                raw = self.s.read(8192)
            except (OSError, serial.SerialException) as err:
                self.events(f"{self.label} PORT_LOST {str(err)[:80]}")
                try:
                    self.s.close()
                except Exception:
                    pass
                self.s = None
                retry = time.monotonic() + 0.5
                continue
            if raw:
                now = time.monotonic()
                text = self.partial + raw.decode(errors="replace")
                *done, self.partial = text.split("\n")
                with self.lock:
                    for ln in done:
                        ln = ln.rstrip("\r")
                        self.lines.append((now, ln))
                        self.file.write(f"[{now:.3f}] {ln}\n")
                    self.file.flush()
            else:
                time.sleep(0.02)

    def send(self, cmd):
        if self.s is None:
            raise RuntimeError(f"{self.label} port not open")
        self.s.write((cmd + "\n").encode())

    def mark(self):
        with self.lock:
            return len(self.lines)

    def since(self, idx):
        with self.lock:
            return list(self.lines[idx:])

    def close(self):
        self.stop = True
        self.t.join(timeout=2)
        if self.s:
            try:
                self.s.close()
            except Exception:
                pass
        self.file.close()


class NullPort:
    """Stands in for the C6 UART when the downloader is unplugged (--c6 none)."""
    label = "c6"
    s = None
    opens = 0
    t = threading.Thread(target=lambda: None)

    def mark(self):
        return 0

    def since(self, idx):
        return []

    def send(self, cmd):
        raise RuntimeError("no C6 UART")

    def close(self):
        pass


def wait_line(port, idx, pattern, seconds, ping=None):
    """Return (index_after, line) for the first new line matching pattern."""
    rx = re.compile(pattern)
    end = time.monotonic() + seconds
    next_ping = 0.0
    while time.monotonic() < end:
        for i, (_, ln) in enumerate(port.since(idx)):
            if rx.search(ln):
                return idx + i + 1, ln
        if ping and time.monotonic() >= next_ping:
            try:
                port.send(ping)
            except Exception:
                pass
            next_ping = time.monotonic() + 1.0
        time.sleep(0.05)
    return idx, None


def command(p4, cmd, pattern, seconds=10):
    idx = p4.mark()
    p4.send(cmd)
    _, ln = wait_line(p4, idx, pattern, seconds)
    return ln


def authenticate(p4, key_hex):
    key = bytes.fromhex(key_hex)
    r = command(p4, "PAIR " + key_hex, r"^PAIR_(OK|LOCKED|INVALID)$")
    if r != "PAIR_OK":
        raise RuntimeError(f"PAIR failed: {r}")
    nonce = secrets.token_bytes(16)
    proof = hmac.new(key, b"host" + nonce, hashlib.sha256).hexdigest()
    reply = command(p4, f"AUTH {nonce.hex()} {proof}", r"^AUTH_(OK|DENIED|ERROR|INVALID)")
    m = re.match(r"^AUTH_OK ([0-9A-Fa-f]{64})$", reply or "")
    if not m:
        raise RuntimeError("AUTH failed")
    want = hmac.new(key, b"device" + nonce, hashlib.sha256).digest()
    if not hmac.compare_digest(bytes.fromhex(m.group(1)), want):
        raise RuntimeError("device proof mismatch")


def poll_status(p4):
    wifi = command(p4, "RTL_WIFI_COEX_STATUS", r"^RTL_WIFI_COEX_STATUS ", 4)
    bus = command(p4, "RTL_WIFI_BUS_STATUS", r"^RTL_WIFI_BUS_STATUS ", 4)
    dial = command(p4, "RTL_ORCDIAL_STATUS", r"^RTL_ORCDIAL_STATUS ", 4)
    if not (wifi and bus and dial):
        return None
    return parse_kv(wifi), parse_kv(bus), parse_kv(dial)


BOOT_PROBE_WINDOW_S = 12.0
ROM_BANNER = re.compile(r"ESP-ROM:esp32c6|rst:0x|ets Jun")
RF_LINE = re.compile(r"^ORCDIAL_RF ch=(\d+) tx=(\d+) tx_refused=(\d+) cb_ok=(\d+) cb_fail=(\d+) rx=(\d+)")
RF_CH = re.compile(r"(\d+):tx(\d+)/rx(\d+)")


def dial_send(dial, text):
    """Send to the Dial; False (not an exception) when it is unplugged or powered off."""
    try:
        dial.send(text)
        return True
    except Exception:
        return False


def dial_rf(dial):
    """Read the Dial's radio counters; None if it does not answer."""
    idx = dial.mark()
    if not dial_send(dial, "ORCDIAL_RF"):
        return None
    time.sleep(0.8)
    out = {}
    for _, ln in dial.since(idx):
        m = RF_LINE.match(ln)
        if m:
            out.update(zip(("channel", "tx", "tx_refused", "cb_ok", "cb_fail", "rx"), map(int, m.groups())))
        elif ln.startswith("ORCDIAL_RF_BY_CHANNEL"):
            out["by_channel"] = {c: {"tx": int(t), "rx": int(r)} for c, t, r in RF_CH.findall(ln) if int(t) or int(r)}
    return out or None


def trigger(mode, p4, c6, c6_off_ms, events, n):
    """Start one boot/reset event. Returns False if the device did not acknowledge."""
    if mode == "p4":
        return command(p4, "RTL_RESET", r"RTL_RESETTING", 5) is not None
    if mode == "c6rts":
        # Real C6 reset through the downloader's EN line (RTS). DTR stays deasserted so the C6 boots
        # normally instead of entering download mode. Needs the adapter; works with any P4 build.
        if c6.s is None:
            events(f"CYCLE {n} C6_UART_NOT_OPEN")
            return False
        # Reset the P4 first and pulse the C6 while the P4 is still booting (before its Hosted init at ~3.5 s),
        # which is what a real cold start looks like. Resetting the C6 under a running host only produces
        # SDIO timeouts that say nothing about boot.
        if command(p4, "RTL_RESET", r"RTL_RESETTING", 5) is None:
            return False
        time.sleep(1.0)
        c6.s.dtr = False
        c6.s.rts = True
        time.sleep(0.25)
        c6.s.rts = False
        return True
    off = command(p4, "RTL_WIFI_TEST_C6_POWER OFF CONFIRM", r"^RTL_WIFI_TEST_C6_POWER_RESULT", 5)
    if off is None:
        events(f"CYCLE {n} C6_POWER_COMMAND_UNAVAILABLE (needs the -C6FaultTest build)")
        return False
    time.sleep(c6_off_ms / 1000.0)
    if mode == "c6pulse":
        return command(p4, "RTL_WIFI_TEST_C6_POWER ON CONFIRM", r"^RTL_WIFI_TEST_C6_POWER_RESULT", 5) is not None
    return command(p4, "RTL_RESET", r"RTL_RESETTING", 5) is not None  # c6off_reset: rail restored by boot


def run_dial_cycle(n, p4, dial, key_hex, settle, events):
    """Restart only the Dial (ORCDIAL_RESTART) and time how long the tablet takes to see it return."""
    rec = {"cycle": n, "mode": "dial", "milestones": {}, "fatal": [], "pass": False}
    polled = poll_status(p4)
    if not polled or polled[2].get("connection") != "Connected":
        rec["error"] = "tablet not Connected before the Dial restart"
        return rec
    dial_send(dial, "ORCDIAL_RF RESET")
    time.sleep(0.3)
    t0 = time.monotonic()
    rec["start_mono"] = round(t0, 3)
    events(f"CYCLE {n} DIAL_RESTART")
    dial_send(dial, "ORCDIAL_RESTART")
    dropped = None
    while time.monotonic() - t0 < settle:
        time.sleep(1.0)
        polled = poll_status(p4)
        if not polled:
            continue
        el = time.monotonic() - t0
        connected = polled[2].get("connection") == "Connected"
        if dropped is None and not connected:
            dropped = rec["milestones"]["tablet_saw_drop"] = round(el, 2)
        elif dropped is not None and connected:
            rec["milestones"]["reconnected"] = round(el, 2)
            break
    if "tablet_saw_drop" not in rec["milestones"]:
        # The Dial may return before the tablet's 5 s silence timeout: still fine if it never dropped.
        polled = poll_status(p4)
        if polled and polled[2].get("connection") == "Connected":
            rec["milestones"]["reconnected"] = round(time.monotonic() - t0, 2)
            rec["note"] = "tablet never saw a drop"
    time.sleep(1.5)
    rec["dial_rf"] = dial_rf(dial)
    rec["end_mono"] = round(time.monotonic(), 3)
    rec["pass"] = "reconnected" in rec["milestones"]
    return rec


def run_cycle(n, p4, dial, c6, key_hex, settle, events, mode="p4", c6_off_ms=1500, recover=False):
    if mode == "dial":
        return run_dial_cycle(n, p4, dial, key_hex, settle, events)
    rec = {"cycle": n, "mode": mode, "milestones": {}, "fatal": [], "pass": False}
    authenticate(p4, key_hex)
    rec["dial_present_at_start"] = dial_send(dial, "ORCDIAL_RF RESET")
    time.sleep(0.3)
    marks = {p: p.mark() for p in (p4, dial, c6)}
    t0 = time.monotonic()
    rec["start_mono"] = round(t0, 3)
    events(f"CYCLE {n} {mode.upper()}_TRIGGER")
    if not trigger(mode, p4, c6, c6_off_ms, events, n):
        rec["error"] = "trigger not acknowledged"
        return rec
    deadline = t0 + settle
    chunks_seen = []
    last = None
    while time.monotonic() < deadline:
        time.sleep(1.0)
        try:
            polled = poll_status(p4)
        except RuntimeError:
            polled = None  # port is gone mid-reboot
        if not polled:
            continue
        wifi, bus, dl = polled
        last = (wifi, bus, dl)
        ms = evaluate_status(wifi, bus, dl)
        el = time.monotonic() - t0
        for k, v in ms.items():
            if v and k not in rec["milestones"]:
                rec["milestones"][k] = round(el, 2)
                events(f"CYCLE {n} {k.upper()} t={el:.1f}s")
        if "audio_chunks" in wifi and wifi.get("audio_enabled") == "1":
            chunks_seen.append(int(wifi["audio_chunks"]))
        if len(rec["milestones"]) == 4 and len(chunks_seen) >= 3 and chunks_seen[-1] > chunks_seen[-3]:
            rec["milestones"]["audio_advancing"] = round(el, 2)
            break
    rec["last_status"] = {"wifi": last[0], "bus": last[1], "dial": last[2]} if last else None
    rec["audio_drops"] = last[0].get("audio_drops") if last else None
    for p in (p4, dial, c6):
        for ts, ln in p.since(marks[p]):
            if FATAL.search(ln):
                # The P4 probes SDIO while the C6 is still powering up after the boot-time rail pulse; those
                # early 0x107 timeouts are expected. The same error later in the run is a real fault.
                if p is p4 and "0x107" in ln and ts - t0 < BOOT_PROBE_WINDOW_S:
                    rec.setdefault("boot_probe_sdio_timeouts", 0)
                    rec["boot_probe_sdio_timeouts"] += 1
                    continue
                rec["fatal"].append(f"{p.label}: {ln[:160]}")
    for _, ln in p4.since(marks[p4]):
        if ln.startswith("RTL_RESET_REASON"):
            rec["reset_reason"] = ln
        if ln.startswith(("RTL_WIFI_BOOT_SPLASH", "RTL_WIFI_BOOT_STATUS")):
            rec.setdefault("boot_lines", []).append(ln[:200])
    rec["c6_bytes"] = sum(len(ln) + 1 for _, ln in c6.since(marks[c6]))
    # A ROM banner on the C6 UART is the only proof the C6 itself restarted this cycle.
    rec["c6_rom_banners"] = sum(1 for _, ln in c6.since(marks[c6]) if ROM_BANNER.search(ln))
    rec["dial_linked"] = any("LINKED" in ln for _, ln in dial.since(marks[dial]))
    rec["dial_rf"] = dial_rf(dial)
    if recover and "orcdial" not in rec["milestones"]:
        rec["recovery"] = recovery_probe(p4, key_hex, events, n)
    rec["end_mono"] = round(time.monotonic(), 3)
    need = {"wifi", "hosted", "capture", "orcdial", "audio_advancing"}
    rec["pass"] = need <= set(rec["milestones"]) and not rec["fatal"]
    return rec


def recovery_probe(p4, key_hex, events, n, wait=30.0):
    """After a failed OrcDial connect: does it recover by itself, or only on an explicit CONNECT?"""
    out = {"self": None, "explicit": None}
    t0 = time.monotonic()
    while time.monotonic() - t0 < wait:
        try:
            polled = poll_status(p4)
        except RuntimeError:
            polled = None  # the port can disappear across a reboot
        if polled and polled[2].get("connection") == "Connected":
            out["self"] = round(time.monotonic() - t0, 1)
            events(f"CYCLE {n} RECOVERED_BY_ITSELF after {out['self']}s")
            return out
        time.sleep(1.0)
    authenticate(p4, key_hex)
    command(p4, "RTL_ORCDIAL_CONNECT", r"^RTL_ORCDIAL_", 5)
    t1 = time.monotonic()
    while time.monotonic() - t1 < wait:
        try:
            polled = poll_status(p4)
        except RuntimeError:
            polled = None
        if polled and polled[2].get("connection") == "Connected":
            out["explicit"] = round(time.monotonic() - t1, 1)
            events(f"CYCLE {n} RECOVERED_BY_EXPLICIT_CONNECT after {out['explicit']}s")
            return out
        time.sleep(1.0)
    events(f"CYCLE {n} NOT_RECOVERED")
    return out


def preflight(p4, key_hex, events, state):
    authenticate(p4, key_hex)
    state["splash_gate_before"] = command(p4, "RTL_SPLASH_GATE STATUS", r"^RTL_SPLASH_GATE enabled=[01]$", 5)
    if command(p4, "RTL_SPLASH_GATE OFF", r"^RTL_SPLASH_GATE enabled=0$", 5) is None:
        raise RuntimeError("could not disable splash gate")
    for act in ("WIFI_POWER 1", "WIFI_BOOT 1", "AUTO_START 1"):
        if command(p4, f"RTL_UI ACTION SETTINGS {act}", r"^RTL_UI_ACTION_(OK|INVALID)", 20) != "RTL_UI_ACTION_OK":
            raise RuntimeError(f"settings action failed: {act}")
        events(f"PREFLIGHT {act} ok")
    wifi = command(p4, "RTL_WIFI_STATUS", r"^RTL_WIFI_STATUS ", 5)
    # The OrcDial status reads all zeros until the staged bridge is up (about 13 s after a reset).
    dial = None
    for _ in range(60):
        dial = command(p4, "RTL_ORCDIAL_STATUS", r"^RTL_ORCDIAL_STATUS ", 5)
        if dial and "bridge=1" in dial:
            break
        time.sleep(1.0)
    w, d = parse_kv(wifi or ""), parse_kv(dial or "")
    state["preflight"] = {"wifi": w, "dial": d}
    if w.get("auto_connect") != "1":
        raise RuntimeError("boot Wi-Fi did not enable")
    if d.get("trust") != "1" or d.get("boot") != "1":
        raise RuntimeError(f"OrcDial not trusted with boot enabled: {dial}")
    events("PREFLIGHT boot wifi=1 orcdial trust=1 boot=1")


def selftest():
    assert parse_kv("a=1 b=Connected c=x_y") == {"a": "1", "b": "Connected", "c": "x_y"}
    ok = evaluate_status({"connected": "1", "capture_state": "3"},
                         {"hosted_ready": "1", "link_failed": "0"},
                         {"connection": "Connected", "trust": "1"})
    assert all(ok.values()), ok
    bad = evaluate_status({"connected": "0", "capture_state": "3"},
                          {"hosted_ready": "1", "link_failed": "1"},
                          {"connection": "Searching", "trust": "1"})
    assert not any(bad[k] for k in ("wifi", "hosted", "orcdial")) and bad["capture"]
    assert FATAL.search("E (123) sdio: cmd53 timeout 0x107")
    assert not FATAL.search("RTL_WIFI_BUS_STATUS wlan_enable=1 link_failed=0")
    print("selftest ok")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cycles", type=int, default=20)
    ap.add_argument("--p4", default="COM17")
    ap.add_argument("--dial", default="COM14")
    ap.add_argument("--c6", default="COM20", help="C6 UART port, or 'none' when the downloader is unplugged")
    ap.add_argument("--key", default=str(DEFAULT_KEY))
    ap.add_argument("--settle", type=float, default=90.0, help="seconds allowed per cycle")
    ap.add_argument("--dwell", type=float, default=8.0, help="seconds between cycles")
    ap.add_argument("--dwell-range", type=float, nargs=2, metavar=("MIN", "MAX"), default=None,
                    help="random dwell (uniform) so resets land at different phases of the Dial channel sweep")
    ap.add_argument("--seed", type=int, default=None, help="seed for random dwell (printed so a run can be replayed)")
    ap.add_argument("--mode", choices=("p4", "c6pulse", "c6off_reset", "c6rts", "dial"), default="p4",
                    help="p4: RTL_RESET; c6pulse: cut the C6 rail only; c6off_reset: cut the C6 rail then RTL_RESET "
                         "(those two need the -C6FaultTest build); c6rts: reset the P4, then pulse the C6 EN line through the downloader about 1 s later "
                         "(needs the adapter attached); dial: restart only the Dial (ORCDIAL_RESTART)")
    ap.add_argument("--c6-off-ms", type=int, default=1500, help="how long the C6 rail stays off in the C6 modes")
    ap.add_argument("--recover", action="store_true",
                    help="after a failed OrcDial connect, watch for self-recovery then try an explicit CONNECT")
    ap.add_argument("--out", default=None)
    ap.add_argument("--force-gate-on", action="store_true",
                    help="always leave the splash button gate ON at the end, whatever it was before")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if serial is None:
        sys.exit("pyserial is required")
    key_hex = Path(a.key).read_text().strip()
    if not re.fullmatch(r"[0-9A-Fa-f]{64}", key_hex):
        sys.exit("pairing key must be 64 hex characters")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    out = Path(a.out) if a.out else Path("boot-cycles") / stamp
    out.mkdir(parents=True)
    ev_file = (out / "events.log").open("w", encoding="utf-8")

    def events(msg):
        line = f"{datetime.now(timezone.utc).isoformat()} mono={time.monotonic():.3f} {msg}"
        ev_file.write(line + "\n")
        ev_file.flush()
        print(line, flush=True)

    p4 = Port("p4", a.p4, 921600, out, events)
    dial = Port("dial", a.dial, 115200, out, events)
    c6 = NullPort() if a.c6.lower() == "none" else Port("c6", a.c6, 115200, out, events)
    ports = (p4, dial, c6)
    for p in ports:
        if isinstance(p, Port):
            p.t.start()
    state, results = {}, []
    try:
        time.sleep(2)
        missing = [p.label for p in ports if isinstance(p, Port) and p.s is None]
        if missing:
            raise RuntimeError(f"required ports not open: {missing}")
        preflight(p4, key_hex, events, state)
        dial_send(dial, "ORCDIAL_RF TRACE 1")
        seed = a.seed if a.seed is not None else secrets.randbits(32)
        rng = random.Random(seed)
        state["args"] = {"mode": a.mode, "c6_off_ms": a.c6_off_ms, "seed": seed, "dwell_range": a.dwell_range,
                         "settle": a.settle, "recover": a.recover}
        events(f"RUN mode={a.mode} seed={seed}")
        for n in range(1, a.cycles + 1):
            rec = run_cycle(n, p4, dial, c6, key_hex, a.settle, events, a.mode, a.c6_off_ms, a.recover)
            results.append(rec)
            events(f"CYCLE {n} {'PASS' if rec['pass'] else 'FAIL'} {json.dumps(rec['milestones'])} "
                   f"fatal={len(rec['fatal'])} c6_rom_banners={rec.get('c6_rom_banners')}")
            (out / "report.json").write_text(json.dumps({"state": state, "cycles": results}, indent=2))
            time.sleep(rng.uniform(*a.dwell_range) if a.dwell_range else a.dwell)
    finally:
        try:
            if a.force_gate_on or state.get("splash_gate_before", "").endswith("enabled=1"):
                authenticate(p4, key_hex)
                command(p4, "RTL_SPLASH_GATE ON", r"^RTL_SPLASH_GATE enabled=1$", 5)
                events("SPLASH_GATE restored ON")
        except Exception as err:
            events(f"SPLASH_GATE restore failed, send RTL_SPLASH_GATE ON: {err}")
        passed = sum(r["pass"] for r in results)
        summary = {"requested": a.cycles, "completed": len(results), "passed": passed,
                   "orcdial_connected": sum("orcdial" in r["milestones"] for r in results),
                   "wifi_connected": sum("wifi" in r["milestones"] for r in results),
                   "c6_restarts_confirmed": sum(bool(r.get("c6_rom_banners")) for r in results)}
        events("SUMMARY " + json.dumps(summary))
        (out / "report.json").write_text(json.dumps({"summary": summary, "state": state, "cycles": results}, indent=2))
        for p in ports:
            p.close()
        ev_file.close()
        print(f"Evidence: {out.resolve()}")


if __name__ == "__main__":
    main()
