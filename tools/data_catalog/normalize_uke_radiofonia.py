#!/usr/bin/env python3
"""Normalize UKE (Urząd Komunikacji Elektronicznej) broadcast permit CSVs
into ORCBRD1 station-card NDJSON.

Source: dane.gov.pl dataset 723,
"Wykazy pozwoleń radiowych dla stacji radiofonicznych pracujących w
służbie radiodyfuzyjnej" — Lists of radio permits for radio broadcasting
stations working in the broadcasting service.

License: Poland's Open Data Act (Ustawa o otwartych danych) — public
information republishable with source attribution. dane.gov.pl default.

The zip contains monthly snapshots. Relevant files (FM + AM):

- ``pozwolenie_ukf_h_<date>.csv`` — UKF (VHF FM) horizontal polarization
- ``pozwolenie_ukf_r_<date>.csv`` — UKF reserve
- ``pozwolenia_falesrednie_h_<date>.csv`` — Fale Średnie (Medium Wave / AM)
- ``pozwolenia_falesrednie_r_<date>.csv`` — MW reserve
- (DAB and long-wave files are outside AM/FM scope for this pass)

CSV columns (UKF, semicolon-delimited, cp1250):
``Lp.`` | ``Status`` | ``Numer pozwolenia`` | ``Data`` | ``Ważny od`` |
``Ważny do`` | ``Wnioskodawca`` | ``Nadawca`` | ``Program`` |
``Identyfikator`` | ``F [MHz]`` | ``Nazwa stacji`` | ``Lokalizacja`` |
``Województwo`` | ``Typ nadajnika`` | ``Producent`` | ``Moc [kW]`` |
… ``Dł. geogr.`` | ``Sz. geogr.`` | ``Układ wsp.`` | … | ``ERP [kW]`` | …
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import re
import zipfile
from collections import Counter
from pathlib import Path


SOURCE = "uke_radiofonia_permits"
SOURCE_URL = "https://dane.gov.pl/pl/dataset/723,wykazy-pozwolen-radiowych-dla-stacji-radiofonicznych-pracujacych-w-suzbie-radiodyfuzyjnej"
AM_HZ = (500_000, 1_800_000)
FM_HZ = (76_000_000, 108_500_000)


def _snap_am_hz(khz: float) -> int | None:
    hz = round(khz) * 1_000
    return hz if AM_HZ[0] <= hz <= AM_HZ[1] else None


def _snap_fm_hz(mhz: float) -> int | None:
    hz = round(mhz * 10) * 100_000
    return hz if FM_HZ[0] <= hz <= FM_HZ[1] else None


_DMS = re.compile(r'^\s*(\d{1,3})\s*([NSEW])\s*(\d{1,2})[\'′](\d{1,2}(?:[.,]\d+)?)["″]\s*$', re.IGNORECASE)


def _dms_to_e7(text: str, default_positive: bool) -> int:
    m = _DMS.match(text or "")
    if not m:
        return 0
    deg = float(m.group(1))
    direction = m.group(2).upper()
    minute = float(m.group(3))
    sec = float(m.group(4).replace(",", "."))
    value = deg + minute / 60 + sec / 3600
    if direction in ("S", "W"):
        value = -value
    elif direction not in ("N", "E") and not default_positive:
        value = -value
    return round(value * 10_000_000)


def _parse_float(text: str) -> float | None:
    text = (text or "").strip().replace(",", ".").replace("\xa0", "")
    if not text:
        return None
    try:
        return float(text)
    except ValueError:
        return None


def _slug(*parts: str) -> str:
    seed = "|".join(p or "" for p in parts).encode("utf-8")
    return hashlib.md5(seed).hexdigest()[:8]


def _read_csv(archive: zipfile.ZipFile, filename: str) -> list[dict[str, str]]:
    with archive.open(filename) as f:
        data = f.read()
    # AM CSVs are UTF-8; FM CSVs are cp1250; detect by presence of BOM or high-bit patterns
    for enc in ("utf-8-sig", "utf-8", "cp1250"):
        try:
            text = data.decode(enc)
            break
        except UnicodeDecodeError:
            continue
    else:
        text = data.decode("cp1250", errors="replace")
    reader = csv.DictReader(io.StringIO(text), delimiter=";")
    return [row for row in reader]


def _record(row: dict[str, str], service: str, freq_col: str, freq_unit: str,
            source_file: str) -> dict[str, object] | None:
    freq = _parse_float(row.get(freq_col, ""))
    if freq is None:
        return None
    if freq_unit == "MHz":
        hz = _snap_fm_hz(freq)
    else:
        hz = _snap_am_hz(freq)
    if hz is None:
        return None
    permit = (row.get("Numer pozwolenia/decyzji") or "").strip()
    site = (row.get("Nazwa stacji") or "").strip()
    location = (row.get("Lokalizacja stacji") or "").strip()
    voiv = (row.get("Województwo") or "").strip().title()
    program = (row.get("Program") or row.get("Nadawca") or row.get("Wnioskodawca") or "").strip()
    lat = _dms_to_e7(row.get("Sz. geogr.", ""), default_positive=True)
    lon = _dms_to_e7(row.get("Dł. geogr.", ""), default_positive=True)
    max_power_kw = _parse_float(row.get("Maksymalna dopuszczalna moc wyjściowa nadajnika", ""))
    erp_kw = _parse_float(row.get("ERP[kW]", ""))
    status = (row.get("Status") or "").strip().upper()[:15] or "LICENSED"

    source_id = f"{source_file}:{permit or site}:{hz}"
    slug = _slug(source_id, program)
    record: dict[str, object] = {
        "id": f"{SOURCE}:{slug}:{hz}",
        "source": SOURCE, "source_id": source_id,
        "source_url": SOURCE_URL, "country": "PL", "service": service,
        "frequency_hz": hz,
    }
    if program:
        record["name"] = program[:20]
    if site:
        record["city"] = site[:10]
    if voiv:
        record["region"] = voiv[:5]
    if lat or lon:
        record["latitude_e7"] = lat
        record["longitude_e7"] = lon
    power_w = None
    if erp_kw is not None and erp_kw > 0:
        power_w = round(erp_kw * 1000)
    elif max_power_kw is not None and max_power_kw > 0:
        power_w = round(max_power_kw * 1000)
    if power_w is not None:
        record["power_w"] = power_w
    record["status"] = status
    return record


def load(zip_path: Path) -> list[dict[str, object]]:
    records: dict[str, dict[str, object]] = {}
    with zipfile.ZipFile(zip_path) as z:
        names = z.namelist()
        for name in names:
            lower = name.lower()
            if "pozwolenie_ukf" in lower or "pozwolenia_ukf" in lower:
                service, freq_col, freq_unit = "fm", "F [MHz]", "MHz"
            elif "falesrednie" in lower:
                service, freq_col, freq_unit = "am", "F [kHz]", "kHz"
            else:
                continue
            rows = _read_csv(z, name)
            # some AM CSVs may name column "Częstotliwość"; try both
            if freq_col not in (rows[0].keys() if rows else []):
                alt = next((c for c in (rows[0].keys() if rows else []) if "kHz" in c or "MHz" in c or "częstot" in c.lower()), None)
                if alt:
                    freq_col = alt
            for row in rows:
                rec = _record(row, service, freq_col, freq_unit, name)
                if rec is not None:
                    records[str(rec["id"])] = rec
    return sorted(records.values(),
                  key=lambda r: (int(r["frequency_hz"]), str(r["id"])))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    records = load(args.input)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        "".join(json.dumps(r, ensure_ascii=False, sort_keys=True) + "\n"
                for r in records),
        encoding="utf-8", newline="\n")
    with_coords = sum(1 for r in records if "latitude_e7" in r)
    print(json.dumps({
        "source": SOURCE, "records": len(records),
        "services": dict(sorted(Counter(str(r["service"]) for r in records).items())),
        "with_coords": with_coords,
    }, ensure_ascii=True, indent=2))


if __name__ == "__main__":
    main()
