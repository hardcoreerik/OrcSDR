"""Locate C1's Golay parity bits, which are whitenened along with its data."""
from __future__ import annotations

import numpy as np

PAYLOADS = r"F:\Ai\OrcSDR-Temp\payloads.txt"
WINDOW = 20
DATA = ([4 * k for k in range(12)]
        + [21 + 4 * k for k in range(1, 13)]
        + [46 + 4 * k for k in range(7)]
        + [3 + 4 * k for k in range(18)])
UNCLAIMED = [p for p in range(72) if p not in DATA]
# C0 parity already located at these positions; the rest belong to C1.
C0_PARITY = [48, 52, 56, 60, 64, 68, 1, 5, 9, 13, 17]
C1_CANDIDATES = [p for p in UNCLAIMED if p not in C0_PARITY]


def load_frames() -> np.ndarray:
    frames = []
    with open(PAYLOADS, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.strip()
            if not line.startswith("PAYLOAD "):
                continue
            raw = bytes.fromhex(line[8:])
            if len(raw) != 162:
                continue
            bits = np.unpackbits(np.frombuffer(raw, dtype=np.uint8))
            for frame in range(18):
                frames.append(bits[frame * 72:(frame + 1) * 72])
    return np.array(frames[:WINDOW], dtype=np.uint8)


def golay_encode(data12: list[int], polynomial: int = 0xC75) -> list[int]:
    work = 0
    for bit in data12:
        work = (work << 1) | bit
    work <<= 11
    for shift in range(22, 10, -1):
        if work & (1 << shift):
            work ^= polynomial << (shift - 11)
    parity = work & 0x7FF
    return list(data12) + [(parity >> (10 - i)) & 1 for i in range(11)]


def keystream(c0: list[int]) -> list[int]:
    value = 0
    for bit in c0:
        value = (value << 1) | int(bit)
    pr = [0] * 24
    pr[0] = 16 * (value & 0x0FFF)
    for i in range(1, 24):
        pr[i] = (173 * pr[i - 1] + 13849) & 0xFFFF
    return [(pr[i] >> 15) & 1 for i in range(24)]


def main() -> int:
    frames = load_frames()
    n = len(frames)
    print(f"frames {n}; C1 candidate positions {C1_CANDIDATES}")

    c0 = [list(frames[i, [4 * k for k in range(12)]]) for i in range(n)]
    c1 = []
    for i in range(n):
        ks = keystream(c0[i])
        c1.append([int(frames[i, 21 + 4 * k] ^ ks[k]) for k in range(1, 13)])

    codewords = np.array([golay_encode(row) for row in c1], dtype=np.uint8)
    hits = []
    for j in range(12, 23):
        for p in C1_CANDIDATES:
            for k in range(13, 24):
                series = np.array([int(frames[i, p]) ^ keystream(c0[i])[k] for i in range(n)],
                                  dtype=np.uint8)
                if np.array_equal(series, codewords[:, j]):
                    hits.append((j, p, k))
    print(f"exact matches: {len(hits)}")
    for j, p, k in hits:
        print(f"  parity bit {j:2d} <- on-air {p:2d} keystream {k}")
    return 0 if hits else 1


if __name__ == "__main__":
    raise SystemExit(main())
