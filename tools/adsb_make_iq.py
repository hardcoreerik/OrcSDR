#!/usr/bin/env python3
"""Render synthetic ADS-B (Mode S, 1090 MHz) as 8-bit unsigned IQ at 2.048 MS/s, for proving the ADS-B decoder over rtl_tcp.

  python tools/adsb_make_iq.py --out adsb_2048k.cu8 --seconds 30
  python tools/rtl_tcp_replay_server.py --bind <this PC> --port 1235 --iq adsb_2048k.cu8 --rate 1024000

Every frame is a real DF17 extended squitter with a correct 24-bit CRC: aircraft identification (callsign), airborne position
(CPR even/odd pairs) and airborne velocity. A few DF11 all-call replies are added. Pulses follow the Mode S timing (8 us
preamble with pulses at 0, 1, 3.5 and 4.5 us, then 1 bit per us, pulse position modulation: a 1 is high in the first half
of the microsecond, a 0 in the second half), with per-aircraft amplitude, random start jitter smaller than a sample, and
complex noise. The carrier is at the IQ centre as the receiver sees a 1090 MHz signal tuned to 1090 MHz; each frame has its own
constant carrier phase (the Tab5 decoder takes |I|+|Q|, which only works when the phase is steady across a frame).

The file is at 2.048 MS/s (what the decoder needs). To fit a Wi-Fi link that cannot carry 4.1 MB/s, stream it slowly:
--rate 1024000 on the replay server paces the same samples at half speed (the decoder works on samples, not wall time),
so it proves the decoder and the protocol path, not the real-time bandwidth. Needs numpy. Test location is synthetic.
"""
import argparse
import math

import numpy as np

RATE = 2_048_000
POLY = 0xFFF409


def crc24(bits):
    """Mode S CRC over a list of 0/1 bits (no parity field)."""
    reg = 0
    for b in bits:
        top = (reg >> 23) & 1
        reg = ((reg << 1) & 0xFFFFFF)
        if top ^ b:
            reg ^= POLY
    return reg


def to_bits(value, n):
    return [(value >> (n - 1 - i)) & 1 for i in range(n)]


def df17(icao, me_bits56):
    head = to_bits(17, 5) + to_bits(5, 3) + to_bits(icao, 24) + me_bits56
    return head + to_bits(crc24(head), 24)


def df11(icao):
    head = to_bits(11, 5) + to_bits(5, 3) + to_bits(icao, 24)
    return head + to_bits(crc24(head), 24)


CHARS = "#ABCDEFGHIJKLMNOPQRSTUVWXYZ#####_###############0123456789######"


def identification(icao, callsign):
    cs = callsign.upper().ljust(8)[:8]
    me = to_bits(4, 5) + to_bits(0, 3)
    for c in cs:
        me += to_bits(CHARS.index(c) if c in CHARS else CHARS.index("_"), 6)
    return df17(icao, me)


def nl(lat):
    if abs(lat) >= 87.0:
        return 1
    a = 1 - math.cos(math.pi / 30)
    b = math.cos(math.pi / 180.0 * abs(lat)) ** 2
    return int(math.floor(2 * math.pi / math.acos(1 - a / b)))


def cpr(lat, lon, odd):
    dlat = 360.0 / (59 if odd else 60)
    yz = int(math.floor(2 ** 17 * ((lat % dlat) / dlat) + 0.5)) & 0x1FFFF
    rlat = dlat * (yz / 2 ** 17 + math.floor(lat / dlat))
    n = max(nl(rlat) - (1 if odd else 0), 1)
    dlon = 360.0 / n
    xz = int(math.floor(2 ** 17 * ((lon % dlon) / dlon) + 0.5)) & 0x1FFFF
    return yz, xz


def position(icao, lat, lon, alt_ft, odd):
    n = (alt_ft + 1000) // 25
    alt = ((n >> 4) << 5) | (1 << 4) | (n & 0xF)
    yz, xz = cpr(lat, lon, odd)
    me = to_bits(11, 5) + to_bits(0, 2) + to_bits(0, 1) + to_bits(alt, 12) + to_bits(0, 1) + to_bits(1 if odd else 0, 1)
    me += to_bits(yz, 17) + to_bits(xz, 17)
    return df17(icao, me)


def velocity(icao, east_kt, north_kt, vrate_fpm):
    def comp(v):
        return (1 if v < 0 else 0), min(abs(int(v)) + 1, 1023)
    sew, vew = comp(east_kt)
    sns, vns = comp(north_kt)
    svr = 1 if vrate_fpm < 0 else 0
    vr = min(abs(int(vrate_fpm)) // 64 + 1, 511)
    me = to_bits(19, 5) + to_bits(1, 3) + [0, 0] + to_bits(0, 3)  # TC 19, subtype 1, IC=0, IFR=0, NUC=0
    me += [sew] + to_bits(vew, 10) + [sns] + to_bits(vns, 10) + [0] + [svr] + to_bits(vr, 9) + to_bits(0, 2) + [0] + to_bits(0, 7)
    assert len(me) == 56
    return df17(icao, me)


def render_frame(bits, amplitude, start_us, total_us, rng):
    """Return (first_sample, magnitude samples) for one frame starting start_us microseconds into the file.
    Each sample is the share of its 1/2.048 us interval that a pulse covers, like the integrating front end of a real dongle
    (a hard 0/1 sampling of 0.5 us pulses at 2.048 MS/s flips bits that a real signal would not)."""
    n_us = 8 + len(bits)
    first = int(math.floor(start_us * RATE / 1e6))
    count = int(math.ceil(n_us * RATE / 1e6)) + 3
    cover = np.zeros(count)
    pulses = [(0.0, 0.5), (1.0, 1.5), (3.5, 4.0), (4.5, 5.0)]
    for i, b in enumerate(bits):
        s0 = 8.0 + i + (0.0 if b else 0.5)
        pulses.append((s0, s0 + 0.5))
    for s0, e0 in pulses:
        ss = (start_us + s0) * RATE / 1e6 - first
        ee = (start_us + e0) * RATE / 1e6 - first
        for k in range(int(math.floor(ss)), int(math.ceil(ee))):
            if 0 <= k < count:
                cover[k] += max(0.0, min(ee, k + 1) - max(ss, k))
    return first, cover * amplitude


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--out", required=True)
    p.add_argument("--seconds", type=float, default=30.0)
    p.add_argument("--aircraft", type=int, default=6)
    p.add_argument("--noise", type=float, default=0.02, help="complex noise sigma, 0..1 of full scale")
    p.add_argument("--seed", type=int, default=1090)
    p.add_argument("--min-amp", type=float, default=0.25, help="weakest aircraft amplitude, 0..1 of full scale (the strongest is min + 0.55)")
    p.add_argument("--list", action="store_true", help="print each frame (hex) and exit")
    a = p.parse_args()
    rng = np.random.default_rng(a.seed)
    n = int(a.seconds * RATE)
    sig = np.zeros(n + 4096, dtype=np.complex128)
    planes = []
    for k in range(a.aircraft):
        planes.append(dict(icao=0xA10000 + 0x137 * (k + 1), call=f"ORC{k + 1:04d}", lat=40.0 + 0.3 * k, lon=-100.0 + 0.4 * k,
                           alt=12000 + 2500 * k, amp=a.min_amp + 0.55 * rng.random(), ve=120 + 40 * k, vn=-80 + 30 * k, odd=False))
    frames = 0
    events = []
    for pl in planes:
        t = rng.random() * 0.4
        while t < a.seconds - 0.001:
            kind = rng.choice(["pos", "pos", "id", "vel", "all"])
            if kind == "pos":
                bits = position(pl["icao"], pl["lat"], pl["lon"], pl["alt"], pl["odd"]); pl["odd"] = not pl["odd"]
            elif kind == "id":
                bits = identification(pl["icao"], pl["call"])
            elif kind == "vel":
                bits = velocity(pl["icao"], pl["ve"], pl["vn"], 0)
            else:
                bits = df11(pl["icao"])
            events.append((t, bits, pl["amp"]))
            t += 0.25 + 0.5 * rng.random()
            pl["lat"] += 0.0002; pl["lon"] += 0.0003
    events.sort(key=lambda e: e[0])
    if a.list:
        for t, bits, amp in events:
            v = int("".join(map(str, bits)), 2)
            print(f"{t:8.3f} s  {v:0{len(bits) // 4}X}  amp={amp:.2f}  crc={'ok' if crc24(bits[:-24]) == int(''.join(map(str, bits[-24:])), 2) else 'BAD'}")
        return
    for t, bits, amp in events:
        start_us = t * 1e6 + rng.random()   # sub-sample jitter
        first, samples = render_frame(bits, amp, start_us, 0, rng)
        if first + samples.size >= sig.size:
            continue
        sig[first:first + samples.size] += samples * np.exp(1j * rng.random() * 2 * math.pi)   # one carrier phase per frame
        frames += 1
    iq = sig[:n] + a.noise * (rng.standard_normal(n) + 1j * rng.standard_normal(n))
    out = np.empty(2 * n, dtype=np.uint8)
    out[0::2] = np.clip(np.rint(127.5 + 127.0 * iq.real), 0, 255).astype(np.uint8)
    out[1::2] = np.clip(np.rint(127.5 + 127.0 * iq.imag), 0, 255).astype(np.uint8)
    out.tofile(a.out)
    print(f"wrote {a.out}: {a.seconds:.1f} s at {RATE} S/s, {frames} frames from {len(planes)} aircraft, {out.size / 1e6:.1f} MB")


if __name__ == "__main__":
    main()
