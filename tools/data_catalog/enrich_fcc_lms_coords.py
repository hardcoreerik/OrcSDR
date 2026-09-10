#!/usr/bin/env python3
"""Add coordinates to the existing FCC LMS AM/FM NDJSON by joining
``facility.dat`` → ``application_facility.dat`` → ``app_am_antenna.dat``
(for AM) and ``app_location.dat`` (for FM).

The base normalizer (:mod:`normalize_fcc_lms`) reads only ``facility.dat``,
which has callsign/service/community but no coordinates. This enrichment
step reads the license-application tables that carry the DMS coordinates
and updates the NDJSON in place with ``latitude_e7`` / ``longitude_e7``.
"""

from __future__ import annotations

import argparse
import csv
import io
import json
import zipfile
from collections import Counter
from pathlib import Path


def _rows(archive: zipfile.ZipFile, name: str):
    with archive.open(name) as f:
        text = io.TextIOWrapper(f, encoding="utf-8-sig", newline="")
        yield from csv.DictReader(text, delimiter="|")


def _dms_to_e7(deg: str, minute: str, sec: str, direction: str,
               negatives: tuple[str, ...]) -> int:
    def _num(text: str) -> float:
        text = (text or "").strip().replace(",", ".")
        try:
            return float(text)
        except ValueError:
            return 0.0
    value = _num(deg) + _num(minute) / 60 + _num(sec) / 3600
    if (direction or "").strip().upper() in negatives:
        value = -value
    return round(value * 10_000_000)


def _build_facility_to_apps(archive: zipfile.ZipFile) -> dict[str, list[str]]:
    """Return facility_id -> [application_id ...] — all applications, not
    just active ones, so we can walk back through renewal / STA / CP records
    when the current license row doesn't carry coordinates directly."""
    out: dict[str, list[str]] = {}
    for row in _rows(archive, "application_facility.dat"):
        facility_id = (row.get("afac_facility_id") or "").strip()
        app_id = (row.get("afac_application_id") or "").strip()
        if facility_id and app_id:
            out.setdefault(facility_id, []).append(app_id)
    return out


def _build_am_coords(archive: zipfile.ZipFile) -> dict[str, tuple[int, int]]:
    """application_id -> (lat_e7, lon_e7) from AM antenna table.
    Prefer the first row that carries non-zero coordinates for each app_id."""
    out: dict[str, tuple[int, int]] = {}
    for row in _rows(archive, "app_am_antenna.dat"):
        app_id = (row.get("aapp_application_id") or "").strip()
        if not app_id or app_id in out:
            continue
        lat = _dms_to_e7(row.get("lat_deg", ""), row.get("lat_min", ""),
                         row.get("lat_sec", ""), row.get("lat_dir", ""),
                         ("S",))
        lon = _dms_to_e7(row.get("long_deg", ""), row.get("long_min", ""),
                         row.get("long_sec", ""), row.get("long_dir", ""),
                         ("W",))
        if lat and lon:  # only store rows with BOTH lat and lon
            out[app_id] = (lat, lon)
    return out


def _build_fm_coords(archive: zipfile.ZipFile) -> dict[str, tuple[int, int]]:
    """application_id -> (lat_e7, lon_e7) from app_location table.
    Prefer the first row that carries non-zero coordinates for each app_id."""
    out: dict[str, tuple[int, int]] = {}
    for row in _rows(archive, "app_location.dat"):
        app_id = (row.get("aloc_aapp_application_id") or "").strip()
        if not app_id or app_id in out:
            continue
        lat = _dms_to_e7(row.get("aloc_lat_deg", ""), row.get("aloc_lat_mm", ""),
                         row.get("aloc_lat_ss", ""), row.get("aloc_lat_dir", ""),
                         ("S",))
        lon = _dms_to_e7(row.get("aloc_long_deg", ""), row.get("aloc_long_mm", ""),
                         row.get("aloc_long_ss", ""), row.get("aloc_long_dir", ""),
                         ("W",))
        if lat and lon:  # only store rows with BOTH lat and lon
            out[app_id] = (lat, lon)
    return out


def enrich(zip_path: Path, ndjson_path: Path, out_path: Path) -> dict[str, int]:
    with zipfile.ZipFile(zip_path) as archive:
        facility_to_apps = _build_facility_to_apps(archive)
        am_coords = _build_am_coords(archive)
        fm_coords = _build_fm_coords(archive)

    records: list[dict[str, object]] = []
    with ndjson_path.open(encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            records.append(json.loads(line))

    added_am = added_fm = 0
    for record in records:
        if "latitude_e7" in record:
            continue
        service = record.get("service")
        facility_id = (record.get("source_id") or "").strip()
        if not facility_id:
            continue
        apps = facility_to_apps.get(facility_id) or []
        coords = None
        pool = am_coords if service == "am" else fm_coords
        for app_id in apps:
            if app_id in pool:
                coords = pool[app_id]
                break
        if coords is None:
            continue
        lat, lon = coords
        if lat == 0 and lon == 0:
            continue
        record["latitude_e7"], record["longitude_e7"] = lat, lon
        if service == "am":
            added_am += 1
        else:
            added_fm += 1

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(
        "".join(json.dumps(r, ensure_ascii=False, sort_keys=True) + "\n"
                for r in records),
        encoding="utf-8", newline="\n")

    return {"total": len(records), "am_coords_added": added_am,
            "fm_coords_added": added_fm,
            "with_coords_after": sum(1 for r in records if "latitude_e7" in r)}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--zip", type=Path, required=True,
                        help="fcc-lms.zip (Current_LMS_Dump)")
    parser.add_argument("--input", type=Path, required=True,
                        help="existing fcc-lms-broadcast.ndjson")
    parser.add_argument("--out", type=Path, required=True,
                        help="output NDJSON with coordinates added")
    args = parser.parse_args()
    report = enrich(args.zip, args.input, args.out)
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
