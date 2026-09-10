#!/usr/bin/env python3
"""Normalize the ANFR (Agence Nationale des Fréquences) open-data export of
French radio installations over 5 watts into ORCBRD1 NDJSON.

Source: data.gouv.fr dataset
``donnees-sur-les-installations-radioelectriques-de-plus-de-5-watts-1``.
License: Licence Ouverte 2.0 (Etalab) — CC BY 4.0 compatible, attribution
to ANFR required.

The export is a multi-table relational dump (semicolon-separated,
Latin-1 encoded) with the following relevant tables:

- ``SUP_EMETTEUR`` — one row per transmitter, filtered here to
  ``EMR_LB_SYSTEME == 'FM'`` (French FM broadcast).
- ``SUP_BANDE`` — one row per frequency band per transmitter; frequencies
  are given as start / end (``BAN_NB_F_DEB`` / ``BAN_NB_F_FIN``) with
  a unit column (``BAN_FG_UNITE``: ``M``=MHz, ``k``=kHz, ``G``=GHz).
- ``SUP_SUPPORT`` — one row per antenna support (mast, tower, building)
  with DMS coordinates.
- ``SUP_ANTENNE`` — one row per antenna, links a transmitter's
  ``AER_ID`` on a ``STA_NM_ANFR`` to a ``SUP_ID`` support.
- ``SUP_STATION`` — one row per station, links ``STA_NM_ANFR`` to
  ``ADM_ID`` (operator id, resolves in ``SUP_EXPLOITANT``).
- ``SUP_EXPLOITANT`` (in the reference zip) — operator name.

Coordinates are converted from DMS+direction to signed decimal degrees
and stored as ``latitude_e7`` / ``longitude_e7``.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import zipfile
from collections import Counter, defaultdict
from pathlib import Path


SOURCE = "anfr_installations_radioelectriques"
SOURCE_URL = "https://www.data.gouv.fr/fr/datasets/donnees-sur-les-installations-radioelectriques-de-plus-de-5-watts-1/"
FM_HZ = (76_000_000, 108_500_000)


def _rows(archive: zipfile.ZipFile, name: str):
    with archive.open(name) as f:
        text = io.TextIOWrapper(f, encoding="utf-8", newline="", errors="replace")
        for row in csv.DictReader(text, delimiter=";"):
            yield row


def _snap_fm_hz(mhz: float) -> int | None:
    hz = round(mhz * 10) * 100_000
    return hz if FM_HZ[0] <= hz <= FM_HZ[1] else None


def _band_center_mhz(row: dict) -> float | None:
    unit = (row.get("BAN_FG_UNITE") or "").strip().upper()
    scale = {"M": 1.0, "K": 0.001, "G": 1000.0}.get(unit)
    if scale is None:
        return None
    def _num(text: str) -> float | None:
        text = (text or "").strip().replace(",", ".")
        try:
            return float(text)
        except ValueError:
            return None
    lo = _num(row.get("BAN_NB_F_DEB", ""))
    hi = _num(row.get("BAN_NB_F_FIN", ""))
    if lo is None:
        return None
    center = (lo + hi) / 2 if hi is not None else lo
    return center * scale


def _dms_to_signed_e7(deg: str, minute: str, sec: str, direction: str,
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


def _slug(*parts: str) -> str:
    seed = "|".join(p or "" for p in parts).encode("utf-8")
    return hashlib.md5(seed).hexdigest()[:8]


def load(data_zip: Path, ref_zip: Path) -> list[dict[str, object]]:
    operators: dict[str, str] = {}
    with zipfile.ZipFile(ref_zip) as ref:
        for row in _rows(ref, "SUP_EXPLOITANT.txt"):
            adm_id = (row.get("ADM_ID") or "").strip()
            name = (row.get("ADM_LB_NOM") or "").strip()
            if adm_id and name:
                operators[adm_id] = name

    with zipfile.ZipFile(data_zip) as data:
        fm_emitters: dict[str, dict[str, str]] = {}
        for row in _rows(data, "SUP_EMETTEUR.txt"):
            if (row.get("EMR_LB_SYSTEME") or "").strip() == "FM":
                emr = (row.get("EMR_ID") or "").strip()
                if emr:
                    fm_emitters[emr] = {
                        "sta": (row.get("STA_NM_ANFR") or "").strip(),
                        "aer": (row.get("AER_ID") or "").strip(),
                    }

        fm_freq: dict[str, int] = {}
        for row in _rows(data, "SUP_BANDE.txt"):
            emr = (row.get("EMR_ID") or "").strip()
            if emr not in fm_emitters or emr in fm_freq:
                continue
            center = _band_center_mhz(row)
            if center is None:
                continue
            hz = _snap_fm_hz(center)
            if hz is not None:
                fm_freq[emr] = hz

        needed_stations = {info["sta"] for emr, info in fm_emitters.items() if emr in fm_freq}
        station_op: dict[str, str] = {}
        for row in _rows(data, "SUP_STATION.txt"):
            sta = (row.get("STA_NM_ANFR") or "").strip()
            if sta in needed_stations:
                adm = (row.get("ADM_ID") or "").strip()
                if adm in operators:
                    station_op[sta] = operators[adm]

        support_coords: dict[str, tuple[int, int]] = {}
        for row in _rows(data, "SUP_SUPPORT.txt"):
            sup = (row.get("SUP_ID") or "").strip()
            if not sup:
                continue
            lat = _dms_to_signed_e7(
                row.get("COR_NB_DG_LAT", ""), row.get("COR_NB_MN_LAT", ""),
                row.get("COR_NB_SC_LAT", ""), row.get("COR_CD_NS_LAT", ""),
                ("S",))
            lon = _dms_to_signed_e7(
                row.get("COR_NB_DG_LON", ""), row.get("COR_NB_MN_LON", ""),
                row.get("COR_NB_SC_LON", ""), row.get("COR_CD_EW_LON", ""),
                ("W",))
            if lat or lon:
                support_coords[sup] = (lat, lon)

        antenna_support: dict[tuple[str, str], str] = {}
        for row in _rows(data, "SUP_ANTENNE.txt"):
            sta = (row.get("STA_NM_ANFR") or "").strip()
            aer = (row.get("AER_ID") or "").strip()
            sup = (row.get("SUP_ID") or "").strip()
            if sta and aer and sup:
                antenna_support[(sta, aer)] = sup

    records: dict[str, dict[str, object]] = {}
    for emr, info in fm_emitters.items():
        hz = fm_freq.get(emr)
        if hz is None:
            continue
        sta = info["sta"]
        sup = antenna_support.get((sta, info["aer"]))
        coords = support_coords.get(sup) if sup else None
        operator = station_op.get(sta, "")

        source_id = f"emr:{emr}:sta:{sta}"
        slug = _slug(source_id, str(hz))
        record: dict[str, object] = {
            "id": f"{SOURCE}:{slug}:{hz}",
            "source": SOURCE, "source_id": source_id,
            "source_url": SOURCE_URL, "country": "FR", "service": "fm",
            "frequency_hz": hz,
        }
        if operator:
            record["name"] = operator[:20]
        if coords:
            record["latitude_e7"], record["longitude_e7"] = coords
        record["status"] = "LICENSED"
        records[str(record["id"])] = record
    return sorted(records.values(),
                  key=lambda r: (int(r["frequency_hz"]), str(r["id"])))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--data-zip", type=Path, required=True)
    parser.add_argument("--ref-zip", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    records = load(args.data_zip, args.ref_zip)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        "".join(json.dumps(r, ensure_ascii=False, sort_keys=True) + "\n"
                for r in records),
        encoding="utf-8", newline="\n")
    with_coords = sum(1 for r in records if "latitude_e7" in r)
    with_name = sum(1 for r in records if "name" in r)
    print(json.dumps({
        "source": SOURCE, "records": len(records),
        "services": {"fm": len(records)},
        "with_coords": with_coords, "with_name": with_name,
    }, ensure_ascii=True, indent=2))


if __name__ == "__main__":
    main()
