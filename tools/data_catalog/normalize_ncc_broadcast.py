#!/usr/bin/env python3
"""Normalize Taiwan NCC AM/FM open-data tables into ORCBRD1 rows.

Sources (verified 2026-09-10 PT):
- FM XLS: data.gov.tw dataset 6445 -> api.ncc.gov.tw uploaddowndoc
  datagov/1522493929772027904.xls (sheet 調頻FM_115_7_3, as-of ROC 115/7/3)
- AM: data.gov.tw dataset 6446 still advertises an XLS that returns
  "file not found" on api.ncc.gov.tw; use the same-day NCC portal PDF
  (or the derived CSV) with columns matching the open-data field list.

Columns (both services):
  station name, frequency (FM MHz / AM kHz), transmitter address,
  east longitude, north latitude, station class.

country is always TW. HF overseas rows in the AM table emit service=shortwave.
Licence: 政府資料開放授權條款-第1版 (OGDL-Taiwan-1.0), CC BY 4.0 compatible.
"""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from pathlib import Path

SOURCE = "ncc_am_fm_stations"
FM_SOURCE_URL = "https://data.gov.tw/dataset/6445"
AM_SOURCE_URL = "https://data.gov.tw/dataset/6446"
FM_MIN_MHZ = 87.5
FM_MAX_MHZ = 108.0
AM_MIN_KHZ = 525.0
AM_MAX_KHZ = 1705.0
SW_MAX_KHZ = 30000.0


def number(value: object) -> float | None:
    if value is None:
        return None
    if isinstance(value, (int, float)):
        return float(value)
    cleaned = str(value).strip().replace(",", "")
    if not cleaned:
        return None
    try:
        return float(cleaned)
    except ValueError:
        return None


def degrees_e7(value: object) -> int | None:
    parsed = number(value)
    if parsed is None:
        return None
    return round(parsed * 10_000_000)


def richness(record: dict[str, object]) -> tuple[int, int, int]:
    return (
        1 if record.get("latitude_e7") is not None else 0,
        1 if record.get("longitude_e7") is not None else 0,
        len(record),
    )


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


def normalize_header(value: object) -> str:
    return str(value or "").replace("\n", "").replace(" ", "").strip()


def pick(row: dict[str, object], *candidates: str) -> object:
    normalized = {normalize_header(key): value for key, value in row.items()}
    for candidate in candidates:
        key = normalize_header(candidate)
        if key in normalized:
            return normalized[key]
    return ""


def fm_record(row: dict[str, object]) -> tuple[dict[str, object] | None, str | None]:
    name = str(pick(row, "FM電臺名稱", "電臺名稱", "FM 電臺名稱") or "").strip()
    freq = number(pick(row, "頻率(MHz)", "頻率"))
    address = str(pick(row, "發射機地址") or "").strip()
    lon = pick(row, "東經")
    lat = pick(row, "北緯")
    kind = str(pick(row, "電臺類別", "電臺類別") or "").strip()
    if not name:
        return None, "missing_name"
    if freq is None:
        return None, "bad_frequency"
    if not (FM_MIN_MHZ <= freq <= FM_MAX_MHZ):
        return None, "out_of_fm_band"
    frequency_hz = round(freq * 1_000_000)
    source_id = f"{name}|{freq}|{address}"
    record: dict[str, object] = {
        "id": f"{SOURCE}:{source_id}:{frequency_hz}",
        "source": SOURCE,
        "source_id": source_id,
        "source_url": FM_SOURCE_URL,
        "country": "TW",
        "service": "fm",
        "frequency_hz": frequency_hz,
        "name": name,
        "callsign": name,
    }
    if address:
        record["transmitter"] = address
        record["city"] = address
    lon_e7 = degrees_e7(lon)
    lat_e7 = degrees_e7(lat)
    if lon_e7 is not None:
        record["longitude_e7"] = lon_e7
    if lat_e7 is not None:
        record["latitude_e7"] = lat_e7
    if kind:
        record["status"] = kind
    return record, None


def am_record(row: dict[str, object]) -> tuple[dict[str, object] | None, str | None]:
    name = str(pick(row, "AM電臺名稱", "電臺名稱", "AM 電臺名稱") or "").strip()
    freq_khz = number(pick(row, "頻率(kHz)", "頻率"))
    address = str(pick(row, "發射機地址") or "").strip()
    lon = pick(row, "東經")
    lat = pick(row, "北緯")
    kind = str(pick(row, "電臺類別") or "").strip()
    if not name:
        return None, "missing_name"
    if freq_khz is None:
        return None, "bad_frequency"
    if AM_MIN_KHZ <= freq_khz <= AM_MAX_KHZ:
        service = "am"
    elif AM_MAX_KHZ < freq_khz <= SW_MAX_KHZ:
        service = "shortwave"
    else:
        return None, "out_of_band"
    frequency_hz = round(freq_khz * 1_000)
    freq_label = str(int(freq_khz)) if float(freq_khz).is_integer() else str(freq_khz)
    source_id = f"{name}|{freq_label}|{address}"
    record: dict[str, object] = {
        "id": f"{SOURCE}:{source_id}:{frequency_hz}",
        "source": SOURCE,
        "source_id": source_id,
        "source_url": AM_SOURCE_URL,
        "country": "TW",
        "service": service,
        "frequency_hz": frequency_hz,
        "name": name,
        "callsign": name,
    }
    if address:
        record["transmitter"] = address
        record["city"] = address
    lon_e7 = degrees_e7(lon)
    lat_e7 = degrees_e7(lat)
    if lon_e7 is not None:
        record["longitude_e7"] = lon_e7
    if lat_e7 is not None:
        record["latitude_e7"] = lat_e7
    if kind:
        record["status"] = kind
    return record, None


def load_xls_rows(path: Path) -> list[dict[str, object]]:
    import xlrd

    book = xlrd.open_workbook(str(path))
    sheet = book.sheet_by_index(0)
    if sheet.nrows < 2:
        return []
    headers = [str(sheet.cell_value(0, col)).strip() for col in range(sheet.ncols)]
    rows: list[dict[str, object]] = []
    for row_index in range(1, sheet.nrows):
        values = [sheet.cell_value(row_index, col) for col in range(sheet.ncols)]
        if not any(str(value).strip() for value in values):
            continue
        rows.append({headers[col]: values[col] for col in range(len(headers))})
    return rows


def load_csv_rows(path: Path) -> list[dict[str, object]]:
    raw = path.read_bytes()
    text = None
    for encoding in ("utf-8-sig", "utf-8", "cp950", "big5"):
        try:
            text = raw.decode(encoding)
            break
        except UnicodeDecodeError:
            continue
    if text is None:
        raise SystemExit(f"unable to decode {path}")
    reader = csv.DictReader(text.splitlines())
    if not reader.fieldnames:
        raise SystemExit(f"empty CSV: {path}")
    rows: list[dict[str, object]] = []
    for row in reader:
        if not any((value or "").strip() for value in row.values()):
            continue
        rows.append(dict(row))
    return rows


def load_rows(path: Path) -> list[dict[str, object]]:
    suffix = path.suffix.lower()
    if suffix in {".xls", ".xlsx"}:
        return load_xls_rows(path)
    if suffix == ".csv":
        return load_csv_rows(path)
    raise SystemExit(f"unsupported input type: {path}")


def consume(
    path: Path | None,
    kind: str,
    by_id: dict[str, dict[str, object]],
    rejects: Counter[str],
) -> int:
    if path is None:
        return 0
    if not path.is_file():
        raise SystemExit(f"input not found: {path}")
    rows = load_rows(path)
    kept = 0
    for row in rows:
        if kind == "fm":
            record, reason = fm_record(row)
        else:
            record, reason = am_record(row)
        if record is None:
            rejects[reason or "rejected"] += 1
            continue
        merge_record(by_id, rejects, record)
        kept += 1
    return kept


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--fm", type=Path, help="NCC FM XLS/CSV")
    parser.add_argument("--am", type=Path, help="NCC AM XLS/CSV (CSV derived from portal PDF OK)")
    parser.add_argument("--out", type=Path, required=True, help="normalized NDJSON")
    args = parser.parse_args()
    if args.fm is None and args.am is None:
        raise SystemExit("provide --fm and/or --am")
    rejects: Counter[str] = Counter()
    by_id: dict[str, dict[str, object]] = {}
    fm_rows = consume(args.fm, "fm", by_id, rejects)
    am_rows = consume(args.am, "am", by_id, rejects)
    records = list(by_id.values())
    records.sort(key=lambda row: (int(row["frequency_hz"]), str(row["id"])))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        "".join(json.dumps(row, ensure_ascii=False, sort_keys=True) + "\n" for row in records),
        encoding="utf-8",
        newline="\n",
    )
    print(json.dumps({
        "source": SOURCE,
        "fm_input": str(args.fm) if args.fm else None,
        "am_input": str(args.am) if args.am else None,
        "fm_rows_seen": fm_rows,
        "am_rows_seen": am_rows,
        "records": len(records),
        "services": dict(sorted(Counter(str(row["service"]) for row in records).items())),
        "countries": dict(sorted(Counter(str(row.get("country", "")) for row in records).items())),
        "rejected": dict(sorted(rejects.items())),
    }, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
