#!/usr/bin/env python3
"""Automated regression checks on a Tab5 for the code the AIS integration shares with other modes.

  python tools/ais_regression_test.py --port COM17 --out evidence_dir [--pi-host HOST --pi-ssh USER@HOST --pi-service NAME]
                                       [--skip A,B,...]

Tests (each prints PASS / FAIL / INCONCLUSIVE with the measured numbers):
  A  AIS <-> ACARS hand-over on the shared general-VHF stream (USB source), three cycles: the opener closes the other
     module, only one decodes at a time, the ACARS receiver counts blocks and stops counting wrong-rate blocks, and the
     rate override is given back (FM afterwards runs at a normal rate with no IQ drops).
  B  ADS-B decodes frames before and after an AIS visit (USB source; needs aircraft in range, otherwise INCONCLUSIVE).
  C  FM runs normally after AIS visits.
  D  FT8 over rtl_tcp keeps decoding across an AIS visit and across a server restart (which fires the rtl_tcp
     discontinuity hook that AIS also listens to). Needs --pi-ssh and passwordless sudo on the server to restart it.
  E  Every dashboard opens from Home and Home returns (tools/tab5_ui_drive.py sweep), including AIS.

It changes the Tab5's receiver source during the run and RESTORES the source, server and splash-gate setting at the end.
It flashes and reboots nothing. Quiet times and band conditions change what A to D can see; INCONCLUSIVE is reported rather
than guessed.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tab5_ft8 as t  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
RESULTS = []


def kv(line):
    return dict(re.findall(r'(\w+)=("[^"]*"|\S+)', line or ""))


def num(d, key, default=0):
    try:
        return int(float(d.get(key, default)))
    except (TypeError, ValueError):
        return default


class Tab5:
    def __init__(self, port, key):
        self.port, self.key = port, t.find_key(key)
        self.dev = t.Tab5(port)
        self.last = 0.0

    def reopen(self):
        try:
            self.dev.s.close()
        except Exception:
            pass
        time.sleep(1.0)
        self.dev = t.Tab5(self.port)
        self.last = 0.0

    def cmd(self, line, prefix=None, wait=6.0):
        if time.time() - self.last > 2.5:
            self.dev.authenticate(self.key)
        self.dev.s.reset_input_buffer()
        self.dev.send(line)
        self.last = time.time()
        out, end = [], time.time() + wait
        while time.time() < end:
            raw = self.dev.s.readline()
            if not raw:
                continue
            text = raw.decode("utf-8", "replace").rstrip()
            if "AUTH_OK" in text or "PAIR_OK" in text:
                continue
            if prefix is None or prefix in text:
                out.append(text)
                if prefix is not None:
                    end = min(end, time.time() + 0.6)
        return out

    def one(self, line, prefix, wait=6.0):
        r = self.cmd(line, prefix, wait)
        return kv(r[0]) if r else {}

    def open(self, name, settle=0.0):
        r = self.cmd(f"RTL_UI OPEN {name}", "RTL_UI_OPEN", 5)
        if settle:
            time.sleep(settle)
        return bool(r) and "OK" in r[0]

    def screen(self):
        return self.one("RTL_SCREEN_STATUS", "RTL_SCREEN_STATUS").get("active")

    def uptime(self):
        return num(self.one("RTL_HEALTH", "RTL_HEALTH_STATUS", 5), "uptime_ms", -1)

    def ais(self):
        return self.one("RTL_AIS STATUS", "RTL_AIS_STATUS", 6)

    def acars(self):
        return self.one("RTL_ACARS STATUS", "RTL_ACARS_STATUS", 6)

    def adsb(self):
        return self.one("RTL_ADSB STATUS", "RTL_ADSB_STATUS", 5)

    def dsp(self):
        return self.one("RTL_DSP STATS", "RTL_DSP_STATS", 5)

    def tcp(self):
        return self.one("RTL_TCP STATUS", "RTL_TCP mode", 5)

    def set_source(self, mode):
        self.open("HOME", 1)
        self.cmd("RTL_STOP", "RTL_STOP_RESULT", 14)
        time.sleep(3)
        self.cmd(f"RTL_TCP SOURCE {mode}", "RTL_TCP", 4)

    def listen_fm(self):
        self.cmd("RTL_LISTEN FM", "RTL_CAPTURE", 5)


def record(name, verdict, detail):
    RESULTS.append({"test": name, "verdict": verdict, "detail": detail})
    print(f"[{verdict}] {name}: {detail}", flush=True)


def check_alive(dev, before, label):
    now = dev.uptime()
    if now < 0 or now < before:
        record(label, "FAIL", f"the Tab5 rebooted (uptime {before} -> {now})")
        return False
    return True


# ---------------------------------------------------------------- A
def test_a(dev):
    problems, notes = [], []
    dev.set_source("usb")
    dev.listen_fm()
    time.sleep(6)
    up = dev.uptime()
    for cycle in range(1, 4):
        dev.open("AIS", 14)
        a = dev.ais()
        dsp_a = dev.dsp()
        if not (num(a, "running") == 1 and a.get("link") == "receiving" and num(a, "blocks") > 0 and num(a, "lost_samples") == 0):
            problems.append(f"cycle {cycle}: AIS not receiving cleanly ({a.get('link')}, blocks {a.get('blocks')}, lost {a.get('lost_samples')}, "
                            f"loss_events {a.get('loss_events')}, driver overruns {dsp_a.get('driver_overruns')}, "
                            f"pipeline drops {dsp_a.get('iq_pipeline_drops')}, load {dsp_a.get('load_pct')}%)")
        dev.open("ACARS", 20)
        a2 = dev.ais()
        if num(a2, "running") != 0:
            problems.append(f"cycle {cycle}: AIS still running after ACARS opened")
        c1 = dev.acars()
        time.sleep(6)
        c2 = dev.acars()
        if num(c1, "running") != 1:
            problems.append(f"cycle {cycle}: ACARS receiver not running")
        grew = num(c2, "blocks") - num(c1, "blocks")
        wrong = num(c2, "wrong_rate") - num(c1, "wrong_rate")
        if grew <= 0:
            problems.append(f"cycle {cycle}: ACARS blocks did not increase ({c1.get('blocks')} -> {c2.get('blocks')})")
        if wrong > 5:
            problems.append(f"cycle {cycle}: ACARS saw {wrong} wrong-rate blocks in 6 s")
        notes.append(f"c{cycle}: ais blocks {a.get('blocks')}, acars +{grew} blocks, wrong +{wrong}")
        if not check_alive(dev, up, "A"):
            return
    dev.open("AIS", 10)
    dev.open("HOME", 6)
    c = dev.acars()
    a = dev.ais()
    if num(c, "running") != 0 or num(a, "running") != 0:
        problems.append(f"after Home: ACARS running={c.get('running')}, AIS running={a.get('running')}")
    dev.listen_fm()
    dev.open("FM", 12)
    d = dev.dsp()
    rate = num(d, "rate")
    if d.get("band") != "FM" or rate in (240000, 250000) or rate == 0:
        problems.append(f"FM did not return to a normal rate (band {d.get('band')}, rate {rate})")
    if num(d, "iq_pipeline_drops") or num(d, "driver_overruns"):
        problems.append(f"FM shows IQ losses (pipeline drops {d.get('iq_pipeline_drops')}, overruns {d.get('driver_overruns')})")
    notes.append(f"FM after: band {d.get('band')} rate {rate}")
    dev.open("HOME", 1)
    record("A  AIS/ACARS hand-over, 3 cycles", "FAIL" if problems else "PASS", "; ".join(problems or notes))


# ---------------------------------------------------------------- B and C
def adsb_window(dev, seconds):
    dev.open("ADSB", seconds)
    s = dev.adsb()
    return s


def test_b(dev):
    dev.set_source("usb")
    dev.listen_fm()
    time.sleep(6)
    up = dev.uptime()
    before = adsb_window(dev, 80)
    ok_before = num(before, "crc_ok")
    dev.open("AIS", 18)
    dev.open("HOME", 3)
    after = adsb_window(dev, 80)
    ok_after = num(after, "crc_ok")
    dev.open("HOME", 2)
    if not check_alive(dev, up, "B  ADS-B around an AIS visit"):
        return
    detail = (f"before: active={before.get('active')} rate={before.get('effective_sps') or before.get('rate_sps')} frames={before.get('frames')} crc_ok={ok_before} "
              f"aircraft={before.get('aircraft')}; after: active={after.get('active')} rate={after.get('effective_sps') or after.get('rate_sps')} "
              f"frames={after.get('frames')} crc_ok={ok_after} aircraft={after.get('aircraft')}")
    rate_after = num(after, "effective_sps") or num(after, "rate_sps")
    if num(after, "active") != 1 or (rate_after and not 1900000 <= rate_after <= 2200000):
        record("B  ADS-B around an AIS visit", "FAIL", f"ADS-B is not streaming at about 2.048 MS/s after AIS (rate {rate_after}). " + detail)
    elif ok_before == 0 and ok_after == 0:
        record("B  ADS-B around an AIS visit", "INCONCLUSIVE", "streaming correctly but no aircraft decoded either time. " + detail)
    elif ok_before > 0 and ok_after == 0:
        record("B  ADS-B around an AIS visit", "FAIL", "decoded before AIS, nothing after. " + detail)
    else:
        record("B  ADS-B around an AIS visit", "PASS", detail)


def test_c(dev):
    dev.set_source("usb")
    dev.listen_fm()
    time.sleep(6)
    up = dev.uptime()
    dev.open("FM", 12)
    base = dev.dsp()
    for _ in range(3):
        dev.open("AIS", 10)
        dev.open("FM", 8)
    after = dev.dsp()
    scr = dev.screen()
    dev.open("HOME", 1)
    if not check_alive(dev, up, "C  FM around AIS visits"):
        return
    problems = []
    if scr != "fm":
        problems.append(f"screen is {scr}, not fm")
    if after.get("band") != "FM" or num(after, "rate") != num(base, "rate"):
        problems.append(f"band/rate changed: {base.get('band')}/{base.get('rate')} -> {after.get('band')}/{after.get('rate')}")
    if num(after, "iq_pipeline_drops") or num(after, "driver_overruns"):
        problems.append(f"IQ losses: pipeline {after.get('iq_pipeline_drops')}, overruns {after.get('driver_overruns')}")
    detail = (f"rate {base.get('rate')} -> {after.get('rate')}, load {base.get('load_pct')}% -> {after.get('load_pct')}%, "
              f"audio dropped {after.get('audio_dropped_chunks')}, drops {after.get('iq_pipeline_drops')}")
    record("C  FM around AIS visits", "FAIL" if problems else "PASS", "; ".join(problems) or detail)


# ---------------------------------------------------------------- D
def ft8_total(dev):
    r = dev.cmd("FT8 DECODES", "ORC_FT8_DECODES_END", 8)
    return num(kv(r[0]), "total") if r else -1


def ft8_status(dev):
    return dev.one("FT8 STATUS", "ORC_FT8_STATUS", 5)


def pi_restart(args):
    cmd = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=8", args.pi_ssh,
           f"sudo -n systemctl restart {args.pi_service} && sleep 3 && systemctl is-active {args.pi_service}"]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    return r.returncode == 0 and "active" in r.stdout


def test_d(dev, args):
    if not args.pi_ssh:
        record("D  FT8 over rtl_tcp, AIS visit, server restart", "INCONCLUSIVE", "no --pi-ssh given, the server restart step cannot run")
        return
    dev.set_source("remote")
    if args.pi_host:
        dev.cmd(f"RTL_TCP HOST {args.pi_host} {args.pi_port}", "RTL_TCP HOST", 4)
    dev.listen_fm()
    time.sleep(10)
    if dev.tcp().get("state") != "streaming":
        record("D  FT8 over rtl_tcp, AIS visit, server restart", "INCONCLUSIVE", f"the rtl_tcp server is not streaming ({dev.tcp().get('state')})")
        return
    up = dev.uptime()
    dev.open("FT8", 8)
    best, best_total = None, 0
    for band in ("20m", "17m", "15m", "40m", "30m"):
        dev.cmd(f"FT8 BAND {band}", "ORC_FT8", 5)
        time.sleep(35)
        total = ft8_total(dev)
        if total > best_total:
            best, best_total = band, total
        if total >= 3:
            break
    if not best:
        record("D  FT8 over rtl_tcp, AIS visit, server restart", "INCONCLUSIVE", "no FT8 decodes on 20m/17m/15m/40m/30m at this time")
        return
    dev.cmd(f"FT8 BAND {best}", "ORC_FT8", 5)
    time.sleep(5)

    def window(label, seconds, action=None):
        t0 = ft8_total(dev)
        s0 = ft8_status(dev)
        if action:
            action()
        time.sleep(seconds)
        t1 = ft8_total(dev)
        s1 = ft8_status(dev)
        delta = t1 - t0 if t1 >= t0 else t1
        return {"label": label, "decodes": delta, "slots": num(s1, "slots") - num(s0, "slots"), "rx_running": s1.get("rx_running"),
                "band": s1.get("band")}

    phases = [window("baseline", 75)]

    def ais_visit():
        dev.open("AIS", 20)
        dev.open("FT8", 3)
        dev.cmd(f"FT8 BAND {best}", "ORC_FT8", 5)

    phases.append(window("after AIS visit", 75, ais_visit))
    restarted = {"ok": False}

    def restart():
        time.sleep(20)
        restarted["ok"] = pi_restart(args)

    reconnects0 = num(dev.tcp(), "reconnects")
    phases.append(window("across server restart", 100, restart))
    time.sleep(10)
    tcp = dev.tcp()
    ais = dev.ais()
    alive = check_alive(dev, up, "D  FT8 over rtl_tcp, AIS visit, server restart")
    if not alive:
        return
    problems = []
    for p in phases:
        if p["decodes"] <= 0:
            problems.append(f"{p['label']}: no decodes")
    if phases[1]["decodes"] < 0.4 * max(1, phases[0]["decodes"]):
        problems.append(f"decodes after the AIS visit fell to {phases[1]['decodes']} from {phases[0]['decodes']} (band conditions change; check by eye)")
    if not restarted["ok"]:
        problems.append("the server restart did not complete")
    if tcp.get("state") != "streaming":
        problems.append(f"the stream did not recover (state {tcp.get('state')})")
    if num(tcp, "reconnects") <= reconnects0:
        problems.append("no reconnect was counted, so the discontinuity hook was not exercised")
    detail = "band %s; %s; reconnects %s -> %s; AIS discontinuity hook count %s" % (
        best, "; ".join(f"{p['label']}: {p['decodes']} decodes in {p['slots']} slots" for p in phases), reconnects0,
        tcp.get("reconnects"), ais.get("discontinuities"))
    dev.open("HOME", 1)
    record("D  FT8 over rtl_tcp, AIS visit, server restart", "FAIL" if problems else "PASS", "; ".join(problems) + (" | " if problems else "") + detail)


# ---------------------------------------------------------------- E
def test_e(args):
    sweep_out = os.path.join(args.out, "sweep")
    r = subprocess.run([sys.executable, os.path.join(HERE, "tab5_ui_drive.py"), "--port", args.port, "sweep", "--out", sweep_out,
                        "--dwell", "6"], capture_output=True, text=True, timeout=3600)
    lines = [ln for ln in r.stdout.splitlines() if ln.startswith("{")]
    summary = json.loads(lines[-1]) if lines and '"summary"' in lines[-1] else {}
    bad = summary.get("not_ok", ["no summary"])
    record("E  all dashboards open and return Home", "PASS" if not bad and r.returncode == 0 else "FAIL",
           f"{summary.get('dashboards')} dashboards, not ok: {bad or 'none'}")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port", required=True)
    p.add_argument("--key")
    p.add_argument("--out", required=True)
    p.add_argument("--pi-host", help="rtl_tcp server host for test D (default: leave the saved host)")
    p.add_argument("--pi-port", type=int, default=1234)
    p.add_argument("--pi-ssh", help="user@host of the server, for the restart step of test D")
    p.add_argument("--pi-service", default="rtl-tcp.service")
    p.add_argument("--skip", default="")
    a = p.parse_args()
    os.makedirs(a.out, exist_ok=True)
    skip = {s.strip().upper() for s in a.skip.split(",") if s.strip()}
    dev = Tab5(a.port, a.key)
    original = dev.tcp()
    print("original receiver setup:", original.get("mode"), original.get("server"), flush=True)
    dev.cmd("RTL_SPLASH_GATE OFF", "RTL_SPLASH_GATE", 4)
    restore_failures = []

    def restore_step(label, action):   # every step runs even if an earlier one failed, and failures count as test failures
        try:
            action()
        except Exception as exc:
            restore_failures.append(f"{label}: {type(exc).__name__}: {exc}")
            try:
                dev.reopen()
            except Exception:
                pass
    try:
        for name, fn in (("A", lambda: test_a(dev)), ("B", lambda: test_b(dev)), ("C", lambda: test_c(dev)),
                         ("D", lambda: test_d(dev, a))):
            if name not in skip:
                try:
                    fn()
                except Exception as exc:   # a broken test must be visible, never silently dropped
                    record(name, "FAIL", f"test raised {type(exc).__name__}: {exc}")
                    dev.reopen()
        dev.open("HOME", 1)
    finally:
        if "E" not in skip:
            def sweep():
                dev.dev.s.close()
                test_e(a)
                dev.reopen()
            restore_step("dashboard sweep", sweep)
        restore_step("receiver source", lambda: dev.set_source(original.get("mode") or "remote"))
        if original.get("server"):
            host, _, port = original["server"].rpartition(":")
            restore_step("rtl_tcp server", lambda: dev.cmd(f"RTL_TCP HOST {host} {port}", "RTL_TCP HOST", 4))
        restore_step("listen", dev.listen_fm)
        time.sleep(8)

        def verify():
            now = dev.tcp()
            print("restored:", now.get("mode"), now.get("server"), now.get("state"), flush=True)
            if original.get("mode") and now.get("mode") != original.get("mode"):
                raise RuntimeError(f"the source is {now.get('mode')}, it was {original.get('mode')}")
            if original.get("server") and now.get("server") != original.get("server"):
                raise RuntimeError(f"the server is {now.get('server')}, it was {original.get('server')}")
        restore_step("verify restore", verify)
        restore_step("splash gate", lambda: dev.cmd("RTL_SPLASH_GATE ON", "RTL_SPLASH_GATE", 4))
        for problem in restore_failures:
            record("restore", "FAIL", problem)
    with open(os.path.join(a.out, "regression_results.json"), "w") as f:
        json.dump(RESULTS, f, indent=1)
    print("\nSUMMARY")
    for r in RESULTS:
        print(f"  {r['verdict']:<13} {r['test']}")
    return 1 if any(r["verdict"] == "FAIL" for r in RESULTS) else 0


if __name__ == "__main__":
    sys.exit(main())
