#!/usr/bin/env python3
"""Build an ORCAIR2 aviation runtime index from OurAirports CSV exports.

The script is intentionally offline/reproducible: download airports.csv and
airport-frequencies.csv separately, archive the source snapshot used for a
release, then run this normalizer. OurAirports publishes its data as Public
Domain; OrcSDR still records the source class as COMMUNITY because the records
are community-maintained rather than an official AIP.
"""

from __future__ import annotations

import argparse
import csv
import math
import unicodedata
from decimal import Decimal, InvalidOperation, ROUND_HALF_UP
from pathlib import Path

MIN_HZ = 118_000_000
MAX_HZ = 136_975_000

SERVICE_MAP = {
    "TWR": "TOWER",
    "TOWER": "TOWER",
    "GND": "GROUND",
    "GROUND": "GROUND",
    "APP": "APPROACH",
    "APPROACH": "APPROACH",
    "DEP": "DEPARTURE",
    "DEPARTURE": "DEPARTURE",
    "ACC": "CENTER",
    "CTR": "CENTER",
    "CENTER": "CENTER",
    "ATIS": "ATIS",
    "AWOS": "AWOS",
    "ASOS": "AWOS",
    "CTAF": "CTAF",
    "UNICOM": "UNICOM",
    "CLD": "CLEARANCE",
    "CLNC": "CLEARANCE",
    "DEL": "CLEARANCE",
    "CLEARANCE": "CLEARANCE",
}


def ascii_text(value: str, limit: int) -> str:
    value = unicodedata.normalize("NFKD", value or "").encode("ascii", "ignore").decode("ascii")
    value = " ".join(value.replace("\t", " ").replace("\r", " ").replace("\n", " ").split())
    return value[:limit]


def e7(value: str, low: Decimal, high: Decimal) -> int:
    try:
        parsed = Decimal(value)
    except InvalidOperation as error:
        raise ValueError(f"invalid coordinate: {value}") from error
    if not low <= parsed <= high:
        raise ValueError(f"coordinate out of range: {value}")
    return int((parsed * 10_000_000).to_integral_value(rounding=ROUND_HALF_UP))


def hz(value: str) -> int:
    try:
        parsed = Decimal(value)
    except InvalidOperation as error:
        raise ValueError(f"invalid MHz frequency: {value}") from error
    result = int((parsed * 1_000_000).to_integral_value(rounding=ROUND_HALF_UP))
    if not MIN_HZ <= result <= MAX_HZ:
        raise ValueError(f"airband frequency out of range: {value}")
    return result


def service(value: str, description: str) -> str:
    token = ascii_text(value, 24).upper()
    if token in SERVICE_MAP:
        return SERVICE_MAP[token]
    combined = f"{token} {ascii_text(description, 40).upper()}"
    for key, mapped in SERVICE_MAP.items():
        if key in combined.split():
            return mapped
    return "UNKNOWN"


def distance_nm(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    r = 3440.065
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dp = math.radians(lat2 - lat1)
    dl = math.radians(lon2 - lon1)
    a = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * r * math.asin(math.sqrt(min(1.0, max(0.0, a))))


def load_airports(path: Path) -> dict[str, dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as stream:
        rows = list(csv.DictReader(stream))
    required = {"ident", "name", "latitude_deg", "longitude_deg", "iso_country", "iso_region"}
    if not rows or not required <= set(rows[0]):
        raise ValueError(f"airports.csv missing columns: {sorted(required)}")
    return {row["ident"]: row for row in rows if row.get("ident")}


def build(args: argparse.Namespace) -> int:
    airports = load_airports(args.airports)
    countries = {item.upper() for item in args.country}
    regions = {item.upper() for item in args.region}
    center = None
    if (args.center_lat is None) != (args.center_lon is None):
        raise ValueError("--center-lat and --center-lon must be provided together")
    if args.center_lat is not None:
        if not -90 <= args.center_lat <= 90 or not -180 <= args.center_lon <= 180:
            raise ValueError("center coordinate out of range")
        center = (args.center_lat, args.center_lon)

    with args.frequencies.open(encoding="utf-8", newline="") as stream:
        rows = list(csv.DictReader(stream))
    required = {"airport_ident", "type", "description", "frequency_mhz"}
    if not rows or not required <= set(rows[0]):
        raise ValueError(f"airport-frequencies.csv missing columns: {sorted(required)}")

    records: list[tuple[float, tuple[str, ...]]] = []
    for row in rows:
        airport = airports.get(row.get("airport_ident", ""))
        if not airport:
            continue
        country = airport.get("iso_country", "").upper()
        region = airport.get("iso_region", "").upper()
        if countries and country not in countries:
            continue
        if regions and region not in regions:
            continue
        try:
            frequency_hz = hz(row["frequency_mhz"])
            lat = float(airport["latitude_deg"])
            lon = float(airport["longitude_deg"])
            latitude_e7 = e7(airport["latitude_deg"], Decimal("-90"), Decimal("90"))
            longitude_e7 = e7(airport["longitude_deg"], Decimal("-180"), Decimal("180"))
        except (ValueError, KeyError):
            continue
        distance = distance_nm(center[0], center[1], lat, lon) if center else 0.0
        if center and args.radius_nm is not None and distance > args.radius_nm:
            continue
        ident = ascii_text(airport.get("ident", ""), 8)
        airport_name = ascii_text(airport.get("name", ""), 47)
        description = ascii_text(row.get("description", ""), 38)
        service_name = service(row.get("type", ""), description)
        label_detail = description or service_name
        label = ascii_text(f"{ident} {label_detail}".strip(), 39)
        fields = (
            "COM", str(latitude_e7), str(longitude_e7), str(frequency_hz), service_name,
            ascii_text(country, 2), ident, airport_name, "", "COMMUNITY", "OURAIRPORTS", label,
        )
        records.append((distance, fields))

    if center:
        records.sort(key=lambda item: (item[0], item[1][6], int(item[1][3]), item[1][11]))
    else:
        records.sort(key=lambda item: (item[1][5], item[1][6], int(item[1][3]), item[1][11]))
    if args.max_records is not None:
        records = records[: args.max_records]
    if not records:
        raise ValueError("no civil VHF communication records matched the requested filters")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="ascii", newline="\n") as stream:
        stream.write("ORCAIR2\n")
        for _, fields in records:
            stream.write("\t".join(fields) + "\n")
    return len(records)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--airports", type=Path, required=True)
    parser.add_argument("--frequencies", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--country", action="append", default=[], help="ISO country code; repeatable")
    parser.add_argument("--region", action="append", default=[], help="OurAirports ISO region; repeatable")
    parser.add_argument("--center-lat", type=float)
    parser.add_argument("--center-lon", type=float)
    parser.add_argument("--radius-nm", type=float)
    parser.add_argument("--max-records", type=int)
    args = parser.parse_args()
    count = build(args)
    print(f"wrote {count} ORCAIR2 records to {args.output}")


if __name__ == "__main__":
    main()
