"""Verify the complete derived schedule against the reference vectors.

Schedule recovered entirely from data (no external tables, no GPL material):

  payload[0..11]   = onair[4k]              k = 0..11
  payload[12..23]  = onair[21+4k] ^ ks[k]   k = 1..12   (C1, whitened)
  payload[24..30]  = onair[46+4k]           k = 0..6
  payload[31..48]  = onair[3+4k]            k = 0..17

If this reproduces the reference 49-bit vectors exactly, the schedule is proven and
can be implemented in the decoder.
"""
from __future__ import annotations

import numpy as np

ORACLE = r"F:\Ai\OrcSDR-Temp\dmr-iq\frames1.txt"
PAYLOADS = r"F:\Ai\OrcSDR-Temp\payloads.txt"


def bits_from_bytes(raw: bytes) -> np.ndarray:
    return np.unpackbits(np.frombuffer(raw, dtype=np.uint8))


def load_ours() -> np.ndarray:
    frames = []
    with open(PAYLOADS, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.strip()
            if not line.startswith("PAYLOAD "):
                continue
            raw = bytes.fromhex(line[8:])
            if len(raw) != 162:
                continue
            bits = bits_from_bytes(raw)
            for frame in range(18):
                frames.append(bits[frame * 72:(frame + 1) * 72])
    return np.array(frames, dtype=np.uint8)


def load_oracle() -> np.ndarray:
    frames = []
    with open(ORACLE, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            tag = line.find("AMBE ")
            if tag < 0:
                continue
            token = line[tag + 5: tag + 5 + 14].strip().upper()
            if len(token) != 14:
                continue
            try:
                raw = bytes.fromhex(token)
            except ValueError:
                continue
            frames.append(bits_from_bytes(raw)[:49])
    return np.array(frames, dtype=np.uint8)


def keystream(c0_bits: np.ndarray) -> list[int]:
    value = 0
    for bit in c0_bits:
        value = (value << 1) | int(bit)
    pr = [0] * 24
    pr[0] = 16 * (value & 0x0FFF)
    for i in range(1, 24):
        pr[i] = (173 * pr[i - 1] + 13849) & 0xFFFF
    return [(pr[i] >> 15) & 1 for i in range(24)]


def extract(onair: np.ndarray) -> np.ndarray:
    out = np.zeros(49, dtype=np.uint8)
    for k in range(12):
        out[k] = onair[4 * k]
    c0 = out[0:12]
    ks = keystream(c0)
    for k in range(1, 13):
        out[11 + k] = onair[21 + 4 * k] ^ ks[k]
    for k in range(7):
        out[24 + k] = onair[46 + 4 * k]
    for k in range(18):
        out[31 + k] = onair[3 + 4 * k]
    return out


def main() -> int:
    ours = load_ours()
    oracle = load_oracle()
    n = min(len(ours), len(oracle))
    print(f"our frames {len(ours)}, oracle frames {len(oracle)}, comparing {n}")

    exact = 0
    per_bit = np.zeros(49, dtype=int)
    for i in range(n):
        got = extract(ours[i])
        want = oracle[i]
        if np.array_equal(got, want):
            exact += 1
        per_bit += (got == want)

    print(f"frames exactly equal: {exact} / {n}")
    worst = np.argsort(per_bit)[:6]
    for bit in worst:
        print(f"  bit {bit:2d}: {per_bit[bit]}/{n}")
    return 0 if exact > n // 2 else 1


if __name__ == "__main__":
    raise SystemExit(main())
