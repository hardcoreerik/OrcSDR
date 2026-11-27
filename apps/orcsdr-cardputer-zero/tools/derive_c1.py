"""Recover C1's twelve data bits by de-whitening with the C0-seeded keystream.

C0's twelve data bits are mapped (on-air 0, 4, ... 44), so the keystream that the
transmitter XORed onto C1 can be regenerated exactly. Each class-1 on-air position
is then tested against every keystream index and every unresolved payload bit; a
correct triple agrees on all frames, a wrong one about half.

No external table is used, and no GPL material has been consulted.
"""
from __future__ import annotations

import numpy as np

ORACLE = r"F:\Ai\OrcSDR-Temp\dmr-iq\frames1.txt"
PAYLOADS = r"F:\Ai\OrcSDR-Temp\payloads.txt"
WINDOW = 20


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


def keystream(c0_bits: np.ndarray) -> np.ndarray:
    """23 keystream bits from the 12-bit C0 data word, per the documented PRNG."""
    value = 0
    for bit in c0_bits:
        value = (value << 1) | int(bit)
    pr = [0] * 24
    pr[0] = 16 * (value & 0x0FFF)
    for i in range(1, 24):
        pr[i] = (173 * pr[i - 1] + 13849) & 0xFFFF
    return np.array([(pr[i] >> 15) & 1 for i in range(24)], dtype=np.uint8)


def main() -> int:
    ours = load_ours()
    oracle = load_oracle()
    n = min(WINDOW, len(ours), len(oracle))
    print(f"our frames {len(ours)}, oracle frames {len(oracle)}, using {n}")

    # C0 data bits: payload 0..11 live at on-air 0,4,...,44.
    c0_positions = [4 * k for k in range(12)]
    c0_all = ours[:n][:, c0_positions]              # (n, 12)
    payload = oracle[:n]                            # (n, 49)

    class1 = [4 * k + 1 for k in range(18)]         # on-air 1,5,...,69
    hits = []
    for position in class1:
        raw_bits = ours[:n, position]               # (n,)
        for k in range(1, 24):
            for frame in range(n):
                ks = keystream(c0_all[frame])
                dewhitened = raw_bits ^ ks[k]
                break
            # vectorised: keystream per frame is frame-dependent
            series = np.array([raw_bits[i] ^ keystream(c0_all[i])[k] for i in range(n)])
            for o in range(12, 24):                 # unresolved payload bits
                if np.array_equal(series, payload[:, o]):
                    hits.append((o, position, k))

    print(f"perfect de-whitened matches: {len(hits)}")
    for o, position, k in hits:
        print(f"  payload bit {o:2d}  <- on-air {position:2d}  keystream index {k}")
    return 0 if hits else 1


if __name__ == "__main__":
    raise SystemExit(main())
