#!/usr/bin/env python3
"""Build a seekable ORCBRD1 offline broadcast-station index.

Input is newline-delimited normalized JSON, deliberately kept separate from
source-specific FCC/regulator/HF schedule adapters.  The compact frequency
index lets the device seek only candidates for a tuned frequency while the
canonical JSON record retains factual provenance and schedule detail.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from collections import Counter
from datetime import date
from pathlib import Path


MAGIC = b"ORCBRD1\n"
VERSION = 1
HEADER = struct.Struct("<8sHHIIIII")
INDEX = struct.Struct("<II")
SERVICES = {"am", "fm", "shortwave"}
REQUIRED = ("id", "source", "source_id", "service", "frequency_hz")


def canonical(value: object) -> bytes:
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"),
                      sort_keys=True).encode("utf-8")


def reject(reason: str, line: int) -> ValueError:
    return ValueError(f"line {line}: {reason}")


def normalize(raw: object, line: int) -> dict[str, object]:
    if not isinstance(raw, dict):
        raise reject("record must be an object", line)
    if any(not isinstance(raw.get(key), str) or not raw[key].strip()
           for key in ("id", "source", "source_id")):
        raise reject("id, source, and source_id must be non-empty strings", line)
    service = raw.get("service")
    if service not in SERVICES:
        raise reject("service must be am, fm, or shortwave", line)
    frequency = raw.get("frequency_hz")
    if not isinstance(frequency, int) or not 100_000 <= frequency <= 300_000_000:
        raise reject("frequency_hz must be an integer from 100000 to 300000000", line)
    record = dict(raw)
    record["id"] = record["id"].strip()
    record["source"] = record["source"].strip()
    record["source_id"] = record["source_id"].strip()
    return record


def load_records(path: Path) -> tuple[list[dict[str, object]], list[str]]:
    records: list[dict[str, object]] = []
    rejected: list[str] = []
    for line_no, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not raw_line.strip() or raw_line.lstrip().startswith("#"):
            continue
        try:
            records.append(normalize(json.loads(raw_line), line_no))
        except (json.JSONDecodeError, ValueError) as error:
            rejected.append(str(error))
    records.sort(key=lambda row: (int(row["frequency_hz"]), str(row["service"]), str(row["id"])))
    return records, rejected


def summary(records: list[dict[str, object]], source_date: str) -> dict[str, object]:
    return {
        "schema": "orcsdr-broadcast-index-v1",
        "source_date": source_date,
        "records": len(records),
        "services": dict(sorted(Counter(str(row["service"]) for row in records).items())),
        "sources": dict(sorted(Counter(str(row["source"]) for row in records).items())),
        "countries": dict(sorted(Counter(str(row.get("country", "")) for row in records).items())),
    }


def build(records: list[dict[str, object]], source_date: str, output: Path) -> dict[str, object]:
    metadata = canonical(summary(records, source_date))
    payloads = [canonical(record) for record in records]
    offsets: list[int] = []
    cursor = 0
    for payload in payloads:
        offsets.append(cursor)
        cursor += 4 + len(payload)

    metadata_offset = HEADER.size
    index_offset = metadata_offset + len(metadata)
    records_offset = index_offset + len(records) * INDEX.size
    header = HEADER.pack(MAGIC, VERSION, HEADER.size, len(records), index_offset,
                         records_offset, metadata_offset, len(metadata))
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".part")
    with temporary.open("wb") as stream:
        stream.write(header)
        stream.write(metadata)
        for record, offset in zip(records, offsets):
            stream.write(INDEX.pack(int(record["frequency_hz"]), offset))
        for payload in payloads:
            stream.write(struct.pack("<I", len(payload)))
            stream.write(payload)
    temporary.replace(output)
    report = summary(records, source_date)
    report.update({"bytes": output.stat().st_size,
                   "sha256": hashlib.file_digest(output.open("rb"), "sha256").hexdigest()})
    return report


def inspect(path: Path) -> dict[str, object]:
    with path.open("rb") as stream:
        raw = stream.read(HEADER.size)
        if len(raw) != HEADER.size:
            raise ValueError("truncated header")
        magic, version, header_bytes, count, index_offset, records_offset, metadata_offset, metadata_bytes = HEADER.unpack(raw)
        if magic != MAGIC or version != VERSION or header_bytes != HEADER.size:
            raise ValueError("not an ORCBRD1 version-1 index")
        if metadata_offset != HEADER.size or index_offset != metadata_offset + metadata_bytes:
            raise ValueError("invalid section offsets")
        stream.seek(metadata_offset)
        metadata = json.loads(stream.read(metadata_bytes))
        stream.seek(index_offset)
        previous = -1
        for _ in range(count):
            frequency, offset = INDEX.unpack(stream.read(INDEX.size))
            if frequency < previous:
                raise ValueError("frequency index is not sorted")
            previous = frequency
            stream.seek(records_offset + offset)
            size_raw = stream.read(4)
            if len(size_raw) != 4:
                raise ValueError("truncated record length")
            size = struct.unpack("<I", size_raw)[0]
            record = json.loads(stream.read(size))
            if record.get("frequency_hz") != frequency:
                raise ValueError("index frequency does not match record")
            stream.seek(index_offset + (_ + 1) * INDEX.size)
    metadata["bytes"] = path.stat().st_size
    metadata["sha256"] = hashlib.file_digest(path.open("rb"), "sha256").hexdigest()
    return metadata


def main() -> None:
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    build_parser = commands.add_parser("build")
    build_parser.add_argument("--input", type=Path, required=True)
    build_parser.add_argument("--out", type=Path, required=True)
    build_parser.add_argument("--source-date", default=date.today().isoformat())
    build_parser.add_argument("--allow-rejected", action="store_true")
    inspect_parser = commands.add_parser("inspect")
    inspect_parser.add_argument("--input", type=Path, required=True)
    args = parser.parse_args()

    if args.command == "inspect":
        print(json.dumps(inspect(args.input), indent=2, sort_keys=True))
        return
    records, rejected = load_records(args.input)
    if rejected and not args.allow_rejected:
        raise SystemExit("rejected rows:\n" + "\n".join(rejected))
    report = build(records, args.source_date, args.out)
    report["rejected"] = rejected
    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
