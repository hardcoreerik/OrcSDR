#!/usr/bin/env python3
"""Given a lat/lon (and optional radius in nautical miles), print airports
with ATC frequencies within that radius from the OurAirports ATC NDJSON.

This is the reference implementation of what the OrcSDR ADS-B dashboard
Settings tab will do on-device: filter the airport-keyed ATC index by
receiver location, sort by distance, list frequencies grouped by type.

Usage:
    python query_atc_nearby.py --lat 40.6413 --lon -73.7781 --radius-nm 25 \\
        --ndjson artifacts/aviation-staging/ourairports-atc.ndjson

Coordinate math: equirectangular projection is fine at ATC scales
(<50nm error is < 0.1nm even at high latitudes).
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path


NM_PER_DEG_LAT = 60.0  # 1 arc-minute of latitude = 1 nautical mile


def haversine_nm(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    """Great-circle distance in nautical miles."""
    R_NM = 3440.065  # Earth radius in nautical miles
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dp = math.radians(lat2 - lat1)
    dl = math.radians(lon2 - lon1)
    a = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * R_NM * math.asin(math.sqrt(a))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lat", type=float, required=True)
    parser.add_argument("--lon", type=float, required=True)
    parser.add_argument("--radius-nm", type=float, default=25.0)
    parser.add_argument("--ndjson", type=Path,
                        default=Path("artifacts/aviation-staging/ourairports-atc.ndjson"))
    parser.add_argument("--airport-types", nargs="*",
                        default=["large_airport", "medium_airport", "small_airport"],
                        help="restrict to these airport types")
    parser.add_argument("--limit", type=int, default=50)
    args = parser.parse_args()

    # Cheap prefilter box: 1 degree lat ~ 60 nm, 1 degree lon ~ 60*cos(lat) nm
    lat_deg_span = args.radius_nm / NM_PER_DEG_LAT + 0.1
    lon_deg_span = args.radius_nm / (NM_PER_DEG_LAT * max(0.1, math.cos(math.radians(args.lat)))) + 0.1

    results: list[tuple[float, dict]] = []
    with args.ndjson.open(encoding="utf-8") as f:
        for line in f:
            rec = json.loads(line)
            if rec.get("airport_type") not in args.airport_types:
                continue
            ap_lat = rec["lat_e7"] / 10_000_000
            ap_lon = rec["lon_e7"] / 10_000_000
            if abs(ap_lat - args.lat) > lat_deg_span:
                continue
            if abs(ap_lon - args.lon) > lon_deg_span:
                continue
            d = haversine_nm(args.lat, args.lon, ap_lat, ap_lon)
            if d <= args.radius_nm:
                results.append((d, rec))

    results.sort(key=lambda x: x[0])
    results = results[: args.limit]

    print(f"Airports with ATC within {args.radius_nm:.0f} nm of "
          f"({args.lat:.4f}, {args.lon:.4f}): {len(results)}")
    print()
    for dist, rec in results:
        icao = rec["icao"]
        iata = f" / {rec['iata']}" if rec["iata"] else ""
        name = rec["name"]
        city = rec.get("city") or ""
        loc_bits = [b for b in (city, rec["country"]) if b]
        loc = ", ".join(loc_bits)
        print(f"  {dist:5.1f} nm  {icao}{iata}  {name}  ({loc})  [{rec['airport_type']}]")
        for fr in rec["frequencies"]:
            freq_mhz = fr["freq_hz"] / 1_000_000
            desc = f" — {fr['desc']}" if fr.get("desc") else ""
            print(f"            {fr['type']:6s} {freq_mhz:7.3f} MHz{desc}")
        print()


if __name__ == "__main__":
    main()
