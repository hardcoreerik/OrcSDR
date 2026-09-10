#!/usr/bin/env python3
"""Normalize the Ukraine.com culture/music/radio page into ORCBRD1 NDJSON.

Source: https://ukraine.com/culture/music/radio/ — an editorial travel/
culture guide's list of Ukrainian FM radio stations. Entries in the page
follow the pattern ``<freq> <station name> (<city>)`` where <freq> is in
MHz. The list includes both the modern 76–108 MHz FM band and the Soviet
OIRT band (66–74 MHz), which is still in use for some legacy Ukrainian
FM stations.

Source tier: general_web. Rights context: ukraine.com is an editorial
site with no explicit open licence; treat records as staged for
tooling/review, ledger controls release.
"""

from __future__ import annotations

import argparse
import hashlib
import html as html_lib
import json
import re
from collections import Counter
from pathlib import Path


SOURCE = "ukraine_com_radio"
SOURCE_URL = "https://ukraine.com/culture/music/radio/"
FM_HZ = (65_000_000, 108_500_000)  # extended to cover OIRT band (66–74 MHz)


def _snap_fm_hz(mhz: float) -> int | None:
    hz = round(mhz * 10) * 100_000
    return hz if FM_HZ[0] <= hz <= FM_HZ[1] else None


def _slug(*parts: str) -> str:
    seed = "|".join(p or "" for p in parts).encode("utf-8")
    return hashlib.md5(seed).hexdigest()[:8]


CITY_TO_REGION = {
    "Kiev": "Kyiv", "Kyiv": "Kyiv",
    "Kharkiv": "Kharkiv", "Kharkov": "Kharkiv",
    "Odessa": "Odesa", "Odesa": "Odesa",
    "Dnepropetrovsk": "Dnipro", "Dnipro": "Dnipro",
    "Lviv": "Lviv",
    "Zaporizhzhya": "Zapor", "Zaporzhzhya": "Zapor", "Zaporizhia": "Zapor",
    "Kramatorsk": "Donet", "Dontetsk": "Donet", "Donetsk": "Donet",
    "Simferopol": "Crime", "Vinnytsya": "Vinny", "Kherson": "Khers",
    "Priluki": "Cherh",
}


def load(html_path: Path) -> list[dict[str, object]]:
    raw = html_path.read_text(encoding="utf-8", errors="replace")
    text = re.sub(r"<br[^>]*>", "\n", raw)
    text = re.sub(r"</?p[^>]*>", "\n", text, flags=re.IGNORECASE)
    text = re.sub(r"<[^>]+>", " ", text)
    text = html_lib.unescape(text)
    text = re.sub(r"\s*\n\s*", "\n", text)

    pattern = re.compile(
        r"^\s*(\d{2,3}(?:[.,]\d{1,3}))\s+(.+?)\s*\(([^)]+)\)\s*$",
        re.MULTILINE,
    )

    records: dict[str, dict[str, object]] = {}
    for m in pattern.finditer(text):
        freq_str = m.group(1).replace(",", ".")
        name = m.group(2).strip()
        city = m.group(3).strip()
        try:
            freq_mhz = float(freq_str)
        except ValueError:
            continue
        hz = _snap_fm_hz(freq_mhz)
        if hz is None:
            continue
        slug = _slug(name, city, str(hz))
        record: dict[str, object] = {
            "id": f"{SOURCE}:{slug}:{hz}",
            "source": SOURCE, "source_id": f"{name}:{city}:{hz}",
            "source_url": SOURCE_URL, "country": "UA", "service": "fm",
            "frequency_hz": hz, "name": name[:20], "city": city[:10],
        }
        region = CITY_TO_REGION.get(city, "")
        if region:
            record["region"] = region[:5]
        record["status"] = "LISTED"
        records[str(record["id"])] = record

    return sorted(records.values(),
                  key=lambda r: (int(r["frequency_hz"]), str(r["id"])))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True,
                        help="ukraine.com/culture/music/radio HTML snapshot")
    parser.add_argument("--out", type=Path, required=True,
                        help="normalized NDJSON")
    args = parser.parse_args()
    records = load(args.input)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        "".join(json.dumps(r, ensure_ascii=False, sort_keys=True) + "\n"
                for r in records),
        encoding="utf-8", newline="\n")
    by_city = Counter(str(r["city"]) for r in records)
    print(json.dumps({
        "source": SOURCE, "records": len(records),
        "services": {"fm": len(records)},
        "cities": dict(sorted(by_city.items(), key=lambda kv: -kv[1])[:15]),
    }, ensure_ascii=True, indent=2))


if __name__ == "__main__":
    main()
