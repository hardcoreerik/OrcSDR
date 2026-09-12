#!/usr/bin/env python3
"""Normalize IFT México "Infraestructura de Estaciones AM/FM" XLSX into
ORCBRD1 station-card NDJSON.

Source: Instituto Federal de Telecomunicaciones (IFT) — Registro Público
de Concesiones. Monthly refresh at
``rpc.ift.org.mx/vrpc/assets/publish/uploads/infraestructura/``.
Distributed under Mexico's federal Términos y Condiciones de Uso for
open data (CC BY-like; attribution required, redistribution permitted).

Sheet layout (one sheet, "AM-FM"):
- Row 1: title "INFRAESTRUCTURA DE AM-FM"
- Row 3: header row 1 — FOLIO ELECTRONICO, POBLACION A SERVIR (spans 2),
  TIPO, TIPO DE USO, CONCESIONARIO, DISTINTIVO, BANDA, FRECUENCIA,
  TIPO DE FRECUENCIA
- Row 4: header row 2 — sub-headers POBLACION, ESTADO
- Rows 5..N: data

Columns (1-indexed by pandas position):
  [1] FOLIO ELECTRONICO — Anatel-style unique application id, used as source_id
  [2] POBLACION — city / community of licence
  [3] ESTADO — Mexican state (Aguascalientes, Jalisco, …)
  [4] TIPO — CONCESIÓN / PERMISO / AUTORIZACIÓN
  [5] TIPO DE USO — COMERCIAL, SOCIAL COMUNITARIA, PÚBLICA, etc.
  [6] CONCESIONARIO — licensee / operator
  [7] DISTINTIVO — callsign (Mexican FM callsigns start with XH, AM with XE)
  [8] BANDA — "AM" or "FM"
  [9] FRECUENCIA — MHz for FM, kHz for AM
  [10] TIPO DE FRECUENCIA — PRINCIPAL / COMPLEMENTARIA

No coordinates in this file. Callsigns are the strongest identifier.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from collections import Counter
from pathlib import Path


SOURCE = "ift_infraestructura_am_fm"
SOURCE_URL = "https://rpc.ift.org.mx/vrpc/visor/downloads"
AM_HZ = (500_000, 1_800_000)
FM_HZ = (76_000_000, 108_500_000)


def _snap_am_hz(khz: float) -> int | None:
    hz = round(khz) * 1_000
    return hz if AM_HZ[0] <= hz <= AM_HZ[1] else None


def _snap_fm_hz(mhz: float) -> int | None:
    hz = round(mhz * 10) * 100_000
    return hz if FM_HZ[0] <= hz <= FM_HZ[1] else None


def _parse_float(text: object) -> float | None:
    if text is None:
        return None
    try:
        return float(str(text).strip().replace(",", "."))
    except ValueError:
        return None


def _clean(text: object) -> str:
    if text is None:
        return ""
    return str(text).strip()


def _slug(*parts: str) -> str:
    seed = "|".join(p or "" for p in parts).encode("utf-8")
    return hashlib.md5(seed).hexdigest()[:8]


def load(xlsx_path: Path) -> list[dict[str, object]]:
    from openpyxl import load_workbook
    wb = load_workbook(xlsx_path, read_only=True, data_only=True)
    ws = wb["AM-FM"]

    records: dict[str, dict[str, object]] = {}
    for i, row in enumerate(ws.iter_rows(values_only=True)):
        if i < 4:  # skip title + double-header rows
            continue
        # cell layout: [None, FOLIO, POBLACION, ESTADO, TIPO, TIPO_USO,
        #               CONCESIONARIO, DISTINTIVO, BANDA, FREQ, TIPO_FREQ]
        if len(row) < 11:
            continue
        folio = _clean(row[1])
        pop = _clean(row[2])
        estado = _clean(row[3])
        tipo = _clean(row[4])
        uso = _clean(row[5])
        concesionario = _clean(row[6])
        callsign = _clean(row[7]).upper()
        banda = _clean(row[8]).upper()
        freq = _parse_float(row[9])
        tipo_freq = _clean(row[10]).upper()

        if not folio or freq is None or not banda:
            continue

        if banda == "FM":
            service = "fm"
            hz = _snap_fm_hz(freq)
        elif banda == "AM":
            service = "am"
            hz = _snap_am_hz(freq)
        else:
            continue
        if hz is None:
            continue

        source_id = folio
        slug = _slug(source_id, callsign)
        record: dict[str, object] = {
            "id": f"{SOURCE}:{slug}:{hz}",
            "source": SOURCE, "source_id": source_id,
            "source_url": SOURCE_URL, "country": "MX", "service": service,
            "frequency_hz": hz,
        }
        if callsign:
            record["callsign"] = callsign[:15]
            record["name"] = callsign[:20]
        elif concesionario:
            record["name"] = concesionario[:20]
        if pop:
            record["city"] = pop[:10]
        if estado:
            record["region"] = estado[:5]
        if tipo_freq:
            record["mode"] = tipo_freq[:8]
        # status field carries "CONCESIÓN"/"PERMISO"/"AUTORIZACIÓN"
        record["status"] = tipo.upper()[:15] or "LICENSED"
        records[str(record["id"])] = record

    return sorted(records.values(),
                  key=lambda r: (int(r["frequency_hz"]), str(r["id"])))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True,
                        help="IFT 01_infraestructura_AM_FM_*.xlsx")
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
        "with_callsign": sum(1 for r in records if "callsign" in r),
    }, ensure_ascii=True, indent=2))


if __name__ == "__main__":
    main()
