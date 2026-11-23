#!/usr/bin/env python3
"""Build the compact US AM/FM station index the Cardputer Zero browses.

Source data: FCC LMS public database, snapshot 2026-11-03, produced by
F:\\Ai\\OrcSDR-station-data. The upstream dataset carries licenceId
USGOV_WORKS ("Public Domain U.S. Government") with attribution "FCC Media
Bureau", so it may be redistributed; the provenance line below is written into
the index header and must not be removed.

The NDJSON is ~9 MB for 26k records. Parsing that on a Pi CM0 while a radio is
running is wasteful, so this emits a fixed-width little-endian table sorted by
frequency, which the reader binary-searches. Licence status is kept because a
licensed record is not proof a station is on air (LICSL means licensed-silent).

Record layout (45 bytes, little endian):
  u32 frequency_hz
  u8  service      0 = AM, 1 = FM
  u8  licensed     bit0 LICEN, bit1 LICSL (silent), bit2 LICRP, bit3 LICSU
  c8  callsign     NUL padded
  c20 city         NUL padded
  c3  region       two-letter state, NUL padded
  i32 latitude_e7
  i32 longitude_e7
"""
from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

MAGIC = b"ORCSTN\x01"
VERSION = 1
RECORD = struct.Struct("<IBB8s20s3sii")
PROVENANCE = (
    b"FCC LMS 2026-11-03, US AM/FM, public domain (USGOV_WORKS), "
    b"attribution FCC Media Bureau"
)
STATUS_BITS = {"LICEN": 1, "LICSL": 2, "LICRP": 4, "LICSU": 8}


def build(source: Path, destination: Path) -> tuple[int, int, int]:
    rows = []
    skipped = 0
    for line in source.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line:
            continue
        record = json.loads(line)
        service = 1 if record.get("service") == "fm" else 0
        frequency = int(record.get("frequency_hz") or 0)
        callsign = (record.get("callsign") or record.get("name") or "").strip().upper()
        if not frequency or not callsign:
            skipped += 1
            continue
        rows.append(
            (
                frequency,
                service,
                STATUS_BITS.get(record.get("status", ""), 0),
                callsign.encode("ascii", "replace"),
                (record.get("city") or "").strip().upper().encode("ascii", "replace"),
                (record.get("region") or "").strip().upper().encode("ascii", "replace"),
                int(record.get("latitude_e7") or 0),
                int(record.get("longitude_e7") or 0),
            )
        )

    # Sorted by tuning order, with the call sign as a stable tie-break so the
    # output is reproducible.
    rows.sort(key=lambda row: (row[0], row[3]))

    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("wb") as out:
        out.write(MAGIC)
        out.write(struct.pack("<III", VERSION, len(rows), RECORD.size))
        out.write(PROVENANCE.ljust(120, b"\0")[:120])
        for row in rows:
            out.write(RECORD.pack(*row))

    am = sum(1 for row in rows if row[1] == 0)
    fm = len(rows) - am
    return len(rows), am, fm


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "source",
        nargs="?",
        default=r"F:\Ai\OrcSDR-station-data\out\fcc_us_am_fm.ndjson",
        type=Path,
    )
    parser.add_argument("destination", nargs="?", default=Path("fm_am_stations.idx"), type=Path)
    args = parser.parse_args()

    total, am, fm = build(args.source, args.destination)
    size = args.destination.stat().st_size
    print(f"records {total} (AM {am}, FM {fm}) -> {args.destination} ({size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
