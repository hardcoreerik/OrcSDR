#!/usr/bin/env python3
"""Search OrcSDR SSTV reception archive exported from Tab5 SD.

The firmware writes append-only index.csv and BMP files. This CLI reads the
archive offline; hashing is deliberately performed on the PC, never on the
ESP32-P4 decoder critical path. Duplicate detection is content-based, NOT by
SD filename (filenames can reset across cards).
"""
from __future__ import annotations
import argparse
import csv
import hashlib
import json
import sys
from pathlib import Path

FIELDS = ("session_id", "utc_epoch", "frequency_hz", "mode", "width",
          "height", "decoded_rows", "sync_missed", "filename")


def read_index(root: Path) -> list[dict[str, object]]:
    index = root / "index.csv"
    with index.open(encoding="utf-8", newline="") as fh:
        records = list(csv.DictReader(fh))
    for n, rec in enumerate(records, start=2):
        if any(k not in rec for k in FIELDS):
            raise ValueError(f"index.csv missing fields on row {n}")
        # Schema v2 (current firmware) stores `complete`; schema v1 stored `partial`. Show one convention.
        if "complete" in rec:
            rec["partial"] = "0" if str(rec["complete"]) == "1" else "1"
        elif "partial" not in rec:
            raise ValueError(f"index.csv has neither complete nor partial on row {n}")
        name = Path(str(rec["filename"]))
        if name.is_absolute() or ".." in name.parts or name.suffix.lower() != ".bmp":
            raise ValueError(f"unsafe or invalid BMP path in CSV on row {n}")
        file_path = root / name
        rec["image_path"] = str(file_path)
        rec["exists"] = file_path.is_file()
        if rec["exists"]:
            digest = hashlib.sha256()
            with file_path.open("rb") as inp:
                for block in iter(lambda: inp.read(128 * 1024), b""):
                    digest.update(block)
            rec["sha256"] = digest.hexdigest()
        else:
            rec["sha256"] = ""
    return records


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("directory", type=Path, help="SD export's /orcsdr/sstv directory")
    p.add_argument("--mode", help="case-insensitive exact SSTV mode")
    p.add_argument("--frequency", type=int, help="frequency in Hz")
    p.add_argument("--complete", action="store_true", help="exclude partial receptions")
    p.add_argument("--duplicates", action="store_true", help="only repeated identical BMP bytes")
    p.add_argument("--json", action="store_true", help="output machine-readable array")
    p.add_argument("--output", type=Path, help="write results to a file")
    args = p.parse_args()
    try:
        records = read_index(args.directory)
    except (OSError, ValueError) as ex:
        print(f"SSTV catalog error: {ex}", file=sys.stderr)
        return 2
    seen: dict[str, int] = {}
    for rec in records:
        digest = str(rec["sha256"])
        if digest:
            seen[digest] = seen.get(digest, 0) + 1
    filtered: list[dict[str, object]] = []
    for rec in records:
        if args.mode and str(rec["mode"]).casefold() != args.mode.casefold():
            continue
        if args.frequency is not None and int(str(rec["frequency_hz"])) != args.frequency:
            continue
        if args.complete and str(rec["partial"]) != "0":
            continue
        if args.duplicates and seen.get(str(rec["sha256"]), 0) < 2:
            continue
        filtered.append(rec)
    if args.json:
        rendered = json.dumps(filtered, indent=2)
    else:
        lines = ["UTC epoch  Frequency Hz   Mode          Lines   Partial  SHA256 (first 16)  File"]
        for rec in filtered:
            lines.append(
                f"{rec['utc_epoch']!s:<10} {rec['frequency_hz']!s:<14} "
                f"{rec['mode']!s:<13} {rec['decoded_rows']!s:<7} "
                f"{rec['partial']!s:<8} {str(rec['sha256'])[:16]:<18} "
                f"{rec['filename']!s}"
            )
        rendered = "\n".join(lines)
    if args.output:
        args.output.write_text(rendered + "\n", encoding="utf-8")
        print(f"Wrote {len(filtered)} records to {args.output}")
    else:
        print(rendered)
    return 0


if __name__ == "__main__":
    sys.exit(main())
