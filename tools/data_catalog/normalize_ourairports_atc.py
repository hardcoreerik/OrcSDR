#!/usr/bin/env python3
"""Normalize OurAirports airport-frequencies.csv + airports.csv into an
airport-keyed ATC NDJSON suitable for OrcSDR's ADS-B dashboard Settings tab.

Source: https://davidmegginson.github.io/ourairports-data/ (Public Domain).

Each output record represents ONE AIRPORT with an inline list of ATC
frequencies plus location, so a nearby-airports query at runtime is a
single geographic filter without a join.

Record shape:
{
  "icao": "KJFK", "iata": "JFK", "ident": "KJFK",
  "name": "John F Kennedy International",
  "city": "New York", "region": "US-NY", "country": "US",
  "lat_e7": 406399, "lon_e7": -737788,
  "airport_type": "large_airport",
  "frequencies": [
    {"type": "TWR", "freq_hz": 119100000, "desc": "TOWER"},
    {"type": "GND", "freq_hz": 121900000, "desc": "GROUND"},
    ...
  ]
}

Closed airports and airports without frequencies are dropped.

ATC-relevant types are kept in a stable priority order for on-device UI:
ATIS > TWR > CTAF > GND > CLD > APP > DEP > CNTR > RDO > A/D > AFIS > INFO > MISC > UNIC > AWOS.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from collections import Counter, defaultdict
from decimal import Decimal, InvalidOperation, ROUND_HALF_UP
from pathlib import Path


SOURCE = "ourairports_atc"
SOURCE_URL = "https://davidmegginson.github.io/ourairports-data/"

# Aviation VHF airband: 118.000 - 136.975 MHz. Extended for stray sources
# but records outside are dropped.
AIRBAND_HZ = (108_000_000, 137_000_000)

# Priority order for on-device UI listing (higher = more useful for ADS-B monitor)
TYPE_PRIORITY = {
    "ATIS": 100, "TWR": 90, "CTAF": 85, "GND": 80, "CLD": 78,
    "APP": 75, "DEP": 74, "CNTR": 70, "RDO": 65, "A/D": 60,
    "AFIS": 55, "INFO": 50, "MISC": 40, "UNIC": 35, "AWOS": 30,
    "ASOS": 28, "FSS": 25, "ARCAL": 20, "AAS": 15,
}
# Sensible default for unknown types
DEFAULT_PRIORITY = 10

# Types we drop as not useful for an ATC audio scanner on ADS-B dashboard
DROP_TYPES = set()  # keep everything; users can filter on device


def _e7(deg: str) -> int | None:
    try:
        val = Decimal(deg)
    except (InvalidOperation, ValueError):
        return None
    return int((val * 10_000_000).to_integral_value(rounding=ROUND_HALF_UP))


def _freq_hz(mhz: str) -> int | None:
    try:
        val = Decimal(mhz)
    except (InvalidOperation, ValueError):
        return None
    hz = int((val * 1_000_000).to_integral_value(rounding=ROUND_HALF_UP))
    return hz if AIRBAND_HZ[0] <= hz <= AIRBAND_HZ[1] else None


def load(airports_csv: Path, freqs_csv: Path) -> list[dict[str, object]]:
    # Index airports by ident
    airports: dict[str, dict[str, str]] = {}
    with airports_csv.open(encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f):
            airports[row["ident"]] = row

    # Bucket frequencies by airport
    freqs_by_airport: dict[str, list[dict[str, object]]] = defaultdict(list)
    with freqs_csv.open(encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f):
            ident = row["airport_ident"]
            hz = _freq_hz(row["frequency_mhz"])
            if hz is None:
                continue
            ftype = (row["type"] or "").strip().upper()
            desc = (row["description"] or "").strip()
            freqs_by_airport[ident].append({
                "type": ftype, "freq_hz": hz, "desc": desc[:32],
            })

    records: list[dict[str, object]] = []
    for ident, ap in airports.items():
        if ap["type"] == "closed":
            continue
        if ident not in freqs_by_airport:
            continue
        lat_e7 = _e7(ap["latitude_deg"])
        lon_e7 = _e7(ap["longitude_deg"])
        if lat_e7 is None or lon_e7 is None:
            continue

        # Sort frequencies: highest priority first, then by freq
        freqs = sorted(
            freqs_by_airport[ident],
            key=lambda fr: (-TYPE_PRIORITY.get(str(fr["type"]), DEFAULT_PRIORITY),
                            int(fr["freq_hz"])),
        )
        # Dedupe (type, freq_hz)
        seen = set()
        deduped: list[dict[str, object]] = []
        for fr in freqs:
            key = (fr["type"], fr["freq_hz"])
            if key in seen:
                continue
            seen.add(key)
            deduped.append(fr)

        rec: dict[str, object] = {
            "ident": ident,
            "icao": ap["icao_code"] or ident,
            "iata": ap["iata_code"] or "",
            "name": ap["name"][:64],
            "city": (ap["municipality"] or "")[:32],
            "region": ap["iso_region"],
            "country": ap["iso_country"],
            "lat_e7": lat_e7,
            "lon_e7": lon_e7,
            "airport_type": ap["type"],
            "frequencies": deduped,
        }
        if ap["scheduled_service"] == "yes":
            rec["scheduled_service"] = True
        records.append(rec)
    # Sort by (country, ident) for reproducibility
    records.sort(key=lambda r: (str(r["country"]), str(r["ident"])))
    return records


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--airports", type=Path, required=True,
                        help="OurAirports airports.csv")
    parser.add_argument("--freqs", type=Path, required=True,
                        help="OurAirports airport-frequencies.csv")
    parser.add_argument("--out", type=Path, required=True,
                        help="output airport-keyed ATC NDJSON")
    args = parser.parse_args()
    records = load(args.airports, args.freqs)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        "".join(json.dumps(r, ensure_ascii=False, sort_keys=True) + "\n"
                for r in records),
        encoding="utf-8", newline="\n")

    total_freqs = sum(len(r["frequencies"]) for r in records)
    by_country = Counter(str(r["country"]) for r in records)
    by_ap_type = Counter(str(r["airport_type"]) for r in records)
    by_freq_type: Counter[str] = Counter()
    for r in records:
        for fr in r["frequencies"]:
            by_freq_type[str(fr["type"])] += 1
    print(json.dumps({
        "source": SOURCE,
        "airports": len(records),
        "frequencies": total_freqs,
        "top_countries": dict(by_country.most_common(20)),
        "airport_types": dict(by_ap_type.most_common()),
        "freq_types_top20": dict(by_freq_type.most_common(20)),
    }, ensure_ascii=True, indent=2))


if __name__ == "__main__":
    main()
