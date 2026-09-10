#!/usr/bin/env python3
"""Normalize Ofcom TxParams VHF/MF CSV exports into ORCBRD1 station rows.

Mapping choices (VHF CSV observed 2026-09-09 PT user-provided file):
- File encoding is Windows-1252 (not UTF-8); e.g. station name MonFM.
- Header "Licence " has a trailing space; Frequency is MHz for VHF.
- Lat/Long columns are already WGS84 decimal degrees (NGR is also present).
- No status column in the VHF export - status is omitted rather than invented.
- ERP columns use values like "0.025,000" in kW; commas are thousands
  separators inside the fractional part and are stripped before float().
- Prefer In-Use ERP HP/VP (max polarization), else Licensed ERP; omit power_w
  when rounded watts would be 0.
- Rows without a licence id (e.g. Gibraltar BFBS/GBC) use a stable composite
  source_id of station|frequency|site.
- Country from Area: Isle of Man->IM, Jersey->JE, Guernsey->GG; all other included Ofcom rows->GB. VHF band rows are service fm.

Mapping choices (MF CSV observed 2026-09-09 PT user-provided txparamsmf.csv):
- Same encoding/Lat-Long/licence-power comma conventions as VHF.
- Header "Licence" has no trailing space; Frequency is kHz in the AM/MF band
  (observed 558-1575); convert to Hz via *1000.
- Power columns are "In-Use EMRP (kW)" and "Licensed EMRP" (no HP/VP split).
- No RDS PS/PI columns in the MF export.
- MF band rows are service am. Shortwave is not emitted.
- Dedupe identical JSON; for same id with differing params keep richer row.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import Counter
from pathlib import Path


SOURCE = "ofcom_txparams"
SOURCE_URL = (
    "https://www.ofcom.org.uk/tv-radio-and-on-demand/"
    "coverage-and-transmitters/radio-tech-parameters"
)
FM_MIN_MHZ = 87.5
FM_MAX_MHZ = 108.0
# ITU Region 1 MF broadcast band (kHz), slightly padded to cover UK TxParams.
AM_MIN_KHZ = 531.0
AM_MAX_KHZ = 1705.0


AREA_COUNTRY = {
    "Isle of Man": "IM",
    "Jersey": "JE",
    "Guernsey": "GG",
}


def country_from_area(area: str) -> str:
    return AREA_COUNTRY.get(area.strip(), "GB")



def open_text(path: Path):
    raw = path.read_bytes()
    for encoding in ("utf-8-sig", "cp1252", "latin-1"):
        try:
            return raw.decode(encoding), encoding
        except UnicodeDecodeError:
            continue
    raise SystemExit(f"unable to decode {path}")


def text(row: list[str], index: int | None) -> str:
    if index is None or len(row) <= index:
        return ""
    return row[index].strip()


def number(value: str) -> float | None:
    cleaned = value.strip().replace(",", "")
    if not cleaned:
        return None
    try:
        return float(cleaned)
    except ValueError:
        return None


def header_index(header: list[str], *names: str) -> int:
    for name in names:
        if name in header:
            return header.index(name)
    raise KeyError(f"missing column among {names!r}")


def optional_header_index(header: list[str], *names: str) -> int | None:
    for name in names:
        if name in header:
            return header.index(name)
    return None


def detect_kind(header: list[str]) -> str:
    names = set(header)
    if "In-Use EMRP (kW)" in names or "Licensed EMRP" in names:
        return "mf"
    if "In-UseERP/HP" in names or "RDS PS" in names or "In-Use ERP/VP" in names:
        return "vhf"
    raise SystemExit(
        "unable to detect Ofcom TxParams kind from headers "
        f"(need VHF ERP/RDS or MF EMRP columns); got {header[:12]!r}..."
    )


def erp_kw_to_watts(*values: str) -> int | None:
    watts: list[float] = []
    for value in values:
        parsed = number(value)
        if parsed is not None and parsed > 0:
            watts.append(parsed * 1000.0)
    if not watts:
        return None
    rounded = round(max(watts))
    return rounded if rounded > 0 else None


def richness(record: dict[str, object]) -> tuple[int, int, int]:
    return (
        int(record.get("power_w") or 0),
        1 if record.get("rds_ps") else 0,
        len(record),
    )


def service_and_hz(kind: str, frequency_value: float) -> tuple[str | None, int | None, str | None]:
    if kind == "vhf":
        if FM_MIN_MHZ <= frequency_value <= FM_MAX_MHZ:
            return "fm", round(frequency_value * 1_000_000), None
        return None, None, "out_of_fm_band"
    if kind == "mf":
        if AM_MIN_KHZ <= frequency_value <= AM_MAX_KHZ:
            return "am", round(frequency_value * 1_000), None
        return None, None, "out_of_am_band"
    return None, None, "unknown_kind"


def map_columns(header: list[str], kind: str) -> dict[str, int | None]:
    columns: dict[str, int | None] = {
        "station": header_index(header, "Station"),
        "area": header_index(header, "Area"),
        "site": header_index(header, "Site"),
        "licence": header_index(header, "Licence ", "Licence"),
        "frequency": header_index(header, "Frequency"),
        "lat": header_index(header, "Lat"),
        "lon": header_index(header, "Long"),
        "rds_ps": optional_header_index(header, "RDS PS"),
        "rds_pi": optional_header_index(header, "RDS PI"),
        "in_use_hp": None,
        "in_use_vp": None,
        "licensed_hp": None,
        "licensed_vp": None,
        "in_use_emrp": None,
        "licensed_emrp": None,
    }
    if kind == "vhf":
        columns["in_use_hp"] = header_index(header, "In-UseERP/HP")
        columns["in_use_vp"] = header_index(header, "In-Use ERP/VP")
        columns["licensed_hp"] = header_index(header, "Licensed ERP/HP ", "Licensed ERP/HP")
        columns["licensed_vp"] = header_index(header, "Licensed ERP/VP")
    else:
        columns["in_use_emrp"] = header_index(header, "In-Use EMRP (kW)")
        columns["licensed_emrp"] = header_index(header, "Licensed EMRP")
    return columns


def station(
    row: list[str],
    columns: dict[str, int | None],
    kind: str,
) -> tuple[dict[str, object] | None, str | None]:
    required = [columns[key] for key in ("station", "area", "site", "licence", "frequency", "lat", "lon")]
    if any(index is None for index in required):
        return None, "missing_columns"
    if len(row) < max(index for index in required if index is not None) + 1:
        return None, "short_row"
    frequency_value = number(text(row, columns["frequency"]))
    if frequency_value is None:
        return None, "bad_frequency"
    service, frequency_hz, band_reason = service_and_hz(kind, frequency_value)
    if service is None or frequency_hz is None:
        return None, band_reason or "out_of_band"
    latitude = number(text(row, columns["lat"]))
    longitude = number(text(row, columns["lon"]))
    if latitude is None or longitude is None:
        return None, "bad_coordinates"
    station_name = text(row, columns["station"])
    if not station_name:
        return None, "missing_station"
    site = text(row, columns["site"])
    area = text(row, columns["area"])
    licence = text(row, columns["licence"])
    latitude_e7 = round(latitude * 10_000_000)
    longitude_e7 = round(longitude * 10_000_000)
    if licence:
        source_id = licence
    else:
        source_id = f"{station_name}|{frequency_value:g}|{site or area or 'unknown'}"
    record: dict[str, object] = {
        "id": f"{SOURCE}:{source_id}:{frequency_hz}:{latitude_e7}:{longitude_e7}",
        "source": SOURCE,
        "source_id": source_id,
        "source_url": SOURCE_URL,
        "country": country_from_area(area),
        "service": service,
        "frequency_hz": frequency_hz,
        "name": station_name,
        "latitude_e7": latitude_e7,
        "longitude_e7": longitude_e7,
    }
    if area:
        record["city"] = area
    if site:
        record["transmitter"] = site
    rds_ps = text(row, columns["rds_ps"])
    rds_pi = text(row, columns["rds_pi"])
    if rds_ps:
        record["rds_ps"] = rds_ps
        cleaned = re.sub(r"_+", " ", rds_ps).strip()
        if cleaned:
            record["callsign"] = cleaned
    if rds_pi:
        record["rds_pi"] = rds_pi
    if kind == "vhf":
        power_w = erp_kw_to_watts(
            text(row, columns["in_use_hp"]),
            text(row, columns["in_use_vp"]),
        )
        if power_w is None:
            power_w = erp_kw_to_watts(
                text(row, columns["licensed_hp"]),
                text(row, columns["licensed_vp"]),
            )
    else:
        power_w = erp_kw_to_watts(text(row, columns["in_use_emrp"]))
        if power_w is None:
            power_w = erp_kw_to_watts(text(row, columns["licensed_emrp"]))
    if power_w is not None:
        record["power_w"] = power_w
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


def load_rows(path: Path, kind_hint: str | None = None) -> tuple[list[dict[str, object]], Counter[str], str, str]:
    decoded, encoding = open_text(path)
    reader = csv.reader(decoded.splitlines())
    try:
        header = next(reader)
    except StopIteration:
        raise SystemExit(f"empty CSV: {path}") from None
    kind = kind_hint or detect_kind(header)
    if kind_hint and detect_kind(header) != kind_hint:
        # Still allow explicit override only when headers match expected kind.
        detected = detect_kind(header)
        if detected != kind_hint:
            raise SystemExit(f"{path}: expected {kind_hint} headers, detected {detected}")
    columns = map_columns(header, kind)
    rejects: Counter[str] = Counter()
    by_id: dict[str, dict[str, object]] = {}
    for row in reader:
        if not any(cell.strip() for cell in row):
            continue
        record, reason = station(row, columns, kind)
        if record is None:
            rejects[reason or "rejected"] += 1
            continue
        merge_record(by_id, rejects, record)
    records = list(by_id.values())
    records.sort(key=lambda row: (int(row["frequency_hz"]), str(row["id"])))
    return records, rejects, encoding, kind


def combine(
    inputs: list[tuple[Path, str | None]],
) -> tuple[list[dict[str, object]], Counter[str], dict[str, object]]:
    rejects: Counter[str] = Counter()
    by_id: dict[str, dict[str, object]] = {}
    encodings: dict[str, str] = {}
    kinds: list[str] = []
    for path, kind_hint in inputs:
        records, file_rejects, encoding, kind = load_rows(path, kind_hint)
        encodings[str(path)] = encoding
        kinds.append(kind)
        rejects.update(file_rejects)
        for record in records:
            merge_record(by_id, rejects, record)
    records = list(by_id.values())
    records.sort(key=lambda row: (int(row["frequency_hz"]), str(row["id"])))
    meta = {"encodings": encodings, "kinds": kinds}
    return records, rejects, meta


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, help="single Ofcom TxParams CSV (auto-detect VHF/MF)")
    parser.add_argument("--vhf", type=Path, help="Ofcom TxParams VHF CSV (FM, Frequency in MHz)")
    parser.add_argument("--mf", type=Path, help="Ofcom TxParams MF CSV (AM, Frequency in kHz)")
    parser.add_argument("--out", type=Path, required=True, help="normalized NDJSON")
    args = parser.parse_args()
    inputs: list[tuple[Path, str | None]] = []
    if args.vhf:
        inputs.append((args.vhf, "vhf"))
    if args.mf:
        inputs.append((args.mf, "mf"))
    if args.input:
        inputs.append((args.input, None))
    if not inputs:
        raise SystemExit("provide --vhf and/or --mf, or legacy --input")
    for path, _ in inputs:
        if not path.is_file():
            raise SystemExit(f"input not found: {path}")
    records, rejects, meta = combine(inputs)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        "".join(json.dumps(row, ensure_ascii=False, sort_keys=True) + "\n" for row in records),
        encoding="utf-8",
        newline="\n",
    )
    encoding_values = sorted(set(meta["encodings"].values()))
    print(json.dumps({
        "source": SOURCE,
        "encoding": encoding_values[0] if len(encoding_values) == 1 else encoding_values,
        "kinds": meta["kinds"],
        "inputs": [str(path) for path, _ in inputs],
        "records": len(records),
        "services": dict(sorted(Counter(str(row["service"]) for row in records).items())),
        "countries": dict(sorted(Counter(str(row.get("country", "")) for row in records).items())),
        "rejected": dict(sorted(rejects.items())),
    }, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
