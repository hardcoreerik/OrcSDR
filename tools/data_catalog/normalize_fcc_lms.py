#!/usr/bin/env python3
"""Normalize the FCC LMS public FACILITY export into station rows.

Reads the outer FCC LMS Public Database ZIP (``Current_LMS_Dump.zip``),
locates ``facility.dat`` and the FCC's own ``lkp_service_code.dat`` /
``lkp_state.dat`` lookups, and emits the canonical ORCBRD1 station-card
contract consumed by :mod:`build_broadcast_index`.

Scope: United States AM and FM only. AM services are FCC service codes
``AM`` (Full Power AM) and ``AX`` (Full Power AX). FM services are
``FM`` (Full Power FM), ``FL`` (Low Power FM), ``FX`` (FM Translator),
``FB`` (FM Booster), and ``FS`` (FM Auxiliary) — every FCC service_code
whose ``lkp_service_code.service_group_code`` is FM. Non-transmitter
rows (``FA`` Allotments, ``FR`` Rulemaking) are excluded. Rows whose
``community_served_state`` is not a US state/territory per
``lkp_state.dat`` are excluded to hold the US-only scope of this
slice; cross-border coordination records remain factually in the FCC's
LMS but are not treated as US stations by this pack.

The FCC LMS ``facility.frequency`` column stores AM tuning as kHz
(e.g. ``1190``) and FM tuning as MHz written as a floating-point
number (e.g. ``93.300003051757812`` for channel 93.3). FM values are
snapped to the nearest 100 kHz channel grid; AM values are snapped to
the nearest kHz. Coordinates and power live in ``APP_AM_ANTENNA`` /
``APP_ANTENNA`` and are not joined here; the ORCBRD1 record contract
permits optional fields to be absent when the source does not supply
them. A ``facility_status`` value like ``LICEN`` is a licensing
record, never proof that the transmitter is currently on air.
"""

from __future__ import annotations

import argparse
import csv
import io
import json
import zipfile
from collections import Counter
from pathlib import Path


SOURCE = "fcc_lms"
SOURCE_URL = "https://opendata.fcc.gov/Media/LMS-Public-Database-Files/nsck-y87u"
FACILITY_TABLE = "facility.dat"
SERVICE_LOOKUP = "lkp_service_code.dat"
STATE_LOOKUP = "lkp_state.dat"
DELIMITER = "|"
REQUIRED_COLUMNS = ("facility_id", "callsign", "service_code",
                    "community_served_city", "community_served_state",
                    "facility_status", "frequency")
RADIO_GROUPS = {"AM": "am", "FM": "fm"}
AM_HZ = (500_000, 1_800_000)
FM_HZ = (87_000_000, 108_500_000)


def _open_table(archive: zipfile.ZipFile, table: str) -> bytes:
    for name in archive.namelist():
        lower = name.lower()
        if lower == table or lower.endswith("/" + table):
            return archive.read(name)
        if lower.endswith("/" + table.replace(".dat", ".zip")) or lower == table.replace(".dat", ".zip"):
            with zipfile.ZipFile(io.BytesIO(archive.read(name))) as inner:
                for inner_name in inner.namelist():
                    if inner_name.lower().endswith(table):
                        return inner.read(inner_name)
    raise SystemExit(f"{table} not found in archive")


def _read_rows(archive: zipfile.ZipFile, table: str) -> tuple[list[str], list[dict[str, str]]]:
    text = _open_table(archive, table).decode("utf-8-sig", errors="replace")
    reader = csv.DictReader(io.StringIO(text), delimiter=DELIMITER)
    return list(reader.fieldnames or []), list(reader)


def load_service_map(archive: zipfile.ZipFile) -> dict[str, str]:
    _, rows = _read_rows(archive, SERVICE_LOOKUP)
    mapping: dict[str, str] = {}
    for row in rows:
        code = (row.get("service_code") or "").strip().upper()
        group = (row.get("service_group_code") or "").strip().upper()
        service = RADIO_GROUPS.get(group)
        if code and service:
            mapping[code] = service
    # Non-transmitter radio-group codes we must exclude explicitly.
    for excluded in ("FA", "FR"):
        mapping.pop(excluded, None)
    return mapping


def load_us_states(archive: zipfile.ZipFile) -> set[str]:
    _, rows = _read_rows(archive, STATE_LOOKUP)
    return {(row.get("state_code") or "").strip().upper()
            for row in rows
            if (row.get("country_code") or "").strip().upper() == "US"
            and (row.get("state_code") or "").strip()}


def parse_frequency(text: str, service: str) -> int | None:
    stripped = (text or "").strip()
    if not stripped:
        return None
    try:
        value = float(stripped)
    except ValueError:
        return None
    if value <= 0:
        return None
    if service == "am":
        hz = round(value) * 1_000
        return hz if AM_HZ[0] <= hz <= AM_HZ[1] else None
    if service == "fm":
        # FCC stores FM as MHz float with binary rounding noise; snap to
        # the 100 kHz channel grid (87.9, 88.1, ..., 107.9 MHz).
        hz = round(value * 10) * 100_000
        return hz if FM_HZ[0] <= hz <= FM_HZ[1] else None
    return None


def station(row: dict[str, str], services: dict[str, str], us_states: set[str]) -> dict[str, object] | None:
    service = services.get((row.get("service_code") or "").strip().upper())
    if service is None:
        return None
    facility_id = (row.get("facility_id") or "").strip()
    if not facility_id or facility_id.startswith("-"):
        # FCC placeholder / NEW rows carry negative synthetic IDs.
        return None
    state = (row.get("community_served_state") or "").strip().upper()[:2]
    if state and state not in us_states:
        return None
    frequency_hz = parse_frequency(row.get("frequency", ""), service)
    if frequency_hz is None:
        return None
    record: dict[str, object] = {
        "id": f"{SOURCE}:{facility_id}:{service}:{frequency_hz}",
        "source": SOURCE, "source_id": facility_id,
        "source_url": SOURCE_URL, "country": "US", "service": service,
        "frequency_hz": frequency_hz,
    }
    callsign = (row.get("callsign") or "").strip().upper()
    if callsign and callsign != "NEW":
        record["callsign"] = callsign
        record["name"] = callsign
    city = (row.get("community_served_city") or "").strip()
    if city:
        record["city"] = city
    if state:
        record["region"] = state
    status = (row.get("facility_status") or "").strip().upper()
    if status:
        record["status"] = status
    return record


def load(zip_path: Path) -> list[dict[str, object]]:
    with zipfile.ZipFile(zip_path) as archive:
        services = load_service_map(archive)
        us_states = load_us_states(archive)
        fields, rows = _read_rows(archive, FACILITY_TABLE)
    missing = [c for c in REQUIRED_COLUMNS if c not in fields]
    if missing:
        raise SystemExit(f"{FACILITY_TABLE} missing columns: {missing}")
    unique: dict[str, dict[str, object]] = {}
    for row in rows:
        record = station(row, services, us_states)
        if record is not None:
            unique[str(record["id"])] = record
    return sorted(unique.values(),
                  key=lambda row: (int(row["frequency_hz"]), str(row["id"])))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True,
                        help="FCC LMS Current_LMS_Dump.zip")
    parser.add_argument("--out", type=Path, required=True,
                        help="normalized NDJSON")
    args = parser.parse_args()
    records = load(args.input)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        "".join(json.dumps(row, ensure_ascii=False, sort_keys=True) + "\n"
                for row in records),
        encoding="utf-8", newline="\n")
    by_service = Counter(str(row["service"]) for row in records)
    by_region = Counter(str(row.get("region", "")) for row in records)
    print(json.dumps({
        "source": SOURCE, "records": len(records),
        "services": dict(sorted(by_service.items())),
        "regions_top10": dict(sorted(by_region.items(), key=lambda kv: -kv[1])[:10]),
    }, indent=2))


if __name__ == "__main__":
    main()
