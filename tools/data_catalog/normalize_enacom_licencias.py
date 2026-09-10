#!/usr/bin/env python3
"""Normalize the ENACOM (Argentina) "Licencias Adjudicadas" FM licence table
into ORCBRD1 station-card NDJSON.

Source: ENACOM (Ente Nacional de Comunicaciones) — Argentine broadcast
regulator. The list ``LICENCIAS ADJUDICADAS`` on the ENACOM public portal
sits behind a JavaScript grid that does not expose bulk export. Users
copy the visible rows out of the browser. Columns per row (tab-separated
in the copy-paste form):

  NOMBRE          station name
  FRECUENCIA      MHz (comma or dot decimal, optional "MHz"/"MHZ" suffix)
  PROVINCIA       province (CABA, BUENOS AIRES, CORDOBA, SANTA FE, ...)
  LOCALIDAD       city
  FECHA           adjudication date (optional)

Some rows have blank frequency; those are dropped. Frequency snap: FM
0.1 MHz. Country: AR. Service: fm. Source tier: official_regulator.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from collections import Counter
from pathlib import Path


SOURCE = "enacom_licencias_adjudicadas"
SOURCE_URL = "https://www.enacom.gob.ar/licencias"
FM_HZ = (76_000_000, 108_500_000)


PROVINCE_CODE = {
    "CABA": "CABA",
    "BUENOS AIRES": "BsAs",
    "CORDOBA": "Cba",
    "CÓRDOBA": "Cba",
    "SANTA FE": "SF",
    "MENDOZA": "Mza",
    "ENTRE RIOS": "ER",
    "ENTRE RÍOS": "ER",
    "TUCUMAN": "Tuc",
    "TUCUMÁN": "Tuc",
    "CHACO": "Cha",
    "SALTA": "Sal",
    "NEUQUEN": "Nqn",
    "NEUQUÉN": "Nqn",
    "SAN JUAN": "SJ",
    "CORRIENTES": "Ctes",
    "LA PAMPA": "LP",
    "MISIONES": "Mis",
    "RIO NEGRO": "RN",
    "RÍO NEGRO": "RN",
    "CHUBUT": "Chb",
    "SANTA CRUZ": "SC",
    "TIERRA DEL FUEGO": "TF",
    "JUJUY": "Juy",
    "CATAMARCA": "Cat",
    "SANTIAGO DEL ESTERO": "SdE",
    "LA RIOJA": "LR",
    "SAN LUIS": "SL",
    "FORMOSA": "For",
}


def _snap_fm_hz(mhz: float) -> int | None:
    hz = round(mhz * 10) * 100_000
    return hz if FM_HZ[0] <= hz <= FM_HZ[1] else None


def _slug(*parts: str) -> str:
    seed = "|".join(p or "" for p in parts).encode("utf-8")
    return hashlib.md5(seed).hexdigest()[:8]


def _parse_freq(text: str) -> float | None:
    m = re.search(r"(\d{2,3}[.,]\d+)", text or "")
    if not m:
        return None
    try:
        return float(m.group(1).replace(",", "."))
    except ValueError:
        return None


def load(path: Path) -> list[dict[str, object]]:
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    records: dict[str, dict[str, object]] = {}
    for line in lines:
        parts = [p.strip() for p in line.split("\t")]
        if len(parts) < 4:
            continue
        name, freq_raw, province, city = parts[0], parts[1], parts[2], parts[3]
        date = parts[4] if len(parts) > 4 else ""
        if name.upper() == "NOMBRE":
            continue  # header row
        freq_mhz = _parse_freq(freq_raw)
        if freq_mhz is None:
            continue
        hz = _snap_fm_hz(freq_mhz)
        if hz is None:
            continue
        province_code = PROVINCE_CODE.get(province.upper(), province[:5])
        slug = _slug(name, city, str(hz), province)
        record: dict[str, object] = {
            "id": f"{SOURCE}:{slug}:{hz}",
            "source": SOURCE,
            "source_id": f"{name}:{city}:{hz}",
            "source_url": SOURCE_URL,
            "country": "AR",
            "service": "fm",
            "frequency_hz": hz,
            "status": "LICENSED",
        }
        if name and name != "-":
            record["name"] = name[:20]
        if city:
            record["city"] = city[:10]
        if province_code:
            record["region"] = province_code[:5]
        if date:
            record["mode"] = date[:8]
        records[str(record["id"])] = record
    return sorted(records.values(),
                  key=lambda r: (int(r["frequency_hz"]), str(r["id"])))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True,
                        help="ENACOM licencias adjudicadas TSV paste")
    parser.add_argument("--out", type=Path, required=True,
                        help="normalized NDJSON")
    args = parser.parse_args()
    records = load(args.input)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        "".join(json.dumps(r, ensure_ascii=False, sort_keys=True) + "\n"
                for r in records),
        encoding="utf-8", newline="\n")
    by_prov = Counter(str(r.get("region", "")) for r in records)
    print(json.dumps({
        "source": SOURCE, "records": len(records),
        "services": {"fm": len(records)},
        "provinces": dict(sorted(by_prov.items(), key=lambda kv: -kv[1])),
    }, ensure_ascii=True, indent=2))


if __name__ == "__main__":
    main()
