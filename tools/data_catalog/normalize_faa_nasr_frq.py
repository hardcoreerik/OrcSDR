#!/usr/bin/env python3
"""Normalize FAA NASR FRQ.csv into airband ATC staging rows for OrcSDR.

Reads the NASR Frequency (FRQ) CSV export, keeps civil VHF airband voice
frequencies in 118.000-136.975 MHz, builds short ASCII labels compatible with
ORCCAT1 (31-char cap), and emits:

* NDJSON master records (full provenance)
* reviewed CSV for :mod:`build_faa_aviation_index`
* a SHA-256 receipt with cycle date and FREQ_USE counts

Optional APT_BASE join supplies ICAO_ID and fallback airport coordinates when
FRQ lat/lon are blank. This is a staging ledger only — not a signed catalog.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import re
from collections import Counter
from decimal import Decimal, InvalidOperation, ROUND_HALF_UP
from pathlib import Path

SOURCE = "faa_nasr_frq"
AIRBAND_MHZ_LOW = Decimal("118.000")
AIRBAND_MHZ_HIGH = Decimal("136.975")
LABEL_CAPACITY = 31

# Map common NASR FREQ_USE tokens to short voice-service tags used in labels/ids.
FREQ_USE_TAGS: list[tuple[re.Pattern[str], str]] = [
    (re.compile(r"^CTAF$", re.I), "CTAF"),
    (re.compile(r"^UNICOM$|UNICOM FREQ", re.I), "UNICOM"),
    (re.compile(r"^LCL(/[PS])?$", re.I), "TWR"),
    (re.compile(r"^GND(/[PS])?$", re.I), "GND"),
    (re.compile(r"^CD(/[PS])?$|^CD PRE TAXI", re.I), "CD"),
    (re.compile(r"^D-?ATIS$|^ATIS$", re.I), "ATIS"),
    (re.compile(r"\bA[SW]OS\b", re.I), "AWOS"),
    (re.compile(r"^APCH", re.I), "APP"),
    (re.compile(r"^DEP", re.I), "DEP"),
    (re.compile(r"^EMERG", re.I), "EMERG"),
    (re.compile(r"^CLASS B$", re.I), "CL B"),
    (re.compile(r"^CLASS C$", re.I), "CL C"),
    (re.compile(r"^TRSA$", re.I), "TRSA"),
    (re.compile(r"^PMSV", re.I), "PMSV"),
    (re.compile(r"^PTD$", re.I), "PTD"),
    (re.compile(r"^SFA$", re.I), "SFA"),
    (re.compile(r"^RAMP CTL$", re.I), "RAMP"),
    (re.compile(r"^RANGE CTL$", re.I), "RANGE"),
    (re.compile(r"\bOPS\b", re.I), "OPS"),
    (re.compile(r"\bRCAG\b", re.I), "RCAG"),
    (re.compile(r"\bRCO\b", re.I), "RCO"),
]

# Extract MHz tokens; ignore navaid channel suffixes like 112.9/76X as a whole
# by requiring a free-standing decimal or integer MHz value.
FREQ_TOKEN_RE = re.compile(
    r"(?<![A-Za-z0-9.])(\d{2,3}(?:\.\d{1,3})?)(?!(?:\.\d)|[A-Za-z/])"
)
SPLIT_RE = re.compile(r"[&;,/]|\s+")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def ascii_only(text: str) -> str:
    return "".join(ch if 32 <= ord(ch) <= 126 else "?" for ch in text)


def frequency_tag(mhz: Decimal) -> str:
    quantized = mhz.quantize(Decimal("0.001"), rounding=ROUND_HALF_UP)
    return f"{quantized:.3f}"


def parse_frequencies(freq_field: str) -> list[Decimal]:
    """Return in-band MHz values from a NASR FREQ cell (may hold several)."""
    text = (freq_field or "").strip()
    if not text:
        return []

    # Navaid channel forms (e.g. 112.9/76X) are not voice airband rows.
    if re.fullmatch(r"\d{2,3}(?:\.\d{1,3})?/\d{1,3}[XY]", text, re.I):
        return []

    values: list[Decimal] = []
    seen: set[str] = set()

    # Prefer explicit multi-value splits first, then token scan.
    parts = [part.strip() for part in SPLIT_RE.split(text) if part.strip()]
    candidates = parts if len(parts) > 1 else [text]
    tokens: list[str] = []
    for part in candidates:
        if re.fullmatch(r"\d{2,3}(?:\.\d{1,3})?", part):
            tokens.append(part)
        else:
            tokens.extend(match.group(1) for match in FREQ_TOKEN_RE.finditer(part))

    if not tokens:
        tokens = [match.group(1) for match in FREQ_TOKEN_RE.finditer(text)]

    for token in tokens:
        try:
            mhz = Decimal(token)
        except InvalidOperation:
            continue
        if not AIRBAND_MHZ_LOW <= mhz <= AIRBAND_MHZ_HIGH:
            continue
        key = frequency_tag(mhz)
        if key in seen:
            continue
        seen.add(key)
        values.append(mhz)
    return values


def freq_use_tag(freq_use: str) -> str:
    text = (freq_use or "").strip()
    if not text:
        return "UNK"
    for pattern, tag in FREQ_USE_TAGS:
        if pattern.search(text):
            return tag
    # Fallback: first ASCII word-ish chunk, truncated.
    cleaned = ascii_only(re.sub(r"\s+", " ", text)).strip()
    token = cleaned.split(" ")[0] if cleaned else "UNK"
    token = re.sub(r"[^A-Za-z0-9/+-]", "", token) or "UNK"
    return token[:12]


def build_label(airport_id: str, use_tag: str, freq_use: str) -> str:
    airport = ascii_only((airport_id or "UNK").strip()) or "UNK"
    tag = ascii_only(use_tag.strip()) or "UNK"
    label = f"{airport} {tag}".strip()
    if len(label) <= LABEL_CAPACITY:
        return label
    # Prefer keeping airport id; trim use tag.
    room = LABEL_CAPACITY - len(airport) - 1
    if room >= 3:
        return f"{airport} {tag[:room]}"
    return label[:LABEL_CAPACITY]


def e7(value: Decimal) -> int:
    return int((value * Decimal(10_000_000)).to_integral_value(rounding=ROUND_HALF_UP))


def load_airports(apt_base: Path | None) -> dict[str, dict[str, str]]:
    if apt_base is None or not apt_base.is_file():
        return {}
    airports: dict[str, dict[str, str]] = {}
    with apt_base.open(encoding="utf-8-sig", newline="") as stream:
        for row in csv.DictReader(stream):
            arpt_id = (row.get("ARPT_ID") or "").strip()
            if arpt_id:
                airports[arpt_id] = row
    return airports


def parse_coord(text: str) -> Decimal | None:
    stripped = (text or "").strip()
    if not stripped:
        return None
    try:
        return Decimal(stripped)
    except InvalidOperation:
        return None


def normalize_row(
    row: dict[str, str],
    airports: dict[str, dict[str, str]],
    id_counts: Counter[str],
) -> tuple[list[dict[str, object]], dict[str, int]]:
    stats = Counter()
    freq_field = (row.get("FREQ") or "").strip()
    if not freq_field:
        stats["drop_empty_freq"] += 1
        return [], stats

    mhz_values = parse_frequencies(freq_field)
    if not mhz_values:
        stats["drop_out_of_band_or_nonvoice"] += 1
        return [], stats

    airport_id = (row.get("SERVICED_FACILITY") or row.get("FACILITY") or "").strip() or "UNK"
    apt = airports.get(airport_id)
    icao_id = (apt.get("ICAO_ID") or "").strip() if apt else ""

    lat = parse_coord(row.get("LAT_DECIMAL") or "")
    lon = parse_coord(row.get("LONG_DECIMAL") or "")
    used_apt_coords = False
    if (lat is None or lon is None) and apt:
        lat = parse_coord(apt.get("LAT_DECIMAL") or "")
        lon = parse_coord(apt.get("LONG_DECIMAL") or "")
        used_apt_coords = lat is not None and lon is not None
        if used_apt_coords:
            stats["coord_fallback_apt"] += 1

    if lat is None or lon is None:
        stats["drop_missing_coords"] += 1
        return [], stats
    if not (Decimal("-90") <= lat <= Decimal("90") and Decimal("-180") <= lon <= Decimal("180")):
        stats["drop_bad_coords"] += 1
        return [], stats

    freq_use = (row.get("FREQ_USE") or "").strip()
    use_tag = freq_use_tag(freq_use)
    label = build_label(airport_id, use_tag, freq_use)
    source_date = (row.get("EFF_DATE") or "").strip().replace("/", "-")
    city = (row.get("SERVICED_CITY") or (apt.get("CITY") if apt else "") or "").strip()
    state = (row.get("SERVICED_STATE") or (apt.get("STATE_CODE") if apt else "") or "").strip()
    country = (row.get("SERVICED_COUNTRY") or (apt.get("COUNTRY_CODE") if apt else "") or "US").strip() or "US"
    facility_type = (row.get("FACILITY_TYPE") or "").strip()
    source_id = "|".join(
        [
            (row.get("FACILITY") or "").strip(),
            airport_id,
            freq_field,
            freq_use,
            (row.get("SECTORIZATION") or "").strip(),
        ]
    )

    records: list[dict[str, object]] = []
    for mhz in mhz_values:
        tag = frequency_tag(mhz)
        base_id = f"{SOURCE}:{airport_id}:{tag}:{use_tag.replace(' ', '_')}"
        id_counts[base_id] += 1
        record_id = base_id if id_counts[base_id] == 1 else f"{base_id}:{id_counts[base_id]}"
        hz = int((mhz * Decimal(1_000_000)).to_integral_value(rounding=ROUND_HALF_UP))
        records.append(
            {
                "id": record_id,
                "source": SOURCE,
                "source_id": source_id,
                "airport_id": airport_id,
                "icao_id": icao_id or None,
                "facility_type": facility_type,
                "freq_use": freq_use,
                "frequency_hz": hz,
                "frequency_mhz": float(mhz),
                "latitude": float(lat),
                "longitude": float(lon),
                "latitude_e7": e7(lat),
                "longitude_e7": e7(lon),
                "label": label,
                "city": city,
                "state": state,
                "country": country,
                "source_date": source_date,
            }
        )
        stats["emitted"] += 1
    return records, stats


def write_outputs(
    records: list[dict[str, object]],
    ndjson_path: Path,
    csv_path: Path,
    receipt_path: Path,
    frq_path: Path,
    apt_path: Path | None,
    cycle_date: str,
    freq_use_counts: Counter[str],
    drop_stats: Counter[str],
    input_rows: int,
) -> dict[str, object]:
    ndjson_path.parent.mkdir(parents=True, exist_ok=True)
    with ndjson_path.open("w", encoding="utf-8", newline="\n") as stream:
        for record in records:
            stream.write(json.dumps(record, ensure_ascii=True, separators=(",", ":")) + "\n")

    with csv_path.open("w", encoding="utf-8", newline="\n") as stream:
        writer = csv.DictWriter(
            stream,
            fieldnames=["latitude", "longitude", "frequency_mhz", "label"],
        )
        writer.writeheader()
        for record in records:
            writer.writerow(
                {
                    "latitude": f"{record['latitude']:.8f}".rstrip("0").rstrip(".")
                    if isinstance(record["latitude"], float)
                    else record["latitude"],
                    "longitude": f"{record['longitude']:.8f}".rstrip("0").rstrip(".")
                    if isinstance(record["longitude"], float)
                    else record["longitude"],
                    "frequency_mhz": f"{Decimal(str(record['frequency_mhz'])).quantize(Decimal('0.001'))}",
                    "label": record["label"],
                }
            )

    inputs = {
        "frq_csv": {
            "path": str(frq_path).replace("\\", "/"),
            "sha256": sha256_file(frq_path),
            "bytes": frq_path.stat().st_size,
        }
    }
    if apt_path is not None and apt_path.is_file():
        inputs["apt_base_csv"] = {
            "path": str(apt_path).replace("\\", "/"),
            "sha256": sha256_file(apt_path),
            "bytes": apt_path.stat().st_size,
        }

    receipt = {
        "source": SOURCE,
        "cycle_date": cycle_date,
        "airband_mhz": [str(AIRBAND_MHZ_LOW), str(AIRBAND_MHZ_HIGH)],
        "input_rows": input_rows,
        "output_rows": len(records),
        "unique_airports": len({r["airport_id"] for r in records}),
        "unique_icao": len({r["icao_id"] for r in records if r.get("icao_id")}),
        "freq_use_counts": dict(freq_use_counts.most_common()),
        "drop_stats": dict(drop_stats),
        "outputs": {
            "ndjson": {
                "path": str(ndjson_path).replace("\\", "/"),
                "sha256": sha256_file(ndjson_path),
                "bytes": ndjson_path.stat().st_size,
                "rows": len(records),
            },
            "reviewed_csv": {
                "path": str(csv_path).replace("\\", "/"),
                "sha256": sha256_file(csv_path),
                "bytes": csv_path.stat().st_size,
                "rows": len(records),
            },
        },
        "inputs": inputs,
    }
    receipt_path.write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return receipt


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--frq",
        type=Path,
        default=Path("artifacts/atc-staging/frq/FRQ.csv"),
        help="NASR FRQ.csv path",
    )
    parser.add_argument(
        "--apt-base",
        type=Path,
        default=Path("artifacts/atc-staging/apt/APT_BASE.csv"),
        help="Optional APT_BASE.csv for ICAO/coord fallback",
    )
    parser.add_argument(
        "--ndjson",
        type=Path,
        default=Path("artifacts/atc-staging/faa-nasr-frq-airband.ndjson"),
    )
    parser.add_argument(
        "--csv",
        type=Path,
        default=Path("artifacts/atc-staging/faa-nasr-frq-reviewed.csv"),
    )
    parser.add_argument(
        "--receipt",
        type=Path,
        default=Path("artifacts/atc-staging/faa-nasr-frq-receipt.json"),
    )
    args = parser.parse_args()

    airports = load_airports(args.apt_base if args.apt_base.is_file() else None)
    records: list[dict[str, object]] = []
    drop_stats: Counter[str] = Counter()
    freq_use_counts: Counter[str] = Counter()
    id_counts: Counter[str] = Counter()
    cycle_date = ""
    input_rows = 0

    with args.frq.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        for row in reader:
            input_rows += 1
            if not cycle_date:
                cycle_date = (row.get("EFF_DATE") or "").strip().replace("/", "-")
            emitted, stats = normalize_row(row, airports, id_counts)
            drop_stats.update(stats)
            for record in emitted:
                records.append(record)
                freq_use_counts[(record.get("freq_use") or "").strip() or "(blank)"] += 1

    # Stable order: airport, frequency, label
    records.sort(
        key=lambda r: (
            str(r.get("airport_id") or ""),
            float(r["frequency_mhz"]),
            str(r.get("label") or ""),
            str(r.get("id") or ""),
        )
    )

    receipt = write_outputs(
        records,
        args.ndjson,
        args.csv,
        args.receipt,
        args.frq,
        args.apt_base if args.apt_base.is_file() else None,
        cycle_date,
        freq_use_counts,
        drop_stats,
        input_rows,
    )
    print(json.dumps({
        "cycle_date": receipt["cycle_date"],
        "input_rows": input_rows,
        "output_rows": len(records),
        "unique_airports": receipt["unique_airports"],
        "drop_stats": dict(drop_stats),
        "ndjson_bytes": receipt["outputs"]["ndjson"]["bytes"],
        "csv_bytes": receipt["outputs"]["reviewed_csv"]["bytes"],
        "sample_labels": [r["label"] for r in records[:8]],
    }, indent=2))


if __name__ == "__main__":
    main()
