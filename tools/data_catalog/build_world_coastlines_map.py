#!/usr/bin/env python3
"""Build the embedded ORCMAP1 world-coastlines pack from Natural Earth GeoJSON.

Keeps the existing ORCMAP1 format (640 segment / 32 label caps). Output is the
firmware default basemap at apps/orcsdr-tab5/main/world_coastlines.idx.

Natural Earth data is public domain. Prefer 110m coastlines for the flash budget.

Usage:
  python build_world_coastlines_map.py \\
    --geojson ne_110m_coastline.geojson \\
    --out ../../apps/orcsdr-tab5/main/world_coastlines.idx
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

DEFAULT_LABELS = [
    (40.71, -74.01, "New York"),
    (34.05, -118.24, "Los Angeles"),
    (41.88, -87.63, "Chicago"),
    (29.76, -95.37, "Houston"),
    (33.45, -112.07, "Phoenix"),
    (51.51, -0.13, "London"),
    (48.86, 2.35, "Paris"),
    (52.52, 13.41, "Berlin"),
    (55.76, 37.62, "Moscow"),
    (35.68, 139.69, "Tokyo"),
    (31.23, 121.47, "Shanghai"),
    (19.08, 72.88, "Mumbai"),
    (-33.87, 151.21, "Sydney"),
    (-23.55, -46.63, "Sao Paulo"),
    (-34.60, -58.38, "Buenos Aires"),
    (30.04, 31.24, "Cairo"),
    (-1.29, 36.82, "Nairobi"),
    (6.52, 3.38, "Lagos"),
    (39.90, 116.40, "Beijing"),
    (37.57, 126.98, "Seoul"),
    (1.35, 103.82, "Singapore"),
    (25.20, 55.27, "Dubai"),
    (43.65, -79.38, "Toronto"),
    (49.28, -123.12, "Vancouver"),
    (25.76, -80.19, "Miami"),
    (44.05, -123.09, "Eugene"),
    (45.52, -122.68, "Portland"),
]


def rdp(pts: list[tuple[float, float]], eps: float) -> list[tuple[float, float]]:
    if len(pts) < 3:
        return pts

    def perp(a, b, p) -> float:
        if a == b:
            return math.hypot(p[0] - a[0], p[1] - a[1])
        t = ((p[0] - a[0]) * (b[0] - a[0]) + (p[1] - a[1]) * (b[1] - a[1])) / (
            (b[0] - a[0]) ** 2 + (b[1] - a[1]) ** 2
        )
        t = max(0.0, min(1.0, t))
        return math.hypot(p[0] - (a[0] + t * (b[0] - a[0])), p[1] - (a[1] + t * (b[1] - a[1])))

    dmax = -1.0
    idx = 0
    for i in range(1, len(pts) - 1):
        d = perp(pts[0], pts[-1], pts[i])
        if d > dmax:
            dmax = d
            idx = i
    if dmax > eps:
        return rdp(pts[: idx + 1], eps)[:-1] + rdp(pts[idx:], eps)
    return [pts[0], pts[-1]]


def seglen_deg(poly: list[tuple[float, float]]) -> float:
    total = 0.0
    for i in range(len(poly) - 1):
        lat1, lon1 = poly[i]
        lat2, lon2 = poly[i + 1]
        total += math.hypot(lat2 - lat1, lon2 - lon1)
    return total


def iter_rings(geometry: dict):
    gtype = geometry.get("type")
    coords = geometry.get("coordinates")
    if gtype == "LineString":
        yield [(latlon[1], latlon[0]) for latlon in coords]  # geojson lon,lat -> lat,lon
    elif gtype == "MultiLineString":
        for line in coords:
            yield [(latlon[1], latlon[0]) for latlon in line]
    elif gtype == "Polygon":
        for ring in coords:
            yield [(latlon[1], latlon[0]) for latlon in ring]
    elif gtype == "MultiPolygon":
        for poly in coords:
            for ring in poly:
                yield [(latlon[1], latlon[0]) for latlon in ring]


def build(geojson: dict, segment_cap: int = 640, label_cap: int = 32, eps: float = 1.75):
    cands: list[tuple[list[tuple[float, float]], float]] = []
    for feature in geojson.get("features", []):
        geometry = feature.get("geometry") or {}
        for ring in iter_rings(geometry):
            if len(ring) < 2:
                continue
            simp = rdp(ring, eps)
            if len(simp) < 2:
                continue
            length = seglen_deg(simp)
            if length < 0.5:
                continue
            cands.append((simp, length))
    cands.sort(key=lambda item: -item[1])
    segs: list[tuple[float, float, float, float]] = []
    for poly, _ in cands:
        need = len(poly) - 1
        if need <= 0:
            continue
        if len(segs) + need > segment_cap:
            poly2 = rdp(poly, eps * 2)
            need = len(poly2) - 1
            if need <= 0 or len(segs) + need > segment_cap:
                continue
            poly = poly2
        for i in range(len(poly) - 1):
            lat1, lon1 = poly[i]
            lat2, lon2 = poly[i + 1]
            segs.append((lat1, lon1, lat2, lon2))
        if len(segs) >= segment_cap:
            break
    labels = DEFAULT_LABELS[:label_cap]
    lines = ["ORCMAP1\n"]
    for lat1, lon1, lat2, lon2 in segs[:segment_cap]:
        lines.append(f"W {lat1:.5f} {lon1:.5f} {lat2:.5f} {lon2:.5f}\n")
    for lat, lon, name in labels:
        lines.append(f"L {lat:.6f} {lon:.6f} {name}\n")
    return "".join(lines), len(segs[:segment_cap]), len(labels)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--geojson", type=Path, required=True, help="Natural Earth coastline GeoJSON")
    ap.add_argument("--out", type=Path, required=True, help="Output ORCMAP1 .idx path")
    ap.add_argument("--segment-cap", type=int, default=640)
    ap.add_argument("--label-cap", type=int, default=32)
    ap.add_argument("--eps", type=float, default=1.75, help="RDP epsilon in degrees")
    args = ap.parse_args()
    geojson = json.loads(args.geojson.read_text(encoding="utf-8"))
    text, nseg, nlab = build(geojson, args.segment_cap, args.label_cap, args.eps)
    args.out.write_text(text, encoding="utf-8")
    print(f"wrote {args.out} segments={nseg} labels={nlab}")


if __name__ == "__main__":
    main()
