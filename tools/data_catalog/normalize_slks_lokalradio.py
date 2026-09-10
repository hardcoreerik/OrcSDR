#!/usr/bin/env python3
"""Normalize the Danish Culture Ministry (Slots- og Kulturstyrelsen / SLKS)
"Oversigt over frekvenser til lokalradiovirksomhed" PDF into ORCBRD1 NDJSON.

Source: SLKS PDF at
``slks.dk/fileadmin/user_upload/dokumenter/KS/medier/radio/Lokalradio/
Oversigt_over_frekvenser_til_lokalradiovirksomhed.pdf``.

Structure: two sections — "Ikkekommercielle lokalfrekvenser"
(non-commercial local frequencies) and "Kommercielle lokalfrekvenser"
(commercial local frequencies). Each section is a two-column list
"<freq MHz>  <municipality>" that PyMuPDF's ``get_text`` returns as
two adjacent lines. No coordinates, no callsign, no power — just
frequency + municipality + section (which maps to `mode` field).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from collections import Counter
from pathlib import Path


SOURCE = "slks_lokalradio_freq"
SOURCE_URL = "https://slks.dk/fileadmin/user_upload/dokumenter/KS/medier/radio/Lokalradio/Oversigt_over_frekvenser_til_lokalradiovirksomhed.pdf"
FM_HZ = (76_000_000, 108_500_000)


def _snap_fm_hz(mhz: float) -> int | None:
    hz = round(mhz * 10) * 100_000
    return hz if FM_HZ[0] <= hz <= FM_HZ[1] else None


def _slug(*parts: str) -> str:
    seed = "|".join(p or "" for p in parts).encode("utf-8")
    return hashlib.md5(seed).hexdigest()[:8]


def load(pdf_path: Path) -> list[dict[str, object]]:
    import fitz
    doc = fitz.open(pdf_path)
    all_lines: list[str] = []
    for page in doc:
        all_lines.extend(page.get_text().split("\n"))

    records: dict[str, dict[str, object]] = {}
    current_section = ""
    i = 0
    while i < len(all_lines):
        line = all_lines[i].strip()
        if "Ikkekommerciel" in line:
            current_section = "non-commercial"
        elif "Kommerciel" in line and "Ikkekommerciel" not in line:
            current_section = "commercial"
        m = re.match(r"^(\d{2,3}[.,]\d)\s*$", line)
        if m:
            freq_str = m.group(1).replace(",", ".")
            try:
                freq_mhz = float(freq_str)
            except ValueError:
                i += 1; continue
            hz = _snap_fm_hz(freq_mhz)
            if hz is None:
                i += 1; continue
            city = ""
            for j in range(i + 1, min(i + 3, len(all_lines))):
                nxt = all_lines[j].strip()
                if nxt and not re.match(r"^\d", nxt) and len(nxt) < 60:
                    city = nxt
                    break
            if not city:
                i += 1; continue
            slug = _slug(city, str(hz), current_section)
            record: dict[str, object] = {
                "id": f"{SOURCE}:{slug}:{hz}",
                "source": SOURCE,
                "source_id": f"{city}:{hz}:{current_section or 'unknown'}",
                "source_url": SOURCE_URL,
                "country": "DK",
                "service": "fm",
                "frequency_hz": hz,
                "city": city[:10],
                "status": "ALLOTTED",
            }
            if current_section:
                record["mode"] = current_section[:8]
            records[str(record["id"])] = record
        i += 1

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
    by_section = Counter(str(r.get("mode", "")) for r in records)
    print(json.dumps({
        "source": SOURCE, "records": len(records),
        "services": {"fm": len(records)},
        "sections": dict(sorted(by_section.items())),
    }, ensure_ascii=True, indent=2))


if __name__ == "__main__":
    main()
