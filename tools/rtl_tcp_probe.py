#!/usr/bin/env python3
"""Probe a real rtl_tcp server: protocol behaviour, rates, band scan and IQ capture.

A client only. It sends rtl_tcp commands and reads; it changes nothing on the server except
the tuner state those commands set (rtl_tcp has no acknowledgement and keeps that state across
reconnects, so run `systemctl restart rtl-tcp` on a lab server afterwards, or pass --reset-gain
to leave the server in auto gain).

  python tools/rtl_tcp_probe.py HOST[:PORT] header
  python tools/rtl_tcp_probe.py HOST rates 240000,960000,1440000,2400000 --secs 12
  python tools/rtl_tcp_probe.py HOST soak 1440000 --secs 600
  python tools/rtl_tcp_probe.py HOST stall --secs 25
  python tools/rtl_tcp_probe.py HOST second
  python tools/rtl_tcp_probe.py HOST badcmd
  python tools/rtl_tcp_probe.py HOST scan --lo 88e6 --hi 108e6 --gain 250 --save-iq OUT_DIR
  python tools/rtl_tcp_probe.py HOST capture --freq 96.1e6 --rate 960000 --secs 5 --gain 250 --out x.cu8
  python tools/rtl_tcp_probe.py HOST retune --from 99.1e6 --to 98.5e6 --rate 960000 --repeat 3
Needs numpy for scan/capture analysis. Prints one JSON line per measurement.
"""
import argparse, json, socket, struct, sys, time

DEFAULT_PORT = 1234


def cmd(op, val):
    return bytes([op]) + struct.pack(">I", val & 0xFFFFFFFF)


class Server:
    def __init__(self, host, port):
        self.host, self.port = host, port

    def connect(self, timeout=10.0):
        t0 = time.time()
        s = socket.create_connection((self.host, self.port), timeout=timeout)
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        return s, t0

    @staticmethod
    def header(s, t0, wait=10.0):
        s.settimeout(wait)
        buf = b""
        try:
            while len(buf) < 12:
                d = s.recv(12 - len(buf))
                if not d:
                    return None, None
                buf += d
        except socket.timeout:
            return None, None
        return buf, (time.time() - t0) * 1000

    def open(self, rate=None, freq=None, gain=None):
        """gain: None = leave, 'auto' = tuner AGC, int = manual tenths of dB."""
        s, t0 = self.connect()
        h, ms = self.header(s, t0)
        if h is None:
            raise RuntimeError("no greeting (server busy or not rtl_tcp)")
        if rate: s.sendall(cmd(0x02, rate))
        if freq: s.sendall(cmd(0x01, int(freq)))
        if gain == "auto": s.sendall(cmd(0x03, 0))
        elif gain is not None: s.sendall(cmd(0x03, 1)); s.sendall(cmd(0x04, int(gain)))
        return s, h, ms


def out(**k):
    print(json.dumps(k), flush=True)


def drain(s, secs, discard=0.0):
    s.settimeout(2.0)
    t0 = time.time(); got = 0; start = None
    while time.time() - t0 < secs:
        try:
            d = s.recv(262144)
        except socket.timeout:
            continue
        if not d:
            break
        if time.time() - t0 >= discard:
            start = start or time.time()
            got += len(d)
    return got, (time.time() - start) if start else 0.0


def cmd_header(srv, a):
    for gap in (0.0, 0.5, 1.0, 1.5, 2.5, 4.0):
        s, t0 = srv.connect(); h, ms = srv.header(s, t0); s.close()
        out(test="header", gap_s=gap, header=h.hex() if h else None, header_ms=ms)
        time.sleep(gap)


def cmd_rates(srv, a):
    for r in [int(x) for x in a.rates.split(",")]:
        s, h, ms = srv.open(rate=r)
        got, el = drain(s, a.secs, discard=2.0); s.close()
        exp = 2 * r
        out(test="rate", rate=r, header_ms=ms, bytes_per_s=round(got / el) if el else 0, expected=exp,
            pct=round(100 * got / el / exp, 1) if el else 0, mbit_s=round(got * 8 / el / 1e6, 2) if el else 0)
        time.sleep(3)


def cmd_soak(srv, a):
    rate = int(a.rate); exp = 2 * rate
    s, h, ms = srv.open(rate=rate); s.settimeout(3.0)
    start = time.time(); ws = start; wb = 0; total = 0; wins = []; stalls = 0
    while time.time() - start < a.secs:
        try:
            d = s.recv(262144)
        except socket.timeout:
            stalls += 1; continue
        if not d:
            out(test="soak_closed", at_s=round(time.time() - start)); break
        now = time.time(); total += len(d); wb += len(d)
        if now - ws >= 10:
            wins.append(round(100 * wb / (now - ws) / exp, 1)); ws = now; wb = 0
    el = time.time() - start; s.close()
    out(test="soak", rate=rate, seconds=round(el), avg_pct=round(100 * total / el / exp, 2),
        min_10s_window_pct=min(wins) if wins else None, windows_below_95=sum(1 for w in wins if w < 95),
        read_timeouts_3s=stalls)


def cmd_stall(srv, a):
    rate = 240000
    s, h, ms = srv.open(rate=rate); drain(s, 3.0)
    time.sleep(a.secs)
    got, el = drain(s, 20.0)
    out(test="stall", stalled_s=a.secs, backlog_s=round(max(0.0, got - el * 2 * rate) / (2 * rate), 2))
    s.close(); time.sleep(3)
    s2, h, ms2 = srv.open(rate=rate); g2, e2 = drain(s2, 3.0); s2.close()
    out(test="reconnect_after_stall", stale_s=round(max(0.0, g2 - e2 * 2 * rate) / (2 * rate), 2), header_ms=ms2)


def cmd_second(srv, a):
    s1, h, ms = srv.open()
    try:
        b = socket.create_connection((srv.host, srv.port), timeout=5.0)
    except Exception as e:
        out(test="second", tcp_connected=False, error=str(e)); s1.close(); return
    b.settimeout(4.0)
    try:
        d = b.recv(12); early = d.hex() if d else "closed"
    except socket.timeout:
        early = None
    s1.close(); t = time.time(); b.settimeout(10.0)
    try:
        d = b.recv(12); late = round((time.time() - t) * 1000) if d else None
    except socket.timeout:
        late = None
    b.close()
    out(test="second_client", tcp_connected=True, header_while_first_connected=early, header_ms_after_first_closed=late)


def cmd_badcmd(srv, a):
    s, h, ms = srv.open(rate=240000)
    base = drain(s, 3.0, 1.0)
    s.sendall(cmd(0x7F, 1234)); unk = drain(s, 3.0, 0.5)
    s.sendall(cmd(0x02, 480000)); bad = drain(s, 4.0, 1.5)
    s.sendall(cmd(0x02, 240000)); back = drain(s, 3.0, 1.5); s.close()
    r = lambda t: round(t[0] / t[1]) if t[1] else 0
    out(test="badcmd", bytes_per_s_240k=r(base), after_unknown_opcode=r(unk), after_invalid_rate_480000=r(bad),
        after_back_to_240000=r(back))


def capture_iq(srv, freq, rate, secs, gain, settle=0.8):
    s, h, ms = srv.open(rate=rate, freq=freq, gain=gain)
    time.sleep(settle); s.settimeout(2)
    t0 = time.time()
    while time.time() - t0 < 0.4:
        try: s.recv(262144)
        except socket.timeout: break
    need = int(rate * 2 * secs); got = bytearray()
    while len(got) < need:
        d = s.recv(262144)
        if not d: break
        got += d
    s.close(); time.sleep(2.0)
    return bytes(got[: need - need % 2])


def spectrum(iq_u8, rate, n=8192):
    import numpy as np
    x = np.frombuffer(iq_u8, dtype=np.uint8).astype(np.float32) - 127.5
    iq = x[0::2] + 1j * x[1::2]
    seg = iq[: len(iq) // n * n].reshape(-1, n) * np.hanning(n)
    p = (np.abs(np.fft.fftshift(np.fft.fft(seg, axis=1))) ** 2).mean(axis=0) + 1e-9
    clip = float(np.mean((np.abs(iq.real) > 125) | (np.abs(iq.imag) > 125))) * 100
    return 10 * np.log10(p), clip, float(np.sqrt(np.mean(np.abs(iq) ** 2)))


def cmd_capture(srv, a):
    import numpy as np
    gain = "auto" if a.gain == "auto" else int(a.gain)
    data = capture_iq(srv, a.freq, a.rate, a.secs, gain)
    if a.out:
        open(a.out, "wb").write(data)
    db, clip, rms = spectrum(data, a.rate)
    pk = int(np.argmax(db)); n = len(db)
    out(test="capture", freq=a.freq, rate=a.rate, gain=a.gain, bytes=len(data), file=a.out,
        peak_over_median_db=round(float(db[pk] - np.median(db)), 1),
        peak_offset_khz=round((pk - n // 2) * a.rate / n / 1e3, 1), rms=round(rms, 1), clip_pct=round(clip, 2))


def cmd_retune(srv, a):
    """Stale samples after a retune. rtl_tcp never acknowledges a command and the server buffers USB data, so samples
    already in flight still carry the old frequency. Needs a strong station at --from (carrier at the centre) that
    --to moves out of the passband. Reports how much sample time arrives after the command before the carrier vanishes."""
    import numpy as np
    rate = a.rate; n = 4096; w = np.hanning(n)
    for _ in range(a.repeat):
        s, h, ms = srv.open(rate=rate, freq=a.from_hz, gain=int(a.gain))
        s.settimeout(3.0); total = 0; carry = b""; ref = []; sent_at = None; n_at_send = 0; changed = None; t_wall = None
        t0 = time.time()
        while time.time() - t0 < 12 and changed is None:
            try: d = s.recv(262144)
            except socket.timeout: continue
            if not d: break
            carry += d; usable = len(carry) - len(carry) % (2 * n)
            for off in range(0, usable, 2 * n):
                blk = np.frombuffer(carry[off:off + 2 * n], dtype=np.uint8).astype(np.float32) - 127.5
                iq = (blk[0::2] + 1j * blk[1::2]) * w
                sp = np.abs(np.fft.fftshift(np.fft.fft(iq))) ** 2
                f = (np.arange(n) - n // 2) * rate / n; band = float(sp[np.abs(f) < 60e3].mean())
                if sent_at is None:
                    ref.append(band)
                    if len(ref) == 40:   # carrier level established (about 0.17 s at 960 kS/s): retune now
                        sent_at = time.time(); n_at_send = total + off // 2; s.sendall(cmd(0x01, int(a.to_hz)))
                        refp = float(np.median(ref))
                elif changed is None and band < refp / 10 ** 1.0:   # 10 dB below the carrier level
                    changed = total + off // 2; t_wall = time.time() - sent_at
            total += usable // 2; carry = carry[usable:]
        s.close(); time.sleep(2.0)
        if changed is None:
            out(test="retune", rate=rate, result="carrier did not vanish (no strong station at --from, or retune ignored)")
        else:
            out(test="retune", rate=rate, from_mhz=a.from_hz / 1e6, to_mhz=a.to_hz / 1e6,
                stale_sample_seconds=round((changed - n_at_send) / rate, 3), wall_seconds_to_change=round(t_wall, 3))


def cmd_scan(srv, a):
    import numpy as np, os
    rate = 2400000; step = 2000000; res = {}
    gain = "auto" if a.gain == "auto" else int(a.gain)
    if a.save_iq: os.makedirs(a.save_iq, exist_ok=True)
    c = int(a.lo) + 1000000
    while c <= int(a.hi) + 1000000:
        data = capture_iq(srv, c, rate, 0.5, gain)
        if a.save_iq:
            open(os.path.join(a.save_iq, f"scan_{c/1e6:.1f}MHz_{rate}.cu8"), "wb").write(data)
        db, clip, rms = spectrum(data, rate)
        n = len(db)
        out(test="scan_step", center_mhz=c / 1e6, clip_pct=round(clip, 2), rms=round(rms, 1), floor_db=round(float(np.median(db)), 1))
        for i, v in enumerate(db):
            f = c + (i - n // 2) * rate / n
            if a.lo <= f <= a.hi:
                k = round(f / 1e5); res[k] = max(res.get(k, -1e9), float(v))
        c += step
    floor = float(np.median(list(res.values())))
    top = sorted(res.items(), key=lambda kv: -kv[1])[:12]
    out(test="scan_top", floor_db=round(floor, 1), channels=[{"mhz": k / 10, "db_over_median": round(v - floor, 1)} for k, v in top])
    for want in (a.want or []):
        k = round(float(want) * 10)
        v = res.get(k, floor)
        out(test="scan_requested", mhz=float(want), db_over_median=round(v - floor, 1),
            rank=1 + sum(1 for x in res.values() if x > v), of=len(res))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("target"); sub = p.add_subparsers(dest="test", required=True)
    sub.add_parser("header"); sub.add_parser("second"); sub.add_parser("badcmd")
    r = sub.add_parser("rates"); r.add_argument("rates"); r.add_argument("--secs", type=float, default=12)
    k = sub.add_parser("soak"); k.add_argument("rate"); k.add_argument("--secs", type=float, default=600)
    st = sub.add_parser("stall"); st.add_argument("--secs", type=float, default=25)
    c = sub.add_parser("capture"); c.add_argument("--freq", type=float, required=True); c.add_argument("--rate", type=int, default=960000)
    c.add_argument("--secs", type=float, default=5); c.add_argument("--gain", default="250"); c.add_argument("--out")
    rt = sub.add_parser("retune"); rt.add_argument("--from", dest="from_hz", type=float, required=True)
    rt.add_argument("--to", dest="to_hz", type=float, required=True); rt.add_argument("--rate", type=int, default=960000)
    rt.add_argument("--gain", default="150"); rt.add_argument("--repeat", type=int, default=3)
    sc = sub.add_parser("scan"); sc.add_argument("--lo", type=float, default=88e6); sc.add_argument("--hi", type=float, default=108e6)
    sc.add_argument("--gain", default="250"); sc.add_argument("--save-iq"); sc.add_argument("--want", nargs="*")
    a = p.parse_args()
    host, _, port = a.target.partition(":")
    srv = Server(host, int(port or DEFAULT_PORT))
    {"header": cmd_header, "rates": cmd_rates, "soak": cmd_soak, "stall": cmd_stall, "second": cmd_second,
     "badcmd": cmd_badcmd, "capture": cmd_capture, "scan": cmd_scan, "retune": cmd_retune}[a.test](srv, a)


if __name__ == "__main__":
    sys.exit(main())
