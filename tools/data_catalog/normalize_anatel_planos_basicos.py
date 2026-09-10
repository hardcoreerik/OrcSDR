#!/usr/bin/env python3
"""Normalize Anatel (Brasil) Planos Básicos de Radiodifusão (PBFM + PBOM)
into ORCBRD1 station-card NDJSON.

Source: Anatel channel-allotment plans for FM (Onda Ultra-Curta) and OM
(Onda Média = AM), distributed as ``planos_basicos.zip`` from
``dados.gov.br`` / Anatel dashboards. The zip also contains PBOT (TV
allotments), PBTVD (Digital TV), and Contorno_Protegido (per-station
protected-contour polygons) which are outside AM/FM/shortwave scope
and are not consumed by this normalizer.

Licence context (verify before shipping in a signed catalog):
Anatel's parent site publishes under CC BY-ND 3.0 Unported. The
Brazilian national open-data guidance excludes ND variants from
"open data," and dados.gov.br national policy is CC BY 4.0. This
normalizer stages the data locally so it can be reviewed and used
for tooling; the ledger keeps ``release_allowed: false`` until an
operator confirms the specific dataset's licence permits redistribution
in a downstream device pack.

Fields extracted:
- FM (PBFM.csv): _id, Município-UF, Frequência PBFM (MHz), Classe PBFM,
  ERP PBFM (kW), Latitude Decimal, Longitude Decimal, UF PBFM
- AM (PBOM.csv): _id, Município-UF, Frequência PBOM (kHz), Classe PBOM,
  ERP PBOM (kW), Latitude Decimal PBOM, Longitude Decimal PBOM, UF PBOM
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


SOURCE = "anatel_planos_basicos"
SOURCE_URL = "https://dados.gov.br/dados/conjuntos-dados/radiodifusao---plano-basico"
AM_HZ = (500_000, 1_800_000)
FM_HZ = (76_000_000, 108_500_000)


def _snap_am_hz(khz: float) -> int | None:
    hz = round(khz) * 1_000
    return hz if AM_HZ[0] <= hz <= AM_HZ[1] else None


def _snap_fm_hz(mhz: float) -> int | None:
    hz = round(mhz * 10) * 100_000
    return hz if FM_HZ[0] <= hz <= FM_HZ[1] else None


def _parse_float(text: str) -> float | None:
    text = (text or "").strip().replace(",", ".")
    if not text:
        return None
    try:
        return float(text)
    except ValueError:
        return None


def _slug(*parts: str) -> str:
    seed = "|".join(p or "" for p in parts).encode("utf-8")
    return hashlib.md5(seed).hexdigest()[:8]


def _city_from_municipio(text: str) -> tuple[str, str]:
    """Split 'Rio de Janeiro - RJ' into ('Rio de Janeiro', 'RJ')."""
    text = (text or "").strip()
    m = re.match(r"^(.*?)\s*-\s*([A-Z]{2})\s*$", text)
    if m:
        return m.group(1).strip(), m.group(2).strip()
    return text, ""


def _record_from_fm_row(row: dict[str, str]) -> dict[str, object] | None:
    freq_text = row.get("Frequência PBFM", "")
    freq_mhz = _parse_float(freq_text)
    if freq_mhz is None:
        return None
    hz = _snap_fm_hz(freq_mhz)
    if hz is None:
        return None
    plan_id = (row.get("_id") or "").strip()
    city, uf = _city_from_municipio(row.get("Município-UF", ""))
    if not uf:
        uf = (row.get("UF PBFM") or "").strip()
    lat = _parse_float(row.get("Latitude Decimal", ""))
    lon = _parse_float(row.get("Longitude Decimal", ""))
    erp_kw = _parse_float(row.get("ERP PBFM", ""))
    klass = (row.get("Classe PBFM") or "").strip()
    caracter = (row.get("Caráter PBFM") or "").strip()

    source_id = f"pbfm:{plan_id}:{hz}"
    slug = _slug(source_id, city, uf)
    record: dict[str, object] = {
        "id": f"{SOURCE}:{slug}:{hz}",
        "source": SOURCE, "source_id": source_id,
        "source_url": SOURCE_URL, "country": "BR", "service": "fm",
        "frequency_hz": hz,
    }
    if city:
        record["city"] = city[:10]
    if uf:
        record["region"] = uf[:5]
    if lat is not None and lon is not None and (lat or lon):
        record["latitude_e7"] = round(lat * 10_000_000)
        record["longitude_e7"] = round(lon * 10_000_000)
    if erp_kw is not None and erp_kw > 0:
        record["power_w"] = round(erp_kw * 1000)
    if klass:
        record["mode"] = klass[:8]
    record["status"] = caracter.upper()[:15] or "ALLOTTED"
    return record


def _record_from_am_row(row: dict[str, str]) -> dict[str, object] | None:
    freq_text = row.get("Frequência PBOM", "")
    freq_khz = _parse_float(freq_text)
    if freq_khz is None:
        return None
    hz = _snap_am_hz(freq_khz)
    if hz is None:
        return None
    plan_id = (row.get("_id") or "").strip()
    city, uf = _city_from_municipio(row.get("Município-UF", ""))
    if not uf:
        uf = (row.get("UF PBOM") or "").strip()
    lat = _parse_float(row.get("Latitude Decimal PBOM", ""))
    lon = _parse_float(row.get("Longitude Decimal PBOM", ""))
    if lat is None:
        lat = _parse_float(row.get("Latitude Decimal", ""))
    if lon is None:
        lon = _parse_float(row.get("Longitude Decimal", ""))
    erp_kw = _parse_float(row.get("ERP PBOM", ""))
    klass = (row.get("Classe PBOM") or "").strip()
    caracter = (row.get("Caráter PBOM") or "").strip()

    source_id = f"pbom:{plan_id}:{hz}"
    slug = _slug(source_id, city, uf)
    record: dict[str, object] = {
        "id": f"{SOURCE}:{slug}:{hz}",
        "source": SOURCE, "source_id": source_id,
        "source_url": SOURCE_URL, "country": "BR", "service": "am",
        "frequency_hz": hz,
    }
    if city:
        record["city"] = city[:10]
    if uf:
        record["region"] = uf[:5]
    if lat is not None and lon is not None and (lat or lon):
        record["latitude_e7"] = round(lat * 10_000_000)
        record["longitude_e7"] = round(lon * 10_000_000)
    if erp_kw is not None and erp_kw > 0:
        record["power_w"] = round(erp_kw * 1000)
    if klass:
        record["mode"] = klass[:8]
    record["status"] = caracter.upper()[:15] or "ALLOTTED"
    return record


def load(zip_path: Path) -> list[dict[str, object]]:
    records: dict[str, dict[str, object]] = {}
    with zipfile.ZipFile(zip_path) as archive:
        for name, parser in (("PBFM.csv", _record_from_fm_row),
                             ("PBOM.csv", _record_from_am_row)):
            try:
                with archive.open(name) as f:
                    text = io.TextIOWrapper(f, encoding="utf-8-sig", newline="")
                    for row in csv.DictReader(text, delimiter=";"):
                        rec = parser(row)
                        if rec is not None:
                            records[str(rec["id"])] = rec
            except KeyError:
                continue
    return sorted(records.values(),
                  key=lambda r: (int(r["frequency_hz"]), str(r["id"])))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True,
                        help="Anatel planos_basicos.zip")
    parser.add_argument("--out", type=Path, required=True,
                        help="normalized NDJSON")
    args = parser.parse_args()
    records = load(args.input)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        "".join(json.dumps(r, ensure_ascii=False, sort_keys=True) + "\n"
                for r in records),
        encoding="utf-8", newline="\n")
    print(json.dumps({
        "source": SOURCE, "records": len(records),
        "services": dict(sorted(Counter(str(r["service"]) for r in records).items())),
        "with_coords": sum(1 for r in records if "latitude_e7" in r),
        "with_power": sum(1 for r in records if "power_w" in r),
    }, ensure_ascii=True, indent=2))


if __name__ == "__main__":
    main()
