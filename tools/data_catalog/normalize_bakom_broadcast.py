#!/usr/bin/env python3
"""Normalize the BAKOM (Swiss Federal Office of Communications) Swiss radio
and TV broadcasters open dataset into ORCBRD1 station-card NDJSON.

Source: opendata.swiss dataset ``schweizerische-radio-und-fernsehsender``,
published by BAKOM as a GeoJSON feature collection served from
``data.geo.admin.ch/ch.bakom.radio-fernsehsender/…``. Each feature is a
transmitter site with a parallel comma-separated ``service`` /
``program`` / ``freqchan`` triple naming the services (DAB+, RADIO,
DVB-T) hosted at that site. ``RADIO`` is analogue FM (Switzerland shut
down its last AM broadcaster, Beromünster, in 2008). Digital DAB+ and
DVB-T are excluded — OrcSDR's contract is AM/FM/shortwave only.

Coordinates are Swiss LV95 (EPSG:2056) and are reprojected to WGS84 for
the ORCBRD1 ``latitude_e7`` / ``longitude_e7`` fields.

Licence: opendata.swiss terms_open (free reuse with source citation).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from collections import Counter
from pathlib import Path


SOURCE = "bakom_radio_fernsehsender"
SOURCE_URL = "https://opendata.swiss/en/dataset/schweizerische-radio-und-fernsehsender"
FM_HZ = (76_000_000, 108_500_000)


def _slug(*parts: str) -> str:
    seed = "|".join(p or "" for p in parts).encode("utf-8")
    return hashlib.md5(seed).hexdigest()[:8]


def _snap_fm_hz(value: float) -> int | None:
    hz = round(value * 10) * 100_000
    return hz if FM_HZ[0] <= hz <= FM_HZ[1] else None


def _parse_freq_mhz(text: str) -> int | None:
    text = (text or "").strip()
    m = re.match(r"^(\d+(?:\.\d+)?)\s*MHz$", text, re.IGNORECASE)
    if not m:
        return None
    try:
        return _snap_fm_hz(float(m.group(1)))
    except ValueError:
        return None


def _parse_power_w(text: str) -> int | None:
    text = (text or "").strip()
    m = re.match(r"^([\d.]+)\s*(kW|W|mW)$", text, re.IGNORECASE)
    if not m:
        return None
    try:
        v = float(m.group(1))
    except ValueError:
        return None
    unit = m.group(2).lower()
    if unit == "kw":
        return round(v * 1000)
    if unit == "w":
        return round(v)
    if unit == "mw":
        return round(v / 1000)
    return None


class _Reproject:
    """Lazy LV95 → WGS84 reprojector; no import cost when data is empty."""

    def __init__(self) -> None:
        self._transformer = None

    def __call__(self, east: float, north: float) -> tuple[float, float] | None:
        if self._transformer is None:
            try:
                from pyproj import Transformer
                self._transformer = Transformer.from_crs(
                    "EPSG:2056", "EPSG:4326", always_xy=True)
            except Exception:
                return None
        try:
            lon, lat = self._transformer.transform(east, north)
        except Exception:
            return None
        if lat != lat or lon != lon:
            return None
        return lat, lon


_reproject = _Reproject()


def _feature_records(feature: dict) -> list[dict[str, object]]:
    props = feature.get("properties") or {}
    services = (props.get("service") or "").split(",")
    programs = (props.get("program") or "").split(",")
    freqs = (props.get("freqchan") or "").split(",")
    if not (len(services) == len(programs) == len(freqs)):
        return []
    geom = feature.get("geometry") or {}
    lat_e7 = lon_e7 = 0
    if geom.get("type") == "Point":
        coords = geom.get("coordinates") or []
        if len(coords) >= 2:
            wgs = _reproject(float(coords[0]), float(coords[1]))
            if wgs is not None:
                lat_e7 = round(wgs[0] * 10_000_000)
                lon_e7 = round(wgs[1] * 10_000_000)

    site_name = (props.get("name") or "").strip()
    site_code = (props.get("code") or "").strip()
    power_w = _parse_power_w(props.get("power") or "")

    records: list[dict[str, object]] = []
    for service, program, freq in zip(services, programs, freqs):
        if service.strip().upper() != "RADIO":
            continue
        frequency_hz = _parse_freq_mhz(freq)
        if frequency_hz is None:
            continue
        program_clean = program.strip()
        source_id = f"{site_code or site_name}:{frequency_hz}"
        slug = _slug(source_id, program_clean)
        record: dict[str, object] = {
            "id": f"{SOURCE}:{slug}:{frequency_hz}",
            "source": SOURCE, "source_id": source_id,
            "source_url": SOURCE_URL, "country": "CH", "service": "fm",
            "frequency_hz": frequency_hz,
        }
        if program_clean:
            record["name"] = program_clean[:20]
            if re.match(r"^[A-Z0-9 +/&.\-]+$", program_clean):
                record["callsign"] = program_clean[:15]
        if site_name:
            record["city"] = site_name[:10]
        if lat_e7 or lon_e7:
            record["latitude_e7"] = lat_e7
            record["longitude_e7"] = lon_e7
        if power_w is not None:
            record["power_w"] = power_w
        record["status"] = "LICENSED"
        records.append(record)
    return records


def load(geojson_path: Path) -> list[dict[str, object]]:
    data = json.loads(geojson_path.read_text(encoding="utf-8"))
    records: dict[str, dict[str, object]] = {}
    for feature in data.get("features") or []:
        for row in _feature_records(feature):
            records[str(row["id"])] = row
    return sorted(records.values(),
                  key=lambda r: (int(r["frequency_hz"]), str(r["id"])))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True,
                        help="BAKOM radio-fernsehsender GeoJSON")
    parser.add_argument("--out", type=Path, required=True,
                        help="normalized NDJSON")
    args = parser.parse_args()
    records = load(args.input)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        "".join(json.dumps(r, ensure_ascii=False, sort_keys=True) + "\n"
                for r in records),
        encoding="utf-8", newline="\n")
    by_service = Counter(str(r["service"]) for r in records)
    print(json.dumps({
        "source": SOURCE, "records": len(records),
        "services": dict(sorted(by_service.items())),
    }, ensure_ascii=True, indent=2))


if __name__ == "__main__":
    main()
