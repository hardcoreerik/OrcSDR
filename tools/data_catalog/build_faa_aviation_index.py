#!/usr/bin/env python3
"""Build ORCCAT1 ATC rows from reviewed FAA aviation CSV data."""

from __future__ import annotations

import csv
import sys
from decimal import Decimal, InvalidOperation, ROUND_HALF_UP
from pathlib import Path

RUNTIME_CAPACITY = 24
LABEL_CAPACITY = 31


def e7(value: str, low: Decimal, high: Decimal) -> int:
    try:
        parsed = Decimal(value)
    except InvalidOperation as error:
        raise ValueError(f"invalid coordinate: {value}") from error
    if not low <= parsed <= high:
        raise ValueError(f"coordinate out of range: {value}")
    return int((parsed * 10_000_000).to_integral_value(rounding=ROUND_HALF_UP))


def hz(value: str) -> int:
    try:
        parsed = Decimal(value)
    except InvalidOperation as error:
        raise ValueError(f"invalid MHz frequency: {value}") from error
    result = int((parsed * 1_000_000).to_integral_value(rounding=ROUND_HALF_UP))
    if not 118_000_000 <= result <= 137_000_000:
        raise ValueError(f"airband frequency out of range: {value}")
    return result


def main(source: Path, output: Path) -> None:
    with source.open(encoding="utf-8", newline="") as stream:
        rows = list(csv.DictReader(stream))
    if not rows or not {"latitude", "longitude", "frequency_mhz", "label"} <= rows[0].keys():
        raise ValueError("CSV must contain latitude, longitude, frequency_mhz, label")
    if len(rows) > RUNTIME_CAPACITY:
        raise ValueError(f"{len(rows)} ATC rows exceed device capacity {RUNTIME_CAPACITY}")
    with output.open("w", encoding="ascii", newline="\n") as stream:
        stream.write("ORCCAT1\n")
        for row in rows:
            label = row["label"].strip()
            if not label or any(ord(char) < 32 or ord(char) > 126 for char in label):
                raise ValueError(f"invalid label: {label!r}")
            if len(label) > LABEL_CAPACITY:
                raise ValueError(f"ATC label exceeds {LABEL_CAPACITY} characters: {label!r}")
            stream.write(f"ATC {e7(row['latitude'], Decimal('-90'), Decimal('90'))} "
                         f"{e7(row['longitude'], Decimal('-180'), Decimal('180'))} "
                         f"{hz(row['frequency_mhz'])} {label}\n")


if __name__ == "__main__":
    main(Path(sys.argv[1]), Path(sys.argv[2]))
