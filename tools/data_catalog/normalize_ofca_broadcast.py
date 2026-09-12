#!/usr/bin/env python3
"""Normalize OFCA analogue sound broadcasting frequency CSV into ORCBRD1 rows.

Source: Office of the Communications Authority frequency table published on
DATA.GOV.HK (dataset hk-ofca-ofca-ofca-dataset-21). English CSV columns
observed 2026-09-10 PT:

- BROADCASTER, CH_NAME, TX_STATION, MODULATION, FREQ, ERP,
  REBROADCAST_TUNNEL, REMARK
- MODULATION is VHF/FM or MF/AM
- FREQ is always MHz (AM carriers appear as 0.567 / 0.864 / 1.584)
- ERP is watts (matches OFCA PDF local-relay notes such as 50 W at Hill 374)
- No coordinates in the CSV; latitude_e7 / longitude_e7 are omitted
- country is always HK (Hong Kong SAR), never CN
- Stable source_id is CH_NAME|TX_STATION|FREQ|MODULATION
"""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from pathlib import Path


SOURCE = "ofca_sound_freq_table"
SOURCE_URL = "https://data.gov.hk/en-data/dataset/hk-ofca-ofca-ofca-dataset-21"
FM_MIN_MHZ = 87.5
FM_MAX_MHZ = 108.0
AM_MIN_MHZ = 0.525
AM_MAX_MHZ = 1.705


def open_text(path: Path) -> tuple[str, str]:
    raw = path.read_bytes()
    for encoding in ("utf-8-sig", "utf-8", "cp1252", "latin-1"):
        try:
            return raw.decode(encoding), encoding
        except UnicodeDecodeError:
            continue
    raise SystemExit(f"unable to decode {path}")


def number(value: str) -> float | None:
    cleaned = value.strip().replace(",", "")
    if not cleaned:
        return None
    try:
        return float(cleaned)
    except ValueError:
        return None


def service_and_hz(modulation: str, frequency_mhz: float) -> tuple[str | None, int | None, str | None]:
    kind = modulation.strip().upper()
    if kind in {"VHF/FM", "FM", "VHF"}:
        if FM_MIN_MHZ <= frequency_mhz <= FM_MAX_MHZ:
            return "fm", round(frequency_mhz * 1_000_000), None
        return None, None, "out_of_fm_band"
    if kind in {"MF/AM", "AM", "MF"}:
        if AM_MIN_MHZ <= frequency_mhz <= AM_MAX_MHZ:
            return "am", round(frequency_mhz * 1_000_000), None
        return None, None, "out_of_am_band"
    return None, None, "unknown_modulation"


def richness(record: dict[str, object]) -> tuple[int, int]:
    return (int(record.get("power_w") or 0), len(record))


def station(row: dict[str, str]) -> tuple[dict[str, object] | None, str | None]:
    broadcaster = (row.get("BROADCASTER") or "").strip()
    channel = (row.get("CH_NAME") or "").strip()
    transmitter = (row.get("TX_STATION") or "").strip()
    modulation = (row.get("MODULATION") or "").strip()
    freq_raw = (row.get("FREQ") or "").strip()
    frequency_mhz = number(freq_raw)
    if not channel:
        return None, "missing_channel"
    if not transmitter:
        return None, "missing_transmitter"
    if frequency_mhz is None:
        return None, "bad_frequency"
    service, frequency_hz, reason = service_and_hz(modulation, frequency_mhz)
    if service is None or frequency_hz is None:
        return None, reason or "out_of_band"
    source_id = f"{channel}|{transmitter}|{freq_raw}|{modulation}"
    record: dict[str, object] = {
        "id": f"{SOURCE}:{source_id}:{frequency_hz}",
        "source": SOURCE,
        "source_id": source_id,
        "source_url": SOURCE_URL,
        "country": "HK",
        "service": service,
        "frequency_hz": frequency_hz,
        "name": channel,
        "callsign": channel,
        "transmitter": transmitter,
    }
    if broadcaster:
        record["city"] = "Hong Kong"
        # Retain broadcaster as region-ish factual label without inventing admin codes.
        record["region"] = broadcaster
    erp = number((row.get("ERP") or "").strip())
    if erp is not None and erp > 0:
        record["power_w"] = round(erp)
    return record, None


def merge_record(
    by_id: dict[str, dict[str, object]],
    rejects: Counter[str],
    record: dict[str, object],
) -> None:
    existing = by_id.get(str(record["id"]))
    if existing is None:
        by_id[str(record["id"])] = record
    elif existing == record:
        rejects["duplicate_identical"] += 1
    elif richness(record) > richness(existing):
        rejects["superseded_same_id"] += 1
        by_id[str(record["id"])] = record
    else:
        rejects["superseded_same_id"] += 1


def load_rows(path: Path) -> tuple[list[dict[str, object]], Counter[str], str]:
    decoded, encoding = open_text(path)
    reader = csv.DictReader(decoded.splitlines())
    if not reader.fieldnames:
        raise SystemExit(f"empty CSV: {path}")
    required = {"BROADCASTER", "CH_NAME", "TX_STATION", "MODULATION", "FREQ", "ERP"}
    missing = sorted(required - {name.strip() for name in reader.fieldnames})
    if missing:
        raise SystemExit(f"{path}: missing columns {missing}")
    rejects: Counter[str] = Counter()
    by_id: dict[str, dict[str, object]] = {}
    for row in reader:
        if not any((value or "").strip() for value in row.values()):
            continue
        record, reason = station(row)
        if record is None:
            rejects[reason or "rejected"] += 1
            continue
        merge_record(by_id, rejects, record)
    records = list(by_id.values())
    records.sort(key=lambda row: (int(row["frequency_hz"]), str(row["id"])))
    return records, rejects, encoding


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True, help="OFCA analogue sound frequency CSV")
    parser.add_argument("--out", type=Path, required=True, help="normalized NDJSON")
    args = parser.parse_args()
    if not args.input.is_file():
        raise SystemExit(f"input not found: {args.input}")
    records, rejects, encoding = load_rows(args.input)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        "".join(json.dumps(row, ensure_ascii=False, sort_keys=True) + "\n" for row in records),
        encoding="utf-8",
        newline="\n",
    )
    print(json.dumps({
        "source": SOURCE,
        "encoding": encoding,
        "input": str(args.input),
        "records": len(records),
        "services": dict(sorted(Counter(str(row["service"]) for row in records).items())),
        "countries": dict(sorted(Counter(str(row.get("country", "")) for row in records).items())),
        "rejected": dict(sorted(rejects.items())),
    }, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
