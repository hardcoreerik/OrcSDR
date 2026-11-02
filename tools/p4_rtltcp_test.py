#!/usr/bin/env python3
"""Acceptance run for the esp-rtl-tcp server (docs/hardware/ESP_RTL_TCP_DESIGN.md), listen-only mode.

  python tools/p4_rtltcp_test.py <device-ip> [--seconds 10]

Prerequisites: `RTL_TCP ON` was sent over the serial console (authenticated), the web server is on (read-only status is read from /api/v1/rtltcp),
and a dongle is streaming. Standard library only. Every check prints PASS or FAIL; the exit code is the number of failures.
"""
import argparse
import http.client
import json
import socket
import struct
import sys
import time

fails = 0


def check(name, ok, detail=""):
    global fails
    print(("PASS " if ok else "FAIL ") + name + ((" - " + str(detail)) if detail and not ok else ""))
    fails += 0 if ok else 1


def status(ip):
    c = http.client.HTTPConnection(ip, 80, timeout=6)
    c.request("GET", "/api/v1/rtltcp", headers={"Host": ip})
    j = json.loads(c.getresponse().read())
    c.close()
    out = {}
    for rec in j["records"]:
        out.update(rec)
    return out


def recv_exact(s, n, timeout=5):
    s.settimeout(timeout)
    data = b""
    while len(data) < n:
        chunk = s.recv(n - len(data))
        if not chunk:
            raise EOFError("closed after %d of %d bytes" % (len(data), n))
        data += chunk
    return data


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ip")
    ap.add_argument("--seconds", type=float, default=10)
    a = ap.parse_args()

    st = status(a.ip)
    check("server is running in listen-only mode", st.get("enabled") == 1 and st.get("running") == 1 and st.get("mode") == "listen-only", st)

    s = socket.create_connection((a.ip, 1234), timeout=5)
    hdr = recv_exact(s, 12)
    magic, tuner, gains = struct.unpack(">4sII", hdr)
    check("header is RTL0 + tuner type + gain count", magic == b"RTL0" and 0 <= tuner <= 6 and 0 < gains <= 32, (magic, tuner, gains))
    print("tuner type %d, gain count %d" % (tuner, gains))

    t0 = time.time()
    total = 0
    sample = b""
    while time.time() - t0 < a.seconds:
        chunk = s.recv(65536)
        if not chunk:
            break
        total += len(chunk)
        if len(sample) < 65536:
            sample += chunk
    dt = time.time() - t0
    rate_sps = total / 2 / dt
    st = status(a.ip)
    print("received %d bytes in %.1f s = %.0f S/s; session_rate %s; blocks_dropped %s" % (total, dt, rate_sps, st.get("session_rate"), st.get("blocks_dropped")))
    want = st.get("session_rate") or 0
    check("sample rate matches the receiver (within 3 %)", want > 0 and abs(rate_sps - want) < 0.03 * want, (rate_sps, want))
    check("no RTL0 bytes appear after the header", b"RTL0" not in sample)
    mean_i = sum(sample[0::2]) / len(sample[0::2])
    check("samples look like CU8 (mean near 127)", 100 < mean_i < 155, mean_i)

    s.sendall(struct.pack(">BI", 0x01, 100000000))  # set centre: ignored in listen-only
    s.sendall(struct.pack(">BI", 0x06, 1))           # IF gain: unsupported
    s.sendall(struct.pack(">BI", 0x7F, 1))           # unknown
    s.sendall(b"\x02\x00")                           # a split command: 2 bytes now, 3 later
    time.sleep(0.3)
    s.sendall(b"\x24\x9f\x00")
    time.sleep(1.0)
    st2 = status(a.ip)
    check("a tuning command is ignored and counted", st2["commands_ignored"] >= 2, st2)
    check("an unsupported command is counted", st2["commands_unsupported"] >= 1, st2)
    check("an unknown command is counted", st2["commands_unknown"] >= 1, st2)
    check("nothing was applied in listen-only mode", st2["commands_applied"] == 0 and st2["commands_rejected"] == 0, st2)
    check("the stream kept flowing after the commands", len(s.recv(65536)) > 0)

    # A second client waits in the backlog and gets its header only after the first leaves.
    s2 = socket.create_connection((a.ip, 1234), timeout=5)
    s2.settimeout(1.5)
    try:
        got = s2.recv(12)
    except socket.timeout:
        got = b""
    check("a second client gets nothing while the first is connected", got == b"", got)
    s.close()
    time.sleep(1.0)
    try:
        hdr2 = recv_exact(s2, 12, timeout=6)
    except Exception as e:  # noqa: BLE001
        hdr2 = b""
        print(e)
    check("the waiting client is served after the first leaves", hdr2[:4] == b"RTL0", hdr2)
    s2.close()
    time.sleep(1.0)
    st3 = status(a.ip)
    check("the session end is recorded as client_closed", st3.get("last_end") == "client_closed", st3)
    check("two clients were counted", st3["clients_total"] >= 2, st3)
    print("%d failed" % fails)
    return fails


if __name__ == "__main__":
    sys.exit(main())
