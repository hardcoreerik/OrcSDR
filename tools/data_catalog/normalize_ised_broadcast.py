#!/usr/bin/env python3
"""Normalize ISED's licensed Broadcast Service CSV export into station rows."""

from __future__ import annotations

import argparse
import csv
import json
import zipfile
from collections import Counter
from pathlib import Path


SOURCE = "ised_sms_broadcast"
SOURCE_URL = "https://open.canada.ca/data/en/dataset/508040d7-6fa9-46e4-afbc-aa61f3ca317e"
FIELDS = 61


def text(row: list[str], index: int) -> str:
    return row[index].strip() if len(row) > index else ""


def number(value: str) -> float | None:
    try:
        return float(value) if value else None
    except ValueError:
        return None


def service_for(frequency_mhz: float, call_sign: str) -> str | None:
    if 0.525 <= frequency_mhz <= 1.705:
        return "am"
    if 87.5 <= frequency_mhz <= 108.0 and call_sign:
        return "fm"
    return None


def station(row: list[str]) -> dict[str, object] | None:
    if len(row) < FIELDS or text(row, 0) != "TX":
        return None
    frequency_mhz = number(text(row, 1))
    call_sign = text(row, 33)
    if frequency_mhz is None:
        return None
    service = service_for(frequency_mhz, call_sign)
    if service is None:
        return None
    # Column 2 is ISED's per-frequency-record identity. The authorization
    # reference (column 47) may legitimately cover several transmitter rows.
    source_id = text(row, 2) or text(row, 47) + "-" + text(row, 1) + "-" + call_sign
    frequency_hz = round(frequency_mhz * 1_000_000)
    latitude_e7 = round((number(text(row, 40)) or 0) * 10_000_000)
    longitude_e7 = round((number(text(row, 41)) or 0) * 10_000_000)
    status = text(row, 56)
    # A frequency record may have several authorized transmitter sites. Keep
    # source_id unchanged and qualify the OrcSDR row ID by those stable facts.
    record: dict[str, object] = {
        "id": f"{SOURCE}:{source_id}:{frequency_hz}:{latitude_e7}:{longitude_e7}:{status}",
        "source": SOURCE, "source_id": source_id,
        "source_url": SOURCE_URL, "country": "CA", "service": service,
        "frequency_hz": frequency_hz, "callsign": call_sign,
        "name": call_sign, "city": text(row, 31), "region": text(row, 39),
        "status": status, "mode": text(row, 12), "latitude_e7": latitude_e7,
        "longitude_e7": longitude_e7,
    }
    power = number(text(row, 58)) or number(text(row, 59)) or number(text(row, 15))
    if power is not None and power > 0:
        record["power_w"] = round(power)
    return record


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True, help="ISED Broadcast Service ZIP")
    parser.add_argument("--out", type=Path, required=True, help="normalized NDJSON")
    args = parser.parse_args()
    with zipfile.ZipFile(args.input) as archive:
        names = [name for name in archive.namelist() if name.lower().endswith(".csv")]
        if len(names) != 1:
            raise SystemExit("expected exactly one CSV in ISED broadcast ZIP")
        with archive.open(names[0]) as raw:
            rows = csv.reader((line.decode("utf-8-sig") for line in raw))
            unique = {json.dumps(record, sort_keys=True): record
                      for row in rows if (record := station(row))}
            records = list(unique.values())
    records.sort(key=lambda row: (row["frequency_hz"], row["id"]))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("".join(json.dumps(row, ensure_ascii=False, sort_keys=True) + "\n" for row in records),
                        encoding="utf-8", newline="\n")
    print(json.dumps({"source": SOURCE, "records": len(records),
                      "services": dict(sorted(Counter(row["service"] for row in records).items()))}, indent=2))


if __name__ == "__main__":
    main()
