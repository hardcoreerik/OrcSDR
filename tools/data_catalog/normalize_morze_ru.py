#!/usr/bin/env python3
"""Normalize the morze.ru Russian FM/UKV radio station HTML table into
ORCBRD1 station-card NDJSON.

Source: morze.ru community radio-station reference site. The page
``radio_tv/radio_tv1.htm`` carries a table of Moscow FM/UKV stations
with columns:

- Frequency (MHz, comma decimal)
- Station name
- Transmitter location + power (kW)
- Frequency history

A second table lists transmitter site coordinates in DMS
(``38E05 55N47`` style) for each Moscow FM site.

Encoding: Windows-1251 (cyrillic).
Rights: community/reference site, no open licence declared.
Source tier: general_web.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from collections import Counter
from pathlib import Path


SOURCE = "morze_ru_radio"
SOURCE_URL = "https://www.morze.ru/radio_tv/radio_tv1.htm"
FM_HZ = (65_000_000, 108_500_000)


def _snap_fm_hz(mhz: float) -> int | None:
    hz = round(mhz * 10) * 100_000
    return hz if FM_HZ[0] <= hz <= FM_HZ[1] else None


def _slug(*parts: str) -> str:
    seed = "|".join(p or "" for p in parts).encode("utf-8")
    return hashlib.md5(seed).hexdigest()[:8]


def _parse_freq_str(text: str) -> float | None:
    text = (text or "").strip().replace(",", ".")
    m = re.match(r"^(\d{2,3}(?:\.\d+)?)$", text)
    if not m:
        return None
    try:
        return float(m.group(1))
    except ValueError:
        return None


def _parse_dms(text: str, positive_char: str) -> int:
    """Parse morze.ru DMS format like '38E05' or '55N47' — degrees then
    direction letter then minutes. Returns E7 signed integer."""
    m = re.match(r"^(\d+)([NSEW])(\d+)$", (text or "").strip())
    if not m:
        return 0
    deg = int(m.group(1))
    direction = m.group(2)
    minutes = int(m.group(3))
    val = deg + minutes / 60
    if direction in ("S", "W"):
        val = -val
    return round(val * 10_000_000)


def _parse_power_kw(text: str) -> int | None:
    """From '..МСК / 5,0' extract 5.0 kW → 5000 W."""
    m = re.search(r"/\s*(\d+(?:[.,]\d+)?)", text or "")
    if not m:
        return None
    try:
        return round(float(m.group(1).replace(",", ".")) * 1000)
    except ValueError:
        return None


def _parse_site(text: str) -> str:
    """Extract the transmitter site name (before the '/') from
    'Останкино МСК / 5,0' → 'Останкино МСК'."""
    return (text or "").split("/")[0].strip()


def _extract_site_coords(html: str) -> dict[str, tuple[int, int]]:
    """From the second table (Transmitter sites), build
    ``site_name -> (lat_e7, lon_e7)``."""
    coords: dict[str, tuple[int, int]] = {}
    for tbl in re.findall(r"<table[^>]*>(.*?)</table>", html, re.DOTALL):
        rows = re.findall(r"<tr[^>]*>(.*?)</tr>", tbl, re.DOTALL)
        if not rows or "координаты" not in rows[0].lower() and "Географические" not in rows[0]:
            continue
        for row in rows[1:]:
            cells = [re.sub(r"<[^>]+>", "", c).strip()
                     for c in re.findall(r"<t[dh][^>]*>(.*?)</t[dh]>", row, re.DOTALL)]
            if len(cells) < 3:
                continue
            site = cells[0]
            dms = cells[2]
            parts = dms.split()
            if len(parts) < 2:
                continue
            lon_e7 = _parse_dms(parts[0], "E")
            lat_e7 = _parse_dms(parts[1], "N")
            if site and (lat_e7 or lon_e7):
                coords[site] = (lat_e7, lon_e7)
    return coords


def load(html_path: Path) -> list[dict[str, object]]:
    raw = html_path.read_bytes().decode("windows-1251", errors="replace")

    site_coords = _extract_site_coords(raw)

    records: dict[str, dict[str, object]] = {}
    for tbl in re.findall(r"<table[^>]*>(.*?)</table>", raw, re.DOTALL):
        rows = re.findall(r"<tr[^>]*>(.*?)</tr>", tbl, re.DOTALL)
        if not rows:
            continue
        header = re.sub(r"<[^>]+>", " ", rows[0]).lower()
        # only tables whose first row mentions "частоты" or "наименование"
        if "частоты" not in header and "наименование" not in header:
            continue
        # only if this looks like a station-list table with 3+ columns
        header_cells = [re.sub(r"<[^>]+>", "", c).strip()
                        for c in re.findall(r"<t[dh][^>]*>(.*?)</t[dh]>", rows[0], re.DOTALL)]
        if len(header_cells) < 3:
            continue
        for row in rows[1:]:
            cells = [re.sub(r"<[^>]+>", " ", c).strip()
                     for c in re.findall(r"<t[dh][^>]*>(.*?)</t[dh]>", row, re.DOTALL)]
            if len(cells) < 3:
                continue
            freq_mhz = _parse_freq_str(cells[0])
            if freq_mhz is None:
                continue
            hz = _snap_fm_hz(freq_mhz)
            if hz is None:
                continue
            name = cells[1]
            site_and_power = cells[2] if len(cells) >= 3 else ""
            site = _parse_site(site_and_power)
            power_w = _parse_power_kw(site_and_power)

            slug = _slug(name, str(hz), site)
            record: dict[str, object] = {
                "id": f"{SOURCE}:{slug}:{hz}",
                "source": SOURCE, "source_id": f"{name}:{hz}",
                "source_url": SOURCE_URL, "country": "RU", "service": "fm",
                "frequency_hz": hz,
            }
            if name:
                record["name"] = name[:20]
            if site:
                record["transmitter"] = site[:32]
                record["city"] = site[:10]
            if power_w is not None:
                record["power_w"] = power_w
            coords = site_coords.get(site)
            if coords is not None:
                record["latitude_e7"], record["longitude_e7"] = coords
            record["region"] = "Мос"
            record["status"] = "LISTED"
            records[str(record["id"])] = record
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
    with_power = sum(1 for r in records if "power_w" in r)
    print(json.dumps({
        "source": SOURCE, "records": len(records),
        "services": {"fm": len(records)},
        "with_coords": with_coords,
        "with_power": with_power,
    }, ensure_ascii=True, indent=2))


if __name__ == "__main__":
    main()
