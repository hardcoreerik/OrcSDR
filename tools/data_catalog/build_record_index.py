#!/usr/bin/env python3
"""Create a compact, streamable ORCCAT1 runtime index from curated CSV rows.

This intentionally does not fetch data. Source-specific review decides which
columns are publishable; this tool only normalizes that reviewed subset.
"""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pack", required=True,
                        choices=("faa_aviation", "noaa_weather", "fcc_broadcast"))
    parser.add_argument("--input", type=Path, required=True, help="curated source CSV")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--field", action="append", required=True,
                        help="source column to retain; repeat for each field")
    args = parser.parse_args()

    args.out.parent.mkdir(parents=True, exist_ok=True)
    count = 0
    with args.input.open(newline="", encoding="utf-8-sig", errors="replace") as source, \
            args.out.open("w", encoding="utf-8", newline="\n") as output:
        reader = csv.DictReader(source)
        if reader.fieldnames is None or any(field not in reader.fieldnames for field in args.field):
            raise ValueError("one or more requested fields are absent from the source CSV")
        output.write("ORCCAT1\n")
        output.write(json.dumps({"schema": "orcsdr-record-index-v1", "pack": args.pack,
                                 "fields": args.field}, separators=(",", ":"), sort_keys=True) + "\n")
        for row in reader:
            record = [row[field].strip() for field in args.field]
            if any(record):
                output.write(json.dumps(record, ensure_ascii=False, separators=(",", ":")) + "\n")
                count += 1
    assert args.out.read_bytes()[:8] == b"ORCCAT1\n"
    print(json.dumps({"pack": args.pack, "records": count, "bytes": args.out.stat().st_size}))


if __name__ == "__main__":
    main()
