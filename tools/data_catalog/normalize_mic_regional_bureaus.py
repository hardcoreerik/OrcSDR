#!/usr/bin/env python3
"""Normalize MIC (総務省) regional-bureau published radio broadcaster lists.

Phase 1 scope: **Kanto (関東) and Kinki (近畿) bureaus only** — Tokyo + Osaka
metro areas and their prefectures. National coverage requires per-bureau URL
discovery for the other nine bureaus and is a follow-up.

Each bureau publishes its own HTML page(s) on ``www.soumu.go.jp`` (Shift-JIS
encoded). The pages carry structured HTML tables of licensed broadcasters
with columns 放送事業者名 / 事業者名, 周波数, and (on some pages) 電力 and
所在地 / 送信場所. Every page under ``soumu.go.jp`` is licensed under
Public Data License v1.0 (公共データ利用規約) per
``www.soumu.go.jp/menu_kyotsuu/policy/tyosaku.html`` — attribution and
modification disclosure required, redistribution permitted.

Coordinates are not published in these HTML pages; ``power_w`` is only
present on the Kinki pages. The ORCBRD1 record contract permits optional
fields to be absent when the source does not supply them.

Emitted service codes are ``am`` (中波 medium wave), ``fm`` (超短波 VHF
including Wide-FM 補完中継局 simulcasts and コミュニティ FM), and
``shortwave`` (Radio Nikkei only).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from collections import Counter
from pathlib import Path


SOURCE = "mic_regional_bureau_lists"
SOURCE_URL = "https://www.soumu.go.jp/soutsu/"
AM_HZ = (500_000, 1_800_000)
FM_HZ = (76_000_000, 108_500_000)   # Japan FM starts at 76 MHz
SW_HZ = (2_000_000, 30_000_000)

BUREAU_REGIONS = {
    "kanto": ("茨城", "栃木", "群馬", "埼玉", "千葉", "東京", "神奈川", "山梨"),
    "kinki": ("大阪", "京都", "兵庫", "奈良", "滋賀", "和歌山"),
}


def _cell(html: str) -> str:
    text = re.sub(r"<[^>]+>", " ", html)
    text = text.replace("&nbsp;", " ").replace("&amp;", "&")
    text = re.sub(r"\s+", " ", text).strip()
    return text


def _tables(html: str) -> list[list[list[str]]]:
    out = []
    for tbl in re.findall(r"<table[^>]*>(.*?)</table>", html, re.DOTALL):
        rows = []
        for row in re.findall(r"<tr[^>]*>(.*?)</tr>", tbl, re.DOTALL):
            cells = [_cell(c) for c in re.findall(r"<t[dh][^>]*>(.*?)</t[dh]>", row, re.DOTALL)]
            rows.append(cells)
        out.append(rows)
    return out


def _sections_before_tables(html: str) -> dict[int, str]:
    # Walk the HTML in order; record the last H2/H3 seen before each <table>.
    positions = {}
    last_heading = ""
    idx = 0
    for match in re.finditer(r"<(h[23]|table)[^>]*>(.*?)</\1", html, re.DOTALL|re.IGNORECASE):
        tag = match.group(1).lower()
        body = _cell(match.group(2))
        if tag == "table":
            positions[idx] = last_heading
            idx += 1
        else:
            last_heading = body
    return positions


def _read(path: Path) -> str:
    return path.read_bytes().decode("shift_jis", errors="replace")


def _slug(*parts: str) -> str:
    seed = "|".join(p or "" for p in parts).encode("utf-8")
    return hashlib.md5(seed).hexdigest()[:8]


def _snap_hz(value: float, service: str) -> int | None:
    if service == "am":
        hz = round(value) * 1_000
        return hz if AM_HZ[0] <= hz <= AM_HZ[1] else None
    if service == "fm":
        hz = round(value * 10) * 100_000
        return hz if FM_HZ[0] <= hz <= FM_HZ[1] else None
    if service == "shortwave":
        hz = round(value * 1_000) * 1_000
        return hz if SW_HZ[0] <= hz <= SW_HZ[1] else None
    return None


def _parse_freqs(text: str, service: str) -> list[tuple[int, str]]:
    """Return (frequency_hz, annotation) tuples parsed from a cell."""
    out: list[tuple[int, str]] = []
    stripped = text.replace("　", " ")
    # e.g. "594kHz(東京)", "82.5（東京・墨田）", "76.1 MHz", "1584kHz（富士吉田）"
    for m in re.finditer(
        r"(\d+(?:[\.．]\d+)?)\s*(?:kHz|ｋHz|ＫHz|MHz|ＭHz|Ｍ?Hz|㎑|㎒)?"
        r"[^\d]*?(?:[（(]([^)）]{0,60})[)）])?",
        stripped,
    ):
        raw_freq, annotation = m.group(1), (m.group(2) or "").strip()
        try:
            value = float(raw_freq.replace("．", "."))
        except ValueError:
            continue
        hz = _snap_hz(value, service)
        if hz is None:
            continue
        # only keep annotations that look like a place name, not "第1放送" etc.
        out.append((hz, annotation))
    return out


def _power_watts(text: str) -> int | None:
    m = re.match(r"\s*([\d.]+)\s*(kW|ｋW|W|ｗ)", text)
    if not m:
        return None
    try:
        value = float(m.group(1))
    except ValueError:
        return None
    if m.group(2) in ("kW", "ｋW"):
        return round(value * 1000)
    return round(value)


def _pick_prefecture(text: str, valid: tuple[str, ...]) -> str:
    for pref in valid:
        if pref in text:
            return pref
    return ""


def _record(*, bureau: str, service: str, name: str, city: str, region: str,
            freq_hz: int, power_w: int | None, status: str) -> dict[str, object]:
    slug = _slug(bureau, service, name, city, str(freq_hz))
    record: dict[str, object] = {
        "id": f"{SOURCE}:{bureau}:{service}:{freq_hz}:{slug}",
        "source": SOURCE, "source_id": f"{bureau}:{service}:{freq_hz}:{slug}",
        "source_url": SOURCE_URL, "country": "JP", "service": service,
        "frequency_hz": freq_hz,
    }
    if name:
        record["name"] = name[:20]  # C++ name[64] holds ~21 CJK chars
    if city:
        record["city"] = city[:10]  # C++ city[32] holds ~10 CJK chars
    if region:
        record["region"] = region[:5]  # C++ region[16] holds ~5 CJK chars
    if power_w is not None:
        record["power_w"] = power_w
    if status:
        record["status"] = status
    return record


def parse_kanto(html: str) -> list[dict[str, object]]:
    bureau = "kanto"
    valid = BUREAU_REGIONS[bureau]
    tables = _tables(html)
    headings = _sections_before_tables(html)
    records: list[dict[str, object]] = []
    for idx, rows in enumerate(tables):
        if not rows or len(rows) < 2:
            continue
        header = " ".join(rows[0]) if rows else ""
        heading = headings.get(idx, "")
        # Classify table by header columns
        if "親局" in header and "kHz" in "".join(row for row in [r for r in rows[1:] for r in r]):
            table_type = "kanto_am"
        elif "MHz" in header and "備考" in header:
            table_type = "kanto_sw"
        elif "親局" in header and ("MHz" in header or "MHz" in "".join(row for row in [r for r in rows[1:] for r in r])):
            table_type = "kanto_fm_full"
        elif "所在地" in header:
            table_type = "kanto_commfm"
        else:
            continue
        prefecture = _pick_prefecture(heading, valid)
        for row in rows[1:]:
            cells = [c for c in row if c]
            if len(cells) < 2:
                continue
            if table_type == "kanto_am":
                name = cells[1] if len(cells) > 1 else ""
                for group_idx in (2, 3):
                    if group_idx >= len(cells):
                        break
                    for hz, annot in _parse_freqs(cells[group_idx], "am"):
                        city = annot or prefecture
                        records.append(_record(bureau=bureau, service="am",
                                               name=name, city=city, region=prefecture,
                                               freq_hz=hz, power_w=None, status="LICENSED"))
            elif table_type == "kanto_sw":
                name = cells[1] if len(cells) > 1 else ""
                for hz, annot in _parse_freqs(cells[2] if len(cells) > 2 else "", "shortwave"):
                    records.append(_record(bureau=bureau, service="shortwave",
                                           name=name, city="", region="",
                                           freq_hz=hz, power_w=None, status="LICENSED"))
            elif table_type == "kanto_fm_full":
                name = cells[1] if len(cells) > 1 else ""
                area = cells[4] if len(cells) > 4 else ""
                for group_idx in (2, 3):
                    if group_idx >= len(cells):
                        break
                    for hz, annot in _parse_freqs(cells[group_idx], "fm"):
                        city = annot or area or prefecture
                        records.append(_record(bureau=bureau, service="fm",
                                               name=name, city=city, region=prefecture,
                                               freq_hz=hz, power_w=None, status="LICENSED"))
            elif table_type == "kanto_commfm":
                name = cells[1] if len(cells) > 1 else ""
                city = cells[2] if len(cells) > 2 else ""
                for hz, _ in _parse_freqs(cells[3] if len(cells) > 3 else "", "fm"):
                    records.append(_record(bureau=bureau, service="fm",
                                           name=name, city=city, region=prefecture,
                                           freq_hz=hz, power_w=None, status="LICENSED"))
    return records


def parse_kinki_tyuha(html: str) -> list[dict[str, object]]:
    bureau = "kinki"
    valid = BUREAU_REGIONS[bureau]
    records: list[dict[str, object]] = []
    for rows in _tables(html):
        if not rows or len(rows) < 2 or "放送事業者名" not in rows[0][0] if rows[0] else True:
            if not rows or not rows[0] or "放送事業者名" not in rows[0][0]:
                continue
        # Kinki tyuha uses rowspan so 放送事業者名 may be missing on continuation rows.
        current_broadcaster = ""
        for row in rows[1:]:
            cells = [c for c in row if c]
            if len(cells) >= 5:
                broadcaster, name, city, freq, power = cells[0], cells[1], cells[2], cells[3], cells[4]
                current_broadcaster = broadcaster
            elif len(cells) == 4:
                broadcaster = current_broadcaster
                name, city, freq, power = cells[0], cells[1], cells[2], cells[3]
            else:
                continue
            parsed = _parse_freqs(freq, "am")
            if not parsed:
                continue
            hz = parsed[0][0]
            power_w = _power_watts(power)
            region = _pick_prefecture(city, valid)
            records.append(_record(bureau=bureau, service="am",
                                   name=name or broadcaster, city=city, region=region,
                                   freq_hz=hz, power_w=power_w, status="LICENSED"))
    return records


def parse_kinki_keniki(html: str) -> list[dict[str, object]]:
    bureau = "kinki"
    valid = BUREAU_REGIONS[bureau]
    records: list[dict[str, object]] = []
    for rows in _tables(html):
        if not rows or len(rows) < 2:
            continue
        header = rows[0]
        # header patterns: ["幹局名","周波数","電力","送信場所","中継局数"]
        #                  or ["放送事業者名","周波数","電力","送信場所","中継局数"]
        if not any("周波数" in c for c in header) or not any("送信場所" in c for c in header):
            continue
        for row in rows[1:]:
            cells = [c for c in row if c]
            if len(cells) < 4:
                continue
            name, freq, power, city = cells[0], cells[1], cells[2], cells[3]
            parsed = _parse_freqs(freq, "fm")
            if not parsed:
                continue
            hz = parsed[0][0]
            power_w = _power_watts(power)
            region = _pick_prefecture(city + name, valid)
            records.append(_record(bureau=bureau, service="fm",
                                   name=name, city=city, region=region,
                                   freq_hz=hz, power_w=power_w, status="LICENSED"))
    return records


def parse_kinki_fmhokan(html: str) -> list[dict[str, object]]:
    bureau = "kinki"
    valid = BUREAU_REGIONS[bureau]
    records: list[dict[str, object]] = []
    for rows in _tables(html):
        if not rows or len(rows) < 2:
            continue
        header = rows[0]
        if not any("放送事業者名" in c for c in header) or not any("周波数" in c for c in header):
            continue
        for row in rows[1:]:
            cells = [c for c in row if c]
            if len(cells) < 4:
                continue
            name, freq, power, area = cells[0], cells[1], cells[2], cells[3]
            parsed = _parse_freqs(freq, "fm")
            if not parsed:
                continue
            hz = parsed[0][0]
            power_w = _power_watts(power)
            region = _pick_prefecture(area, valid)
            city = area.split("市")[0] + "市" if "市" in area else area
            records.append(_record(bureau=bureau, service="fm",
                                   name=name, city=city, region=region,
                                   freq_hz=hz, power_w=power_w, status="LICENSED"))
    return records


PARSERS = {
    "kanto_list":    parse_kanto,
    "kinki_tyuha":   parse_kinki_tyuha,
    "kinki_keniki":  parse_kinki_keniki,
    "kinki_fmhokan": parse_kinki_fmhokan,
}


def load(stage_dir: Path) -> list[dict[str, object]]:
    records: dict[str, dict[str, object]] = {}
    for label, parser in PARSERS.items():
        path = stage_dir / f"{label}.html"
        if not path.is_file():
            continue
        for row in parser(_read(path)):
            records[str(row["id"])] = row
    return sorted(records.values(),
                  key=lambda row: (int(row["frequency_hz"]), str(row["id"])))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage", type=Path, required=True,
                        help="directory containing kanto_list.html, kinki_*.html")
    parser.add_argument("--out", type=Path, required=True,
                        help="normalized NDJSON")
    args = parser.parse_args()
    records = load(args.stage)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        "".join(json.dumps(row, ensure_ascii=False, sort_keys=True) + "\n"
                for row in records),
        encoding="utf-8", newline="\n")
    by_service = Counter(str(row["service"]) for row in records)
    by_region = Counter(str(row.get("region", "")) for row in records)
    print(json.dumps({
        "source": SOURCE, "records": len(records),
        "services": dict(sorted(by_service.items())),
        "regions": dict(sorted(by_region.items(), key=lambda kv: -kv[1])),
    }, ensure_ascii=True, indent=2))


if __name__ == "__main__":
    main()
