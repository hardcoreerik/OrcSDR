"""Recover the DMR voice 72->49 bit mapping by correlation.

Only C1 is whitened, so 37 of the 49 payload bits appear verbatim in the 72 on-air
bits. Given our captured frames and the reference vectors from the same capture,
a true bit pair agrees on every frame while a false pair agrees about half the
time. No external tables are needed, which also keeps the schedule licence-clean.
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
            bits = bits_from_bytes(raw)          # 1296 bits
            for frame in range(18):              # 18 x 72
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
            frames.append(bits_from_bytes(raw)[:49])   # first 49 bits are the vector
    return np.array(frames, dtype=np.uint8)


def score(ours: np.ndarray, oracle: np.ndarray, offset: int, window: int) -> tuple[int, np.ndarray]:
    """Perfect-agreement column count for one alignment."""
    count = min(window, len(ours), len(oracle) - offset)
    if count <= 0:
        return 0, np.zeros((72, 49), dtype=int)
    p = ours[:count].astype(np.int16)
    o = oracle[offset:offset + count].astype(np.int16)
    # agreements = P^T O + (1-P)^T (1-O)
    table = p.T @ o + (1 - p).T @ (1 - o)
    perfect = int(np.count_nonzero(table == count))
    return perfect, table


def main() -> int:
    ours = load_ours()
    oracle = load_oracle()
    print(f"our frames {len(ours)} (72 bits), oracle frames {len(oracle)} (49 bits)")
    if not len(ours) or not len(oracle):
        return 2

    best = (0, -1, np.zeros((72, 49), dtype=int), 0)
    for offset in range(0, max(1, len(oracle) - 40)):
        for window in (20, 30, 40, 60):
            perfect, table = score(ours, oracle, offset, window)
            if perfect > best[0]:
                best = (perfect, offset, table, min(window, len(ours), len(oracle) - offset))

    perfect, offset, table, count = best
    print(f"best alignment offset {offset}, {count} frames, perfect-agreement pairs {perfect}")

    if perfect < 24:
        print("no alignment produces the expected ~37 verbatim bits")
        return 1

    # Report the recovered schedule: payload bit -> on-air bit.
    inverted = int(np.count_nonzero(table == 0))
    print(f"perfect agreement {perfect}, perfect inversion {inverted} (both are real mappings)")
    pairs = [(int(p), int(o)) for p in range(72) for o in range(49) if table[p, o] == count or table[p, o] == 0]
    # Each payload bit should claim exactly one on-air position and vice versa.
    by_payload: dict[int, list[int]] = {}
    for p, o in pairs:
        by_payload.setdefault(o, []).append(p)
    unique = {o: ps[0] for o, ps in by_payload.items() if len(ps) == 1}
    print(f"payload positions identified: {len(unique)} of 49 (37 verbatim expected, 12 are whitened C1)")
    for o in sorted(unique):
        print(f"  payload bit {o:2d}  <- on-air bit {unique[o]:2d}")
    with open(r"F:\\Ai\\OrcSDR-Temp\\schedule_partial.txt", "w", encoding="utf-8") as out:
        for o in sorted(unique):
            out.write(f"{o} {unique[o]}\\n")
    return 0 if len(unique) >= 30 else 1


if __name__ == "__main__":
    raise SystemExit(main())
