#!/usr/bin/env python3
"""Build OrcSDR's fixed-record ADS-B lookup index from the FAA release."""

import argparse
import csv
import struct
from pathlib import Path


HEADER = struct.Struct("<8sII")
RECORD = struct.Struct("<I9s49s51s")


def field(value: str, size: int) -> bytes:
    return value.strip().encode("ascii", "replace")[: size - 1].ljust(size, b"\0")


def self_check() -> None:
    assert HEADER.size == 16
    assert RECORD.size == 113
    assert field(" N73063 ", 9) == b"N73063\0\0\0"


def main() -> None:
    self_check()
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path, help="directory containing MASTER.txt and ACFTREF.txt")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    references: dict[str, str] = {}
    with (args.source / "ACFTREF.txt").open(
        newline="", encoding="utf-8-sig", errors="replace"
    ) as source:
        for row in csv.DictReader(source):
            references[row["CODE"].strip()] = " ".join(
                part for part in (row["MFR"].strip(), row["MODEL"].strip()) if part
            )

    records: list[tuple[int, bytes, bytes, bytes]] = []
    with (args.source / "MASTER.txt").open(
        newline="", encoding="utf-8-sig", errors="replace"
    ) as source:
        for row in csv.DictReader(source):
            hex_code = row["MODE S CODE HEX"].strip()
            if not hex_code:
                continue
            records.append(
                (
                    int(hex_code, 16),
                    field("N" + row["N-NUMBER"].strip(), 9),
                    field(references.get(row["MFR MDL CODE"].strip(), ""), 49),
                    field(row["NAME"], 51),
                )
            )

    records.sort(key=lambda record: record[0])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("wb") as output:
        output.write(HEADER.pack(b"ORCADSB1", RECORD.size, len(records)))
        for record in records:
            output.write(RECORD.pack(*record))
    print(f"records={len(records)} bytes={args.output.stat().st_size} record_size={RECORD.size}")


if __name__ == "__main__":
    main()
