#!/usr/bin/env python3
"""Normalize asiawaves.net radio-station HTML pages into ORCBRD1 NDJSON.

Source: http://www.asiawaves.net/ — long-running community DXer reference
site covering FM/MW/SW broadcasting across South, Southeast, and Middle
East Asia. Pages follow the shape "<country>-radio.htm" (and city-level
subpages under /india/, /indonesia/, /thailand/).

Each country/city page contains one or more <table> blocks with the
column set:

  Frequency | Transmitter Power (kW) | Network | Operating Hours | Notes

Where Frequency is like "89.2 MHz" for FM and "612 kHz" for MW. Rows
where Notes says "INACTIVE" are skipped. Frequency prefix "*" is
stripped (indicates typo asterisk in source).

Rights: http://www.asiawaves.net/copy.htm — "reasonable use of this
material to be made for non-commercial purposes" is explicitly
permitted; whole-page reproduction is not. Facts (frequency + name
+ city + country) are extracted and reformatted; page HTML is not
redistributed. Attribution: asiawaves.net.

Country hint is taken from filename prefix "<cc>_...". City is taken
from filename (if in a subpath like "india/delhi-radio", city="delhi")
or left blank for country-level pages.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from collections import Counter
from pathlib import Path


SOURCE = "asiawaves_net"
SOURCE_URL_BASE = "http://www.asiawaves.net/"
FM_HZ = (76_000_000, 108_500_000)
AM_HZ = (500_000, 1_800_000)


def _snap_fm_hz(mhz: float) -> int | None:
    hz = round(mhz * 10) * 100_000
    return hz if FM_HZ[0] <= hz <= FM_HZ[1] else None


def _snap_am_hz(khz: float) -> int | None:
    hz = round(khz) * 1_000
    return hz if AM_HZ[0] <= hz <= AM_HZ[1] else None


def _slug(*parts: str) -> str:
    seed = "|".join(p or "" for p in parts).encode("utf-8")
    return hashlib.md5(seed).hexdigest()[:8]


def _parse_freq(text: str) -> tuple[int, str] | None:
    """Return (hz, service) from strings like '89.2 MHz' or '612 kHz'."""
    text = re.sub(r"[*\s]+", " ", (text or "")).strip()
    m = re.search(r"(\d{2,4}(?:[.,]\d+)?)\s*(MHz|kHz|Mhz|Khz|MHZ|KHZ)", text)
    if not m:
        return None
    try:
        val = float(m.group(1).replace(",", "."))
    except ValueError:
        return None
    unit = m.group(2).lower()
    if unit == "mhz":
        hz = _snap_fm_hz(val)
        return (hz, "fm") if hz else None
    if unit == "khz":
        hz = _snap_am_hz(val)
        return (hz, "am") if hz else None
    return None


def _parse_power_kw(text: str) -> int | None:
    m = re.search(r"(\d+(?:[.,]\d+)?)\s*kW", text or "", re.IGNORECASE)
    if not m:
        return None
    try:
        return round(float(m.group(1).replace(",", ".")) * 1000)
    except ValueError:
        return None


def load(page_path: Path, country_code: str, city: str) -> list[dict[str, object]]:
    raw = page_path.read_bytes().decode("utf-8", errors="replace")
    records: dict[str, dict[str, object]] = {}
    tables = re.findall(r"<table[^>]*>(.*?)</table>", raw, re.DOTALL)
    for tbl in tables:
        rows = re.findall(r"<tr[^>]*>(.*?)</tr>", tbl, re.DOTALL)
        if not rows:
            continue
        # header row check
        header_cells = [re.sub(r"<[^>]+>", " ", c).strip()
                        for c in re.findall(r"<t[dh][^>]*>(.*?)</t[dh]>",
                                            rows[0], re.DOTALL)]
        header_lower = [h.lower() for h in header_cells]
        if not any("freq" in h for h in header_lower):
            continue
        # column indexes
        col_freq = next((i for i, h in enumerate(header_lower) if "freq" in h), 0)
        col_power = next((i for i, h in enumerate(header_lower)
                          if "power" in h or "kw" in h), None)
        col_net = next((i for i, h in enumerate(header_lower)
                        if "network" in h or "station" in h or "name" in h), None)
        col_notes = next((i for i, h in enumerate(header_lower) if "note" in h), None)

        for row in rows[1:]:
            cells = [re.sub(r"<[^>]+>", " ", c).strip()
                     for c in re.findall(r"<t[dh][^>]*>(.*?)</t[dh]>",
                                         row, re.DOTALL)]
            if len(cells) <= col_freq:
                continue
            freq_result = _parse_freq(cells[col_freq])
            if not freq_result:
                continue
            hz, service = freq_result

            name = cells[col_net].strip() if col_net is not None and len(cells) > col_net else ""
            power_w = _parse_power_kw(cells[col_power]) if col_power is not None and len(cells) > col_power else None
            notes = cells[col_notes].strip() if col_notes is not None and len(cells) > col_notes else ""

            # skip INACTIVE
            if re.search(r"inactive|off\s*air|silent|closed", notes, re.IGNORECASE):
                continue

            slug = _slug(name, city, country_code, str(hz))
            record: dict[str, object] = {
                "id": f"{SOURCE}:{slug}:{hz}",
                "source": SOURCE, "source_id": f"{country_code}:{city}:{name}:{hz}",
                "source_url": SOURCE_URL_BASE,
                "country": country_code.upper(),
                "service": service,
                "frequency_hz": hz,
                "status": "LISTED",
            }
            if name:
                record["name"] = name[:20]
            if city:
                record["city"] = city[:10]
            if power_w is not None:
                record["power_w"] = power_w
            records[str(record["id"])] = record
    return list(records.values())


def load_all(stage_dir: Path) -> list[dict[str, object]]:
    filename_re = re.compile(r"^([a-z]{2})_(.+?)\.html$")
    all_records: dict[str, dict[str, object]] = {}
    for path in sorted(stage_dir.glob("*.html")):
        m = filename_re.match(path.name)
        if not m:
            continue
        cc = m.group(1)
        remainder = m.group(2)
        # city: if remainder has '_' and starts with country name, take second part
        # e.g. india_delhi-radio -> city=delhi. Otherwise blank.
        city = ""
        if "_" in remainder:
            parts = remainder.split("_", 1)
            if parts[1] and parts[1] != "radio" and not parts[1].startswith("index"):
                # e.g. 'delhi-radio', 'jakarta-radio', 'thai-fm-radio'
                stripped = re.sub(r"-radio.*$", "", parts[1])
                stripped = re.sub(r"-fm-radio.*$", "", stripped)
                stripped = re.sub(r"^thai-?", "", stripped)
                if stripped and stripped not in ("fm", "am"):
                    city = stripped.title()
        for r in load(path, cc, city):
            all_records[str(r["id"])] = r
    return sorted(all_records.values(),
                  key=lambda r: (int(r["frequency_hz"]), str(r["id"])))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    records = load_all(args.stage)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        "".join(json.dumps(r, ensure_ascii=False, sort_keys=True) + "\n"
                for r in records),
        encoding="utf-8", newline="\n")
    by_country = Counter(str(r["country"]) for r in records)
    by_service = Counter(str(r["service"]) for r in records)
    print(json.dumps({
        "source": SOURCE, "records": len(records),
        "services": dict(sorted(by_service.items())),
        "countries": dict(sorted(by_country.items())),
    }, ensure_ascii=True, indent=2))


if __name__ == "__main__":
    main()
