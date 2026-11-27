"""Locate the Golay(23,12) parity bits for C0 and C1 on the 72-bit DMR voice frame.

The 49 data bits are mapped; 23 on-air positions are unclaimed. Those should hold
11 parity bits for C0, 11 for C1 and one spare. Since parity is a deterministic
function of the data, correlating an encoded codeword against the unclaimed
positions identifies them exactly - the same method that recovered the data bits,
so no external table is consulted.
"""
from __future__ import annotations

import numpy as np

ORACLE = r"F:\Ai\OrcSDR-Temp\dmr-iq\frames1.txt"
PAYLOADS = r"F:\Ai\OrcSDR-Temp\payloads.txt"
WINDOW = 20

# Data positions recovered earlier.
DATA = ([4 * k for k in range(12)]                       # C0 data
        + [21 + 4 * k for k in range(1, 13)]             # C1 data (whitened)
        + [46 + 4 * k for k in range(7)]                 # C2 first seven
        + [3 + 4 * k for k in range(18)])                # C2 rest + C3
UNCLAIMED = [p for p in range(72) if p not in DATA]


def bits_from_bytes(raw: bytes) -> np.ndarray:
    return np.unpackbits(np.frombuffer(raw, dtype=np.uint8))


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
            bits = bits_from_bytes(raw)
            for frame in range(18):
                frames.append(bits[frame * 72:(frame + 1) * 72])
    return np.array(frames[:WINDOW], dtype=np.uint8)


def golay_encode(data12: list[int], polynomial: int = 0xC75) -> list[int]:
    """Systematic Golay(23,12): 12 data bits followed by 11 parity bits."""
    reg = 0
    for bit in data12:
        reg = (reg << 1) | bit
        if reg & 0x1000:
            reg ^= polynomial << 1
    # long division of data*x^11 by the generator
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
    print(f"frames {n}, unclaimed positions {len(UNCLAIMED)}: {UNCLAIMED}")

    c0 = np.zeros((n, 12), dtype=np.uint8)
    c1 = np.zeros((n, 12), dtype=np.uint8)
    for i in range(n):
        c0[i] = frames[i, [4 * k for k in range(12)]]
        ks = keystream(list(c0[i]))
        # C1 arrives whitened; recover it before encoding.
        for k in range(1, 13):
            c1[i, k - 1] = frames[i, 21 + 4 * k] ^ ks[k]

    for name, data in (("C0", c0), ("C1", c1)):
        print(f"\n{name}: codeword bits against unclaimed on-air positions")
        codewords = np.array([golay_encode(list(row)) for row in data], dtype=np.uint8)
        hits = []
        for bit in range(23):
            column = codewords[:, bit]
            for p in UNCLAIMED:
                if np.array_equal(frames[:, p], column):
                    hits.append((bit, p))
        # a bit may match several positions; report the clean ones
        print(f"  exact matches: {len(hits)}")
        for bit, p in hits[:14]:
            kind = "data" if bit < 12 else "parity"
            print(f"    codeword bit {bit:2d} ({kind})  <- on-air {p:2d}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
