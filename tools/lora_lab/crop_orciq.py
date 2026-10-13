#!/usr/bin/env python3
"""Crop an ORCIQ capture to a time window (keeps the 36-byte header valid)."""
import argparse
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import decode_orciq  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--start-s", type=float, required=True)
    parser.add_argument("--end-s", type=float, required=True)
    args = parser.parse_args()
    rate, frequency_hz, sf, bandwidth_hz, iq = decode_orciq.read_capture(args.capture)
    cropped = iq[int(args.start_s * rate) * 2:int(args.end_s * rate) * 2]
    if not cropped:
        raise SystemExit("empty crop: the window is outside the capture "
                         f"({len(iq) // 2 / rate:.2f} s long)")
    header = decode_orciq.HEADER.pack(decode_orciq.MAGIC, decode_orciq.HEADER.size, rate,
                                      frequency_hz, len(cropped), 1, sf, 0, bandwidth_hz, 0)
    args.output.write_bytes(header + cropped)
    print(f"CROPPED samples={len(cropped) // 2} seconds={len(cropped) / 2 / rate:.2f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
