"""Find the DMR voice-burst -> AMBE frame bit mapping by set intersection.

The reference log (frames1.txt) holds 49-bit AMBE frames that a third-party
decoder extracted from the same capture. Our VoiceGroup payloads hold 216 bits
per burst with the 48-bit sync already stripped. If any candidate mapping turns
our payload into a frame that appears anywhere in that reference set, the mapping
is right.

First-frame equality was the wrong test: the reference decoder starts at its own
sync, so its first frame need not be our first burst.
"""
from __future__ import annotations

import itertools
import sys

ORACLE = r"F:\Ai\OrcSDR-Temp\dmr-iq\frames1.txt"
PAYLOADS = r"F:\Ai\OrcSDR-Temp\payloads.txt"


def load_oracle(path: str) -> set[str]:
    frames = set()
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            tag = line.find("AMBE ")
            if tag < 0:
                continue
            token = line[tag + 5: tag + 5 + 14].strip().upper()
            if len(token) == 14 and all(c in "0123456789ABCDEF" for c in token):
                frames.add(token)
    return frames


def load_payloads(path: str) -> list[list[int]]:
    out = []
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.strip()
            if not line.startswith("PAYLOAD "):
                continue
            raw = bytes.fromhex(line[8:])
            if len(raw) != 162:  # superframe: 6 bursts x 27 bytes
                continue
            out.append(raw)
    return out


def bits_msb(raw: bytes) -> list[int]:
    """Transmission order: first bit of each byte is its MSB."""
    out = []
    for byte in raw:
        for n in range(8):
            out.append((byte >> (7 - n)) & 1)
    return out


def bits_lsb(raw: bytes) -> list[int]:
    out = []
    for byte in raw:
        for n in range(8):
            out.append((byte >> n) & 1)
    return out


def to_hex(bits: list[int]) -> str:
    value = 0
    for bit in bits:
        value = (value << 1) | bit
    # 49 bits -> 7 bytes, padded in the low bits like the reference log
    value <<= (56 - 49)
    return f"{value:014X}"


def frames_for(payload_bits: list[int], half: str, stride: int, offset: int) -> list[list[int]]:
    """Four candidate frames: two per 98-bit half, 10-bit slot type between."""
    halves = {"ab": [(0, 98), (108, 98)], "ba": [(108, 98), (0, 98)]}[half]
    out = []
    for start, length in halves:
        base = start + offset
        for frame in range(2):
            # stride 1: the two frames are contiguous halves of the 98 bits.
            # stride 2: the frames are bit-interleaved, so they differ by parity.
            first = base + (frame if stride == 2 else frame * 49)
            last = first + 48 * stride
            if last >= len(payload_bits):
                continue
            out.append([payload_bits[first + i * stride] for i in range(49)])
    return out


def main() -> int:
    oracle = load_oracle(ORACLE)
    payloads = load_payloads(PAYLOADS)
    print(f"oracle frames {len(oracle)}, payloads {len(payloads)}")
    if not oracle or not payloads:
        return 2

    hits = []
    tried = 0
    for order_name, to_bits in (("msb", bits_msb), ("lsb", bits_lsb)):
        for half in ("ab", "ba"):
            for stride, offset in ((1, 0), (1, 49), (2, 0), (2, 1)):
                tried += 1
                for index, raw in enumerate(payloads):
                    for burst in range(6):
                        chunk = raw[burst * 27:(burst + 1) * 27]
                        payload_bits = to_bits(chunk)
                        for candidate in frames_for(payload_bits, half, stride, offset):
                            text = to_hex(candidate)
                            if text in oracle:
                                hits.append((order_name, half, stride, offset, index, text))

    print(f"candidates tried {tried}")
    if hits:
        for hit in hits[:10]:
            print("HIT order=%s half=%s stride=%d offset=%d payload=%d frame=%s" % hit)
        print(f"total hits {len(hits)}")
        return 0
    print("no mapping in this space matched the oracle")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
