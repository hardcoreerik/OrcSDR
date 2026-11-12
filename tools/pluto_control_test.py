#!/usr/bin/env python3
"""Proves control of a PlutoSDR through OrcSDR's web API and the esp-pluto-sdr driver, with the Pluto read back independently.

  python tools/pluto_control_test.py <p4-ip> --pluto-ip <pluto-ip> [--password PASSWORD]

Every change is made through the signed web API of the OrcSDR P4 Core board (the same routes the page uses). After each change the test
reads the Pluto's actual state straight from the Pluto's own iiod over the network (READ requests only: nothing is ever written to the Pluto
from this PC, so it cannot disturb the stream the P4 is reading over USB) and compares it with what was asked for and with what the
board's own status (GET /api/v1/pluto) says. It also checks that out-of-range requests are refused and change nothing, and that the
transmit side is never touched (TX attenuation stays at the device's muted boot value). The starting state is restored at the end.

Receive only. The board must be streaming the Pluto as receiver 0. Needs only the standard library. Every check prints PASS or FAIL; the exit
code is the number of failures.
"""
import argparse
import os
import socket
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import p4_web_auth_test as w  # noqa: E402  (the signed-request client)

PHY = "ad9361-phy"
fails = 0


def check(name, ok, detail=""):
    global fails
    print(("PASS " if ok else "FAIL ") + name + ((" - " + str(detail)) if detail and not ok else ""))
    fails += 0 if ok else 1


class Iiod:
    """Read-only access to the Pluto's own iiod (libiio network protocol). Only READ commands are ever sent."""

    def __init__(self, ip):
        self.s = socket.create_connection((ip, 30431), timeout=5)

    def _line(self):
        l = b""
        while not l.endswith(b"\n"):
            c = self.s.recv(1)
            if not c:
                raise IOError("iiod closed the connection")
            l += c
        return l

    def read(self, attr, dev=PHY, chan=None, out=False):
        if chan:
            c = "READ %s %s %s %s" % (dev, "OUTPUT" if out else "INPUT", chan, attr)
        else:
            c = "READ %s %s" % (dev, attr)
        self.s.sendall((c + "\r\n").encode())
        l = self._line()
        while l.strip() == b"":
            l = self._line()
        n = int(l)
        if n <= 0:
            raise IOError("iiod error %d for %s" % (n, c))
        d = b""
        while len(d) < n:
            d += self.s.recv(n - len(d))
        return d.decode(errors="replace").strip()

    def f(self, attr, **kw):
        return float(self.read(attr, **kw).split()[0])


class ConsoleIio:
    """Independent readback through the Pluto's own serial console (debug port), using `iio_attr` ON the Pluto against its local iiod. Needs a
    console that is already logged in (the test never logs in or types a password) and only runs read-only `iio_attr` queries."""

    PROMPT = "root@fishball"

    def __init__(self, port):
        import re
        import serial  # pyserial
        self.re = re
        self.s = serial.Serial()
        self.s.port, self.s.baudrate, self.s.timeout = port, 115200, 0.3
        self.s.dtr = self.s.rts = False
        self.s.open()
        self.s.write(b"\n")
        time.sleep(0.4)
        first = self.s.read(4096)
        if b"login:" in first:
            raise SystemExit("the Pluto console on %s shows a login prompt: log in first (the test does not enter passwords)" % port)

    def _run(self, cmd, timeout=10):
        self.s.reset_input_buffer()
        self.s.write(cmd.encode() + b"\n")
        buf = b""
        end = time.time() + timeout
        while time.time() < end:
            buf += self.s.read(4096)
            if buf.rstrip().endswith(b"# ") or buf.rstrip().endswith(b"#"):
                if len(buf) > len(cmd):
                    break
        text = self.re.sub(rb"\x1b\[[0-9;?]*[ -/]*[@-~]", b"", buf).decode(errors="replace").replace("\r", "")
        lines = [l for l in text.split("\n") if l.strip() and cmd not in l and not l.startswith(self.PROMPT)]
        return lines

    def read(self, attr, dev=PHY, chan=None, out=False):
        if chan:
            cmd = "iio_attr -u local: %s -c %s %s %s" % ("-o" if out else "-i", dev, chan, attr)
        else:
            cmd = "iio_attr -u local: -d %s %s" % (dev, attr)
        lines = []
        for _ in range(3):  # a serial console occasionally drops or mangles a reply; ask again before calling it a failure
            lines = self._run(cmd)
            if lines:
                break
        if not lines:
            raise IOError("no answer from the Pluto console for: " + cmd)
        return lines[-1].strip()  # the value is the last line; the console's echo of a long command can come back garbled above it

    def f(self, attr, **kw):
        return float(self.read(attr, **kw).split()[0])


class BoardView:
    """The same read interface as Iiod, answered from the board's own /api/v1/pluto record (values the driver read back from the device).
    Less independent than a direct read of the Pluto: use it when the Pluto is not reachable on the network. Transmit-side values are not
    available through it, so those checks are skipped."""

    MAP = {
        ("frequency", "altvoltage0"): lambda b: str(b["center_actual_hz"]),
        ("sampling_frequency", "voltage0"): lambda b: str(b["rate_converter"]),
        ("rf_bandwidth", "voltage0"): lambda b: str(b["bandwidth_hz"]),
        ("gain_control_mode", "voltage0"): lambda b: "manual" if b["gain_manual"] else "slow_attack",
        ("hardwaregain", "voltage0"): lambda b: "%.1f dB" % (b["gain_db10"] / 10.0),
        ("xo_correction", None): lambda b: str(b["xo_hz"]),
        ("rf_dc_offset_tracking_en", "voltage0"): lambda b: str(b["dc_tracking_rf"]),
        ("bb_dc_offset_tracking_en", "voltage0"): lambda b: str(b["dc_tracking_bb"]),
        ("quadrature_tracking_en", "voltage0"): lambda b: str(b["quadrature_tracking"]),
    }

    def __init__(self, dev):
        self.dev = dev
        self.independent = False

    def read(self, attr, dev=PHY, chan=None, out=False):
        if out and chan != "altvoltage0":
            raise NotImplementedError("transmit-side value %s not available from the board" % attr)
        fn = self.MAP.get((attr, chan))
        if not fn:
            raise NotImplementedError(attr)
        return fn(board_status(self.dev))

    def f(self, attr, **kw):
        return float(self.read(attr, **kw).split()[0])


def board_status(dev):
    status, j = dev.call("GET", "/api/v1/pluto")
    assert status == 200, (status, j)
    merged = {}
    for rec in j["records"]:  # identity, capabilities, control readback and stream counters are separate records
        merged.update(rec)
    return merged


def wait_for(pred, timeout=4.0, step=0.15):
    end = time.time() + timeout
    last = None
    while time.time() < end:
        try:
            last = pred()
        except Exception as e:  # a transient read failure while the worker is busy
            last = e
        if last is True:
            return True
        time.sleep(step)
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ip", help="the OrcSDR P4 Core board")
    ap.add_argument("--pluto-ip", help="the Pluto's own network address, for an independent readback; without it the board's own record is used")
    ap.add_argument("--pluto-console", help="COM port of the Pluto's debug console (already logged in) for an independent readback without the network")
    ap.add_argument("--password", default="password")
    args = ap.parse_args()

    d = w.Device(args.ip, args.password)
    status, r, s = d.login()
    if not s:
        print("cannot log in (wait a few seconds after a failed attempt, or check --password):", status, r)
        return 1
    if args.pluto_ip:
        iio, independent, source = Iiod(args.pluto_ip), True, "the Pluto's own iiod over the network (independent)"
    elif args.pluto_console:
        iio, independent, source = ConsoleIio(args.pluto_console), True, "iio_attr on the Pluto itself through its debug console (independent)"
    else:
        iio, independent, source = BoardView(d), False, "the board's /api/v1/pluto record (driver readback)"
    print("readback source:", source)

    def post(route, **q):
        target = "/api/v1/%s?%s" % (route, "&".join("%s=%s" % kv for kv in q.items()))
        return s.post(target)

    # ---- before: the board streams the Pluto, TX is muted ----
    st = board_status(d)
    check("the board reports a streaming PlutoSDR", st.get("state") == "STREAMING" and st.get("present") == 1, st.get("state"))
    check("capabilities were read from the device", st.get("caps_valid") == 1 and st.get("tune_max_hz") == 6000000000 and st.get("tune_min_hz") == 70000000, st)
    check("the driver reports hardware TX present but not enabled", st.get("hardware_tx") == 1 and st.get("tx_enabled") == 0, st)
    tx_att0 = tx_lo0 = None
    if independent:
        tx_att0 = iio.f("hardwaregain", chan="voltage0", out=True)
        tx_lo0 = iio.read("frequency", chan="altvoltage1", out=True)
        check("transmit attenuation is at the muted boot value before the test", tx_att0 <= -89.0, tx_att0)

    start = {
        "hz": iio.read("frequency", chan="altvoltage0", out=True),
        "rate_out": st["rate_out"],
        "bw": iio.read("rf_bandwidth", chan="voltage0"),
        "mode": iio.read("gain_control_mode", chan="voltage0"),
        "xo": iio.read("xo_correction"),
    }
    start_hz = int(start["hz"])
    print("starting state:", start, "board:", {k: st[k] for k in ("center_actual_hz", "rate_out", "bandwidth_hz", "gain_db10", "ppm_milli")})

    # ---- tuning: requested vs the synthesizer vs the board ----
    print("-- tuning")
    for hz in (96100000, 162550000, 433920000, 1090000000, 2400000000):
        status, r = post("receivers/tune", index=0, hz=hz)
        check("tune %d Hz accepted" % hz, status == 200 and r.get("ok"), (status, r))
        got = wait_for(lambda: int(iio.read("frequency", chan="altvoltage0", out=True)) == hz)
        actual = int(iio.read("frequency", chan="altvoltage0", out=True))
        check("the Pluto's LO reads %d Hz" % hz, got, "device says %d (error %+d Hz)" % (actual, actual - hz))
        # the board's own readback follows within about a second (one attribute is refreshed per ~120 ms)
        ok_b = wait_for(lambda: board_status(d)["center_actual_hz"] == actual, timeout=3.0)
        b = board_status(d)
        check("the board's status agrees with the device (actual %d, error %+d Hz)" % (b["center_actual_hz"], b["center_error_hz"]), ok_b, b["center_actual_hz"])
    for label, hz in (("below the device minimum (70 MHz)", 50000000), ("above the 32-bit frequency type", 7000000000), ("zero", 0)):
        before = int(iio.read("frequency", chan="altvoltage0", out=True))
        status, r = post("receivers/tune", index=0, hz=hz)
        time.sleep(0.6)
        after = int(iio.read("frequency", chan="altvoltage0", out=True))
        check("tune %s is refused (HTTP %s) and changes nothing" % (label, status), status == 400 and before == after, (status, r, before, after))

    # ---- rate: requested, quantized by the source, converter rate and RF bandwidth on the device ----
    print("-- sample rate")
    for sps, decim in ((960000, 3), (2400000, 1)):
        status, r = post("receivers/rate", index=0, sps=sps)
        applied = r.get("applied_sps") if isinstance(r, dict) else None
        check("rate %d accepted and applied as %s" % (sps, applied), status == 200 and applied == sps, (status, r))
        want_conv = sps * decim
        okc = wait_for(lambda: abs(iio.f("sampling_frequency", chan="voltage0") - want_conv) <= 2, timeout=6.0)
        conv = iio.f("sampling_frequency", chan="voltage0")
        check("the converter runs %d S/s (decimation %d); device says %d" % (want_conv, decim, conv), okc)
        bw = iio.f("rf_bandwidth", chan="voltage0")
        check("the RF bandwidth follows the output rate (80%%: %d Hz); device says %d" % (sps * 8 // 10, bw), abs(bw - sps * 0.8) <= sps * 0.05, bw)
        okb = wait_for(lambda: board_status(d)["rate_out"] == sps, timeout=4.0)
        check("the board's status reports the %d S/s output" % sps, okb, board_status(d)["rate_out"])
        time.sleep(1.8)
        b = board_status(d)
        check("delivered rate is within 2%% of %d (%d)" % (sps, b["out_sps"]), abs(b["out_sps"] - sps) <= sps * 0.02, b["out_sps"])
    status, r = post("receivers/rate", index=0, sps=1000)
    check("a rate the source cannot run is refused", status == 400, (status, r))

    # ---- gain ----
    print("-- gain")
    for tenth in (200, 400, 600):
        status, r = post("tuner/gain", index=0, mode="manual", tenth_db=tenth)
        check("manual gain %.1f dB accepted" % (tenth / 10), status == 200 and r.get("ok"), (status, r))
        okm = wait_for(lambda: iio.read("gain_control_mode", chan="voltage0") == "manual" and abs(iio.f("hardwaregain", chan="voltage0") - tenth / 10) <= 1.0, timeout=4.0)
        check("the device is in manual mode at %.1f dB (reads %s)" % (tenth / 10, iio.read("hardwaregain", chan="voltage0")), okm)
        okb = wait_for(lambda: board_status(d)["gain_manual"] == 1 and abs(board_status(d)["gain_db10"] - tenth) <= 10, timeout=3.0)
        check("the board reports manual gain near %d" % tenth, okb, board_status(d)["gain_db10"])
    g_before = iio.f("hardwaregain", chan="voltage0")
    status, r = post("tuner/gain", index=0, mode="manual", tenth_db=900)
    time.sleep(0.8)
    check("a gain above the device span (90 dB) changes nothing", abs(iio.f("hardwaregain", chan="voltage0") - g_before) <= 0.5, (status, g_before, iio.f("hardwaregain", chan="voltage0")))
    status, r = post("tuner/gain", index=0, mode="auto")
    check("automatic gain accepted", status == 200 and r.get("ok"), (status, r))
    check("the device is back on its AGC", wait_for(lambda: iio.read("gain_control_mode", chan="voltage0") == "slow_attack", timeout=4.0), iio.read("gain_control_mode", chan="voltage0"))

    # ---- RF bandwidth ----
    print("-- RF bandwidth")
    status, r = post("tuner/bandwidth", index=0, hz=1000000)
    check("RF bandwidth 1 MHz accepted", status == 200, (status, r))
    check("the device reports 1 MHz", wait_for(lambda: abs(iio.f("rf_bandwidth", chan="voltage0") - 1000000) <= 50000, timeout=4.0), iio.read("rf_bandwidth", chan="voltage0"))
    status, r = post("tuner/bandwidth", index=0, hz=0)
    check("RF bandwidth back to automatic accepted", status == 200, (status, r))

    # ---- frequency correction (reference oscillator) ----
    print("-- frequency correction")
    for ppm_milli, xo in ((5000, 40000200), (-5000, 39999800), (0, 40000000)):
        status, r = post("tuner/ppm", index=0, ppm_milli=ppm_milli)
        check("correction %+.3f ppm accepted" % (ppm_milli / 1000), status == 200, (status, r))
        okx = wait_for(lambda: int(iio.read("xo_correction")) == xo, timeout=4.0)
        check("the device's reference reads %d Hz (xo_correction %s)" % (xo, iio.read("xo_correction")), okx)
        okb = wait_for(lambda: abs(board_status(d)["ppm_milli"] - ppm_milli) <= 25, timeout=4.0)
        check("the board reports %+d milli-ppm (%s)" % (ppm_milli, board_status(d)["ppm_milli"]), okb)
    status, r = post("tuner/ppm", index=0, ppm_milli=900000)
    time.sleep(0.8)
    check("a correction beyond the device span (900 ppm) changes nothing", int(iio.read("xo_correction")) == 40000000, int(iio.read("xo_correction")))

    # ---- offset trackers ----
    print("-- offset trackers")
    for mask in (0, 7):
        status, r = post("tuner/offsets", index=0, mask=mask)
        check("offset trackers mask %d accepted" % mask, status == 200, (status, r))
        want = [mask & 1, (mask >> 1) & 1, (mask >> 2) & 1]
        okt = wait_for(lambda: [int(iio.read("rf_dc_offset_tracking_en", chan="voltage0")), int(iio.read("bb_dc_offset_tracking_en", chan="voltage0")),
                                int(iio.read("quadrature_tracking_en", chan="voltage0"))] == want, timeout=4.0)
        check("the device's trackers read %s" % want, okt)

    # ---- transmit side untouched, stream healthy ----
    print("-- afterwards")
    if independent:
        tx_att1 = iio.f("hardwaregain", chan="voltage0", out=True)
        check("transmit attenuation unchanged (%.2f dB)" % tx_att1, tx_att1 == tx_att0, (tx_att0, tx_att1))
        check("transmit LO unchanged", iio.read("frequency", chan="altvoltage1", out=True) == tx_lo0)
    else:
        print("note: transmit-side readback needs --pluto-ip; skipped (the driver reports tx_enabled=0)")
    b = board_status(d)
    check("no USB errors during the run (%d)" % b["usb_errors"], b["usb_errors"] == 0, b["usb_errors"])
    check("still streaming", b["state"] == "STREAMING", b["state"])

    # ---- restore ----
    post("receivers/tune", index=0, hz=start_hz)
    post("receivers/rate", index=0, sps=start["rate_out"] or 960000)
    time.sleep(1.0)
    print("-- restored; failures: %d" % fails)
    return fails


if __name__ == "__main__":
    sys.exit(main())
