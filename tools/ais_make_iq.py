#!/usr/bin/env python3
"""Deterministic synthetic AIS IQ for bench replay (network only, never RF).

  python tools/ais_make_iq.py --out ais_fleet.cu8            # 60 s, 240 kS/s CU8 centred on 162.000 MHz
  python tools/ais_make_iq.py --out ais_fleet.cu8 --list     # print every transmission and exit

Writes the IQ file plus <out>.json: the SHA-256 of the IQ, every transmission (time, channel, message type, the exact
payload bytes and the field values they were built from) and the same payloads as NMEA AIVDM sentences, so the result
can be checked three independent ways: byte-for-byte against the host decoder (tools/ais_decode_cu8.cpp), field-for-field
against the values below, and by any third-party NMEA AIS decoder.

The encoder here is written from ITU-R M.1371-5 Annex 8 (message field tables) and Annex 2 (HDLC framing: 24-bit
training sequence, 0x7E flags, bit stuffing, CRC-16 FCS, NRZI, GMSK BT 0.4, 9600 bit/s). It shares no code with the
OrcSDR receiver or parser.

Identities are synthetic on purpose: MMSIs use MID 100, which the ITU has not allocated to any country. Stream the
file only with tools/rtl_tcp_replay_server.py --iq (a private TCP substitute for an rtl_tcp server). Do not transmit it.

Not generated: Type 27 (sent on 156.775 / 156.825 MHz, outside this 162 MHz window) and Type 28 (no independent
layout reference is available to this tool).
"""
import argparse
import hashlib
import json
import math
import sys

import numpy as np

RATE = 240000
BAUD = 9600
SPS = RATE // BAUD            # 25 samples per bit
CENTER_HZ = 162000000
CHANNEL_HZ = {1: -25000.0, 2: +25000.0}   # AIS 1 = 161.975 MHz, AIS 2 = 162.025 MHz
DEVIATION_HZ = 2400.0         # h = 0.5 at 9600 bit/s
BT = 0.4


# ---------------------------------------------------------------- message bits (ITU-R M.1371-5 Annex 8)
class Bits:
    def __init__(self):
        self.bits = []

    def u(self, value, width):
        value = int(value)
        if value < 0 or value >= (1 << width):
            raise ValueError(f"{value} does not fit {width} bits")
        self.bits += [(value >> (width - 1 - i)) & 1 for i in range(width)]
        return self

    def s(self, value, width):
        value = int(value)
        if value < -(1 << (width - 1)) or value >= (1 << (width - 1)):
            raise ValueError(f"{value} does not fit signed {width} bits")
        return self.u(value & ((1 << width) - 1), width)

    def text(self, value, chars):
        value = value.upper().ljust(chars, "@")[:chars]
        for ch in value:
            code = ord(ch)
            if 64 <= code <= 95:
                six = code - 64
            elif 32 <= code <= 63:
                six = code
            else:
                raise ValueError(f"character {ch!r} is not in the AIS 6-bit set")
            self.u(six, 6)
        return self

    def bytes(self):
        if len(self.bits) % 8:
            raise ValueError("message is not a whole number of bytes")
        out = bytearray()
        for i in range(0, len(self.bits), 8):
            byte = 0
            for bit in self.bits[i:i + 8]:
                byte = (byte << 1) | bit
            out.append(byte)
        return bytes(out)


def lon_field(deg):   # 1/10000 minute
    return round(deg * 600000)


def lat_field(deg):
    return round(deg * 600000)


def msg_position_a(msg_type, mmsi, lat, lon, sog_kn, cog_deg, heading, nav_status, second):
    b = Bits().u(msg_type, 6).u(0, 2).u(mmsi, 30).u(nav_status, 4).s(0, 8)       # rate of turn 0
    b.u(round(sog_kn * 10), 10).u(1, 1).s(lon_field(lon), 28).s(lat_field(lat), 27)
    b.u(round(cog_deg * 10), 12).u(heading, 9).u(second % 60, 6).u(0, 2).u(0, 3).u(0, 1).u(0, 19)
    assert len(b.bits) == 168
    return b.bytes()


def msg_base_station(mmsi, lat, lon, second):
    b = Bits().u(4, 6).u(0, 2).u(mmsi, 30).u(2026, 14).u(11, 4).u(10, 5).u(12, 5).u((second // 60) % 60, 6).u(second % 60, 6)
    b.u(1, 1).s(lon_field(lon), 28).s(lat_field(lat), 27).u(7, 4).u(0, 10).u(0, 1).u(0, 19)   # EPFD 7 = surveyed
    assert len(b.bits) == 168
    return b.bytes()


def msg_static_voyage(mmsi, imo, callsign, name, ship_type, dims, draught_m, destination):
    b = Bits().u(5, 6).u(0, 2).u(mmsi, 30).u(0, 2).u(imo, 30).text(callsign, 7).text(name, 20).u(ship_type, 8)
    b.u(dims[0], 9).u(dims[1], 9).u(dims[2], 6).u(dims[3], 6).u(1, 4)             # EPFD 1 = GPS
    b.u(11, 4).u(12, 5).u(8, 5).u(30, 6).u(round(draught_m * 10), 8).text(destination, 20).u(0, 1).u(0, 1)
    assert len(b.bits) == 424
    return b.bytes()


def msg_position_b(mmsi, lat, lon, sog_kn, cog_deg, heading, second):
    b = Bits().u(18, 6).u(0, 2).u(mmsi, 30).u(0, 8).u(round(sog_kn * 10), 10).u(1, 1)
    b.s(lon_field(lon), 28).s(lat_field(lat), 27).u(round(cog_deg * 10), 12).u(heading, 9).u(second % 60, 6)
    b.u(0, 2).u(1, 1).u(0, 1).u(1, 1).u(1, 1).u(1, 1).u(0, 1).u(0, 1).u(0b11100000000000000110, 20)   # CS unit, default CS state
    assert len(b.bits) == 168
    return b.bytes()


def msg_aid_to_navigation(mmsi, aid_type, name, lat, lon, virtual, second):
    b = Bits().u(21, 6).u(0, 2).u(mmsi, 30).u(aid_type, 5).text(name, 20).u(1, 1)
    b.s(lon_field(lon), 28).s(lat_field(lat), 27).u(0, 9).u(0, 9).u(0, 6).u(0, 6).u(7, 4).u(second % 60, 6)
    b.u(0, 1).u(0, 8).u(0, 1).u(1 if virtual else 0, 1).u(0, 1).u(0, 1)
    assert len(b.bits) == 272
    return b.bytes()


def msg_static_b_part_a(mmsi, name):
    b = Bits().u(24, 6).u(0, 2).u(mmsi, 30).u(0, 2).text(name, 20)
    assert len(b.bits) == 160
    return b.bytes()


def msg_static_b_part_b(mmsi, ship_type, vendor, callsign, dims):
    b = Bits().u(24, 6).u(0, 2).u(mmsi, 30).u(1, 2).u(ship_type, 8).text(vendor, 7).text(callsign, 7)
    b.u(dims[0], 9).u(dims[1], 9).u(dims[2], 6).u(dims[3], 6).u(1, 4).u(0, 2)
    assert len(b.bits) == 168
    return b.bytes()


# ---------------------------------------------------------------- HDLC / NRZI (Annex 2, 3.2)
def fcs16(data):
    """CRC-16/X-25 (ISO/IEC 13239): reflected polynomial 0x8408, preset 0xFFFF, complemented."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8408 if crc & 1 else crc >> 1
    return crc ^ 0xFFFF


assert fcs16(b"123456789") == 0x906E   # the published check value


def frame_bits(payload):
    """Training sequence, start flag, bit-stuffed data + FCS (each byte least significant bit first), end flag."""
    crc = fcs16(payload)
    body = []
    for byte in bytes(payload) + bytes([crc & 0xFF, crc >> 8]):
        body += [(byte >> i) & 1 for i in range(8)]
    stuffed, ones = [], 0
    for bit in body:
        stuffed.append(bit)
        ones = ones + 1 if bit else 0
        if ones == 5:
            stuffed.append(0)
            ones = 0
    flag = [0, 1, 1, 1, 1, 1, 1, 0]
    training = [0, 1] * 12
    return training + flag + stuffed + flag


def nrzi(bits):
    """A 0 changes the level, a 1 keeps it."""
    level, out = 1, []
    for bit in bits:
        if bit == 0:
            level = -level
        out.append(level)
    return out


# ---------------------------------------------------------------- GMSK
def gaussian_taps():
    sigma = math.sqrt(math.log(2.0)) / (2.0 * math.pi * BT)        # in bit periods
    span = 2 * SPS
    t = np.arange(-span, span + 1) / SPS
    taps = np.exp(-0.5 * (t / sigma) ** 2)
    return taps / taps.sum()


TAPS = gaussian_taps()


def burst_iq(payload, channel, offset_hz, amplitude, phase0):
    levels = np.array(nrzi(frame_bits(payload)), dtype=np.float64)
    lead, tail = 8, 4                                               # ramp-up / ramp-down bit periods (carrier, no data)
    shaped = np.concatenate([np.full(lead * SPS, levels[0]), np.repeat(levels, SPS), np.full(tail * SPS, levels[-1])])
    shaped = np.convolve(shaped, TAPS, mode="same")
    n = shaped.size
    carrier = CHANNEL_HZ[channel] + offset_hz
    phase = phase0 + 2.0 * math.pi * np.cumsum(carrier + DEVIATION_HZ * shaped) / RATE
    envelope = np.ones(n)
    ramp = 4 * SPS
    envelope[:ramp] = 0.5 - 0.5 * np.cos(np.pi * np.arange(ramp) / ramp)
    envelope[-ramp:] = envelope[:ramp][::-1]
    return (amplitude * envelope * np.exp(1j * phase)).astype(np.complex64)


# ---------------------------------------------------------------- NMEA armouring (IEC 61162-1 / ITU-R M.1371 Annex 8)
def aivdm(payload, channel, sequence_id):
    bits = []
    for byte in payload:
        bits += [(byte >> (7 - i)) & 1 for i in range(8)]
    fill = (6 - len(bits) % 6) % 6
    bits += [0] * fill
    chars = ""
    for i in range(0, len(bits), 6):
        v = 0
        for bit in bits[i:i + 6]:
            v = (v << 1) | bit
        chars += chr(v + 48 if v < 40 else v + 56)
    parts = [chars[i:i + 60] for i in range(0, len(chars), 60)]
    out = []
    for index, part in enumerate(parts):
        last = index == len(parts) - 1
        body = "AIVDM,%d,%d,%s,%s,%s,%d" % (len(parts), index + 1, str(sequence_id) if len(parts) > 1 else "",
                                           "A" if channel == 1 else "B", part, fill if last else 0)
        checksum = 0
        for ch in body:
            checksum ^= ord(ch)
        out.append("!%s*%02X" % (body, checksum))
    return out


# ---------------------------------------------------------------- the fleet
ORIGIN = (46.2000, -123.8000)   # open water; only a reference point for the synthetic tracks


def moved(lat, lon, sog_kn, cog_deg, seconds):
    nm = sog_kn * seconds / 3600.0
    dlat = nm * math.cos(math.radians(cog_deg)) / 60.0
    dlon = nm * math.sin(math.radians(cog_deg)) / (60.0 * math.cos(math.radians(lat)))
    return lat + dlat, lon + dlon


def build_schedule(seconds, origin=ORIGIN):
    """Returns transmissions sorted by time. Every station alternates between the two channels."""
    lat0, lon0 = origin
    tx = []

    def add(t, station, msg_type, payload, fields):
        if t + 0.2 < seconds:
            tx.append({"t": t, "station": station, "type": msg_type, "payload": payload, "fields": fields})

    class_a = [  # mmsi, message type, start offset, period, lat, lon, sog, cog, nav status
        (100000001, 1, 0.50, 6.0, lat0 + 0.020, lon0 - 0.060, 12.3, 90.0, 0),
        (100000002, 3, 1.30, 10.0, lat0 - 0.030, lon0 + 0.020, 7.5, 315.0, 0),
        (100000003, 2, 2.10, 10.0, lat0 + 0.050, lon0 + 0.070, 0.0, 0.0, 1),      # at anchor
    ]
    for mmsi, msg_type, start, period, lat, lon, sog, cog, nav in class_a:
        t = start
        while t < seconds:
            la, lo = moved(lat, lon, sog, cog, t)
            la, lo = lat_field(la) / 600000.0, lon_field(lo) / 600000.0
            heading = int(cog) % 360
            add(t, mmsi, msg_type, msg_position_a(msg_type, mmsi, la, lo, sog, cog, heading, nav, int(t)),
                {"mmsi": mmsi, "lat": la, "lon": lo, "sog_kn": sog, "cog_deg": cog, "heading": heading, "nav_status": nav})
            t += period
    # Class B: positions, and static data in two parts.
    b_mmsi, b_lat, b_lon, b_sog, b_cog = 100000011, lat0 - 0.010, lon0 - 0.020, 5.2, 200.0
    t = 3.30
    while t < seconds:
        la, lo = moved(b_lat, b_lon, b_sog, b_cog, t)
        la, lo = lat_field(la) / 600000.0, lon_field(lo) / 600000.0
        add(t, b_mmsi, 18, msg_position_b(b_mmsi, la, lo, b_sog, b_cog, int(b_cog), int(t)),
            {"mmsi": b_mmsi, "lat": la, "lon": lo, "sog_kn": b_sog, "cog_deg": b_cog, "heading": int(b_cog)})
        t += 15.0
    for t in (8.10, 38.10):
        add(t, b_mmsi, 24, msg_static_b_part_a(b_mmsi, "ORCSDR TEST B1"), {"mmsi": b_mmsi, "part": "A", "name": "ORCSDR TEST B1"})
        add(t + 0.40, b_mmsi, 24, msg_static_b_part_b(b_mmsi, 37, "ORCTEST", "TESTB1", (5, 7, 2, 2)),
            {"mmsi": b_mmsi, "part": "B", "ship_type": 37, "callsign": "TESTB1", "vendor": "ORCTEST", "dims": [5, 7, 2, 2]})
    # Base station.
    base = 1000001      # 00 + MID 100 + 0001
    base_lat, base_lon = lat_field(lat0 + 0.080) / 600000.0, lon_field(lon0 - 0.010) / 600000.0
    t = 4.10
    while t < seconds:
        add(t, base, 4, msg_base_station(base, base_lat, base_lon, int(t)), {"mmsi": base, "lat": base_lat, "lon": base_lon})
        t += 10.0
    # Static and voyage data for two Class A vessels (two-slot messages).
    add(5.20, 100000001, 5, msg_static_voyage(100000001, 9100001, "TESTA1", "ORCSDR TEST A1", 70, (90, 30, 10, 10), 6.4, "TEST PORT"),
        {"mmsi": 100000001, "imo": 9100001, "callsign": "TESTA1", "name": "ORCSDR TEST A1", "ship_type": 70,
         "dims": [90, 30, 10, 10], "draught_m": 6.4, "destination": "TEST PORT"})
    add(25.20, 100000002, 5, msg_static_voyage(100000002, 9100002, "TESTA2", "ORCSDR TEST A2", 52, (20, 8, 4, 4), 3.1, "REPLAY BENCH"),
        {"mmsi": 100000002, "imo": 9100002, "callsign": "TESTA2", "name": "ORCSDR TEST A2", "ship_type": 52,
         "dims": [20, 8, 4, 4], "draught_m": 3.1, "destination": "REPLAY BENCH"})
    add(35.20, 100000001, 5, msg_static_voyage(100000001, 9100001, "TESTA1", "ORCSDR TEST A1", 70, (90, 30, 10, 10), 6.4, "TEST PORT"),
        {"mmsi": 100000001, "imo": 9100001, "callsign": "TESTA1", "name": "ORCSDR TEST A1", "ship_type": 70,
         "dims": [90, 30, 10, 10], "draught_m": 6.4, "destination": "TEST PORT"})
    # Aids to navigation: one physical, one virtual (99 + MID 100).
    aids = [(991000001, 14, "TEST BUOY 1", lat0 + 0.010, lon0 + 0.030, False, (12.10, 42.10)),
            (991000002, 30, "TEST VIRTUAL MARK", lat0 - 0.045, lon0 - 0.050, True, (20.10, 50.10))]
    for mmsi, aid_type, name, lat, lon, virtual, times in aids:
        la, lo = lat_field(lat) / 600000.0, lon_field(lon) / 600000.0
        for t in times:
            add(t, mmsi, 21, msg_aid_to_navigation(mmsi, aid_type, name, la, lo, virtual, int(t)),
                {"mmsi": mmsi, "aid_type": aid_type, "name": name, "lat": la, "lon": lo, "virtual": virtual})
    tx.sort(key=lambda item: (item["t"], item["station"]))
    # Per-station RF character, fixed: carrier offset (Hz) and amplitude (0..1 of full scale).
    rf = {100000001: (0.0, 0.42), 100000002: (+200.0, 0.30), 100000003: (-300.0, 0.22), 100000011: (+400.0, 0.18),
          1000001: (-100.0, 0.36), 991000001: (+250.0, 0.26), 991000002: (-150.0, 0.24)}
    turn = {}
    for item in tx:
        n = turn.get(item["station"], 0)
        turn[item["station"]] = n + 1
        item["channel"] = 1 + (n % 2)
        item["offset_hz"], item["amplitude"] = rf[item["station"]]
    return tx


def render(tx, seconds, noise, seed, offset_scale):
    rng = np.random.default_rng(seed)
    total = int(seconds * RATE)
    iq = (noise * (rng.standard_normal(total) + 1j * rng.standard_normal(total))).astype(np.complex64)
    busy = {1: [], 2: []}
    for item in tx:
        start = int(round(item["t"] * RATE))
        burst = burst_iq(item["payload"], item["channel"], item["offset_hz"] * offset_scale, item["amplitude"],
                         rng.uniform(0, 2 * math.pi))
        end = start + burst.size
        if end > total:
            raise SystemExit("a transmission runs past the end of the file")
        for a, b in busy[item["channel"]]:
            if start < b and a < end:
                raise SystemExit(f"two transmissions overlap on channel {item['channel']} at {item['t']:.2f} s")
        busy[item["channel"]].append((start, end))
        iq[start:end] += burst
        item["start_sample"] = start
        item["samples"] = int(burst.size)
    out = np.empty(total * 2, dtype=np.uint8)
    out[0::2] = np.clip(np.rint(127.5 + 127.5 * iq.real), 0, 255).astype(np.uint8)
    out[1::2] = np.clip(np.rint(127.5 + 127.5 * iq.imag), 0, 255).astype(np.uint8)
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--out", required=True)
    p.add_argument("--seconds", type=float, default=60.0)
    p.add_argument("--noise", type=float, default=0.01, help="complex noise sigma, 0..1 of full scale")
    p.add_argument("--seed", type=int, default=162)
    p.add_argument("--offset-scale", type=float, default=1.0,
                   help="multiplies every station's carrier offset (1 = up to +/-400 Hz); raise it to test offset tolerance")
    p.add_argument("--origin", default="%g,%g" % ORIGIN,
                   help="LAT,LON the synthetic tracks are laid out around (to put them on a map near a bench receiver)")
    p.add_argument("--list", action="store_true", help="print each transmission and exit")
    a = p.parse_args()
    origin = tuple(float(v) for v in a.origin.split(","))
    tx = build_schedule(a.seconds, origin)
    if a.list:
        for item in tx:
            print("%6.2f s ch%d type %2d mmsi %09d %s" % (item["t"], item["channel"], item["type"], item["station"],
                                                       item["payload"].hex().upper()))
        return
    data = render(tx, a.seconds, a.noise, a.seed, a.offset_scale)
    with open(a.out, "wb") as f:
        f.write(data.tobytes())
    digest = hashlib.sha256(data.tobytes()).hexdigest()
    sequence = 0
    manifest_tx = []
    for item in tx:
        sentences = aivdm(item["payload"], item["channel"], sequence % 10)
        if len(sentences) > 1:
            sequence += 1
        manifest_tx.append({"t": round(item["t"], 3), "start_sample": item["start_sample"], "samples": item["samples"],
                            "channel": item["channel"], "type": item["type"], "mmsi": item["station"],
                            "bits": len(item["payload"]) * 8, "payload_hex": item["payload"].hex().upper(),
                            "offset_hz": item["offset_hz"] * a.offset_scale, "amplitude": item["amplitude"],
                            "fields": item["fields"], "nmea": sentences})
    by_type, by_channel = {}, {1: 0, 2: 0}
    for item in tx:
        by_type[item["type"]] = by_type.get(item["type"], 0) + 1
        by_channel[item["channel"]] += 1
    manifest = {"generator": "tools/ais_make_iq.py", "profile": "fleet-v1", "synthetic": True, "center_hz": CENTER_HZ,
                "sample_rate": RATE, "format": "cu8", "seconds": a.seconds, "noise_sigma": a.noise, "seed": a.seed,
                "offset_scale": a.offset_scale, "origin": list(origin), "sha256": digest, "bytes": int(data.size),
                "transmissions": len(tx), "by_type": {str(k): v for k, v in sorted(by_type.items())},
                "by_channel": {str(k): v for k, v in by_channel.items()},
                "stations": sorted({item["station"] for item in tx}), "tx": manifest_tx}
    with open(a.out + ".json", "w", newline="\n") as f:
        json.dump(manifest, f, indent=1)
    print("wrote %s: %.0f s, %d bytes, %d transmissions (%s), sha256 %s" %
          (a.out, a.seconds, data.size, len(tx), ", ".join("type %s x%d" % kv for kv in manifest["by_type"].items()), digest))


if __name__ == "__main__":
    sys.exit(main())
