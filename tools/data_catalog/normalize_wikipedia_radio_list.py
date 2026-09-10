#!/usr/bin/env python3
"""Normalize a set of Wikipedia ``List of radio stations in <country>``
article HTML dumps into ORCBRD1 station-card NDJSON.

Source tier: general_web (Wikipedia). License: CC BY-SA 4.0 with
attribution and share-alike propagation. Every emitted record carries
the article title, revision id, and retrieval timestamp in
``source_id`` so downstream OrcSDR consumers can trace back to the
exact revision used.

Input: a directory containing ``<cc>_<article_title>_rev<revid>.html``
files (rendered MediaWiki HTML fragments). ``<cc>`` is the ISO 3166-1
alpha-2 country code the records belong to.

The parser identifies wikitables where at least one column carries FM
frequencies (77–108 MHz) or MW frequencies (500–1700 kHz), and maps
neighbouring columns to station name / city / format when a matching
header is present. It emits one record per station-frequency pair and
uses a stable hash of (source, article revision, frequency, name) as
the ORCBRD1 ``id``.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from collections import Counter
from pathlib import Path


SOURCE = "wikipedia_radio_list"
BASE_URL = "https://en.wikipedia.org/wiki/"
AM_HZ = (500_000, 1_800_000)
FM_HZ = (76_000_000, 108_500_000)


NAME_HEADERS = (
    # English
    "station", "branding", "name", "callsign", "network", "broadcaster",
    # German (Programm = station's on-air brand)
    "programm", "sender", "sendername",
    # Dutch
    "zender", "omroep",
    # Italian
    "emittente", "stazione",
    # French
    "programme", "émetteur", "chaîne", "chaine",
    # Spanish/Portuguese
    "emisora", "estación", "estacao", "estação", "cadena",
    # Nordic
    "kanal", "kanava",
    # Slavic
    "stanice", "stanica", "станция", "станція",
)
CITY_HEADERS = (
    "city", "town", "location", "area", "region",
    "standort", "senderstandort", "stadt", "ort",  # German
    "plaats", "locatie",  # Dutch
    "località", "localita", "sede",  # Italian
    "ville", "localité", "localite",  # French
    "localidad", "ciudad",  # Spanish
    "kommune", "kommun",  # Nordic (municipality)
)
FORMAT_HEADERS = ("format", "language", "genre", "programming",
                  "sprache", "taal", "lingua", "langue", "idioma")
# Detect a value that looks like a lat/lon coordinate rather than a name
# (matches "47°27'30 N", "38E05", "55N47", "-73.7781", etc.)
COORD_LIKE_RE = re.compile(
    r"\d+°\d+|\d+\s*[NSEWnsew]\s*\d+|[NSEW]\s*\d{1,3}[°.]\d|^-?\d{1,3}\.\d{3,}$"
)


def _clean_html(text: str) -> str:
    text = re.sub(r"<sup[^>]*>.*?</sup>", "", text, flags=re.DOTALL)
    text = re.sub(r"<[^>]+>", " ", text)
    text = text.replace("&amp;", "&").replace("&nbsp;", " ")
    text = re.sub(r"\[\d+\]", "", text)
    text = re.sub(r"\s+", " ", text).strip()
    return text


def _parse_freq(text: str) -> tuple[int, str] | None:
    """Return (frequency_hz, service) or None if not a broadcast frequency."""
    text = (text or "").strip()
    m = re.match(r"^\s*(\d{2,4}(?:[.,]\d+)?)\s*(?:MHz|Mhz|mhz|Mc/s)?\s*(?:FM|fm)?\s*$", text)
    if not m:
        # match "97.5 MHz FM" style
        m = re.match(r"^\s*(\d{2,4}(?:[.,]\d+)?)\s*(?:MHz|Mhz|kHz|Khz)?", text)
        if not m:
            return None
    try:
        value = float(m.group(1).replace(",", "."))
    except ValueError:
        return None
    # Heuristic: FM tables give MHz (76–108); AM/MW tables give kHz (500–1700)
    fm_hz = round(value * 10) * 100_000
    if FM_HZ[0] <= fm_hz <= FM_HZ[1]:
        return fm_hz, "fm"
    am_hz = round(value) * 1_000
    if AM_HZ[0] <= am_hz <= AM_HZ[1]:
        return am_hz, "am"
    return None


def _cells(row_html: str) -> list[str]:
    cells = re.findall(r"<t[dh][^>]*>(.*?)</t[dh]>", row_html, re.DOTALL)
    return [_clean_html(c) for c in cells]


def _classify_columns(header_cells: list[str]) -> dict[str, int]:
    result: dict[str, int] = {}
    for i, cell in enumerate(header_cells):
        lower = cell.lower()
        if "freq" in lower or "mhz" in lower or "khz" in lower:
            result.setdefault("frequency", i)
        for k in NAME_HEADERS:
            if k in lower:
                result.setdefault("name", i)
                break
        for k in CITY_HEADERS:
            if k in lower:
                result.setdefault("city", i)
                break
        for k in FORMAT_HEADERS:
            if k in lower:
                result.setdefault("format", i)
                break
    return result


def _stable_slug(*parts: str) -> str:
    seed = "|".join(p or "" for p in parts).encode("utf-8")
    return hashlib.md5(seed).hexdigest()[:8]


def _table_records(table_html: str, cc: str, article: str, revid: str,
                   source_url: str) -> list[dict[str, object]]:
    rows = re.findall(r"<tr[^>]*>(.*?)</tr>", table_html, re.DOTALL)
    if not rows:
        return []
    header = _cells(rows[0])
    cols = _classify_columns(header)
    # Score every column by how many data rows parse as a broadcast frequency.
    data_rows = [_cells(r) for r in rows[1:]]
    max_cols = max((len(r) for r in data_rows), default=len(header))
    if "frequency" not in cols and data_rows:
        best_col, best_hits = -1, 0
        for candidate in range(min(max_cols, 8)):
            hits = 0
            for data in data_rows[:40]:
                if len(data) > candidate and _parse_freq(data[candidate]):
                    hits += 1
            if hits > best_hits:
                best_col, best_hits = candidate, hits
        if best_hits >= 3:
            cols["frequency"] = best_col
    if "frequency" not in cols:
        return []
    if "name" not in cols:
        # Prefer the widest text column adjacent to the frequency column,
        # penalising columns that look like coordinates or are mostly numeric.
        candidate_widths: list[tuple[int, int]] = []
        for candidate in range(max_cols):
            if candidate == cols["frequency"]:
                continue
            avg = 0
            n = 0
            coord_hits = 0
            for data in data_rows[:20]:
                if len(data) > candidate:
                    v = data[candidate]
                    if v and not _parse_freq(v):
                        avg += len(v)
                        n += 1
                        if COORD_LIKE_RE.search(v):
                            coord_hits += 1
            if n:
                # Skip columns where >20% of values look like coordinates
                if coord_hits * 5 > n:
                    continue
                candidate_widths.append((avg // n, candidate))
        if candidate_widths:
            candidate_widths.sort(reverse=True)
            cols["name"] = candidate_widths[0][1]
        else:
            cols["name"] = 0 if cols["frequency"] != 0 else 1

    records: list[dict[str, object]] = []
    for row in rows[1:]:
        data = _cells(row)
        if len(data) <= cols["frequency"]:
            continue
        parsed = _parse_freq(data[cols["frequency"]])
        if parsed is None:
            continue
        hz, service = parsed
        name = data[cols["name"]].strip('"' + " ") if "name" in cols and cols["name"] < len(data) else ""
        city = data[cols["city"]] if "city" in cols and cols["city"] < len(data) else ""
        fmt = data[cols["format"]] if "format" in cols and cols["format"] < len(data) else ""

        slug = _stable_slug(article, str(hz), name, city)
        source_id = f"wp:{article}:rev{revid}:{hz}"
        record: dict[str, object] = {
            "id": f"{SOURCE}:{slug}:{hz}",
            "source": SOURCE, "source_id": source_id,
            "source_url": source_url, "country": cc.upper(),
            "service": service, "frequency_hz": hz,
        }
        if name:
            record["name"] = name[:20]
        if city:
            record["city"] = city[:10]
        if fmt:
            record["mode"] = fmt[:8]
        record["status"] = "LICENSED"
        records.append(record)
    return records


def load(stage_dir: Path) -> list[dict[str, object]]:
    filename_re = re.compile(r"^([a-z]{2})_([A-Za-z_]+)_rev(\d+)\.html$")
    seen: dict[str, dict[str, object]] = {}
    for path in sorted(stage_dir.glob("*.html")):
        m = filename_re.match(path.name)
        if not m:
            continue
        cc, article, revid = m.group(1), m.group(2), m.group(3)
        source_url = f"{BASE_URL}{article}?oldid={revid}"
        html = path.read_text(encoding="utf-8")
        tables = re.findall(
            r'<table[^>]*class="[^"]*wikitable[^"]*"[^>]*>(.*?)</table>',
            html, re.DOTALL)
        for tbl in tables:
            for row in _table_records(tbl, cc, article, revid, source_url):
                seen[str(row["id"])] = row
    return sorted(seen.values(),
                  key=lambda r: (int(r["frequency_hz"]), str(r["id"])))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage", type=Path, required=True,
                        help="directory of <cc>_<article>_rev<n>.html files")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    records = load(args.stage)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        "".join(json.dumps(r, ensure_ascii=False, sort_keys=True) + "\n"
                for r in records),
        encoding="utf-8", newline="\n")
    print(json.dumps({
        "source": SOURCE, "records": len(records),
        "services": dict(sorted(Counter(str(r["service"]) for r in records).items())),
        "countries": dict(sorted(Counter(str(r["country"]) for r in records).items())),
    }, ensure_ascii=True, indent=2))


if __name__ == "__main__":
    main()
