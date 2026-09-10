#!/usr/bin/env python3
"""Build a readable ORCMAP1 Lane County pack from Overpass/OSM JSON.

Keeps the existing ORCMAP1 format (640 segment / 32 label caps).
Prioritizes longer major/tertiary roads, rivers, and runways so the
basemap reads on-device with a brighter OrcSDR draw palette.

Usage:
  python build_lane_county_map.py \
    --osm lane_county_osm_source.json \
    --out lane_county_map.idx

Input JSON shape: Overpass elements[] with way geometry lat/lon + tags.
"""
from __future__ import annotations

import argparse
import math
from pathlib import Path


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


def seglen_nm(poly: list[tuple[float, float]]) -> float:
    total = 0.0
    for i in range(len(poly) - 1):
        lat1, lon1 = poly[i]
        lat2, lon2 = poly[i + 1]
        total += math.hypot(
            (lat2 - lat1) * 60.0,
            (lon2 - lon1) * 60.0 * math.cos(math.radians((lat1 + lat2) / 2.0)),
        )
    return total


DEFAULT_LABELS = [
    (44.050505, -123.095051, "Eugene"),
    (44.046236, -123.022029, "Springfield"),
    (44.137628, -123.066199, "Coburg"),
    (44.219194, -123.204785, "Junction City"),
    (44.120982, -123.264246, "Alvadore"),
    (44.158402, -123.307710, "Franklin"),
    (43.9179, -123.0, "Creswell"),
    (44.2715, -123.168, "Harrisburg"),
    (44.058, -123.07, "EUG"),
    (44.045, -123.15, "Veneta"),
]

PRIO = {
    "motorway": 6,
    "trunk": 5,
    "primary": 4,
    "secondary": 3,
    "tertiary": 1,
    "river": 5,
    "canal": 2,
    "runway": 3,
}


def build(osm: dict, segment_cap: int = 640, label_cap: int = 32):
    import json  # noqa: F401 — caller passes parsed dict

    cands = []
    for e in osm.get("elements", []):
        if e.get("type") != "way" or not e.get("geometry"):
            continue
        tags = e.get("tags") or {}
        poly = [(g["lat"], g["lon"]) for g in e["geometry"]]
        if len(poly) < 2:
            continue
        if tags.get("aeroway") == "runway":
            kind, p, eps = "A", PRIO["runway"], 0.002
        elif tags.get("waterway") in ("river", "canal"):
            kind, p, eps = "W", PRIO.get(tags["waterway"], 1), 0.002
        elif tags.get("highway") in ("motorway", "trunk", "primary", "secondary", "tertiary"):
            kind, p = "R", PRIO[tags["highway"]]
            eps = 0.0015 if p >= 3 else 0.004
        else:
            continue
        simp = rdp(poly, eps)
        if len(simp) < 2:
            continue
        length = seglen_nm(simp)
        if length < 0.25:
            continue
        cands.append((kind, simp, length * p))

    cands.sort(key=lambda x: -x[2])
    segs = []
    for kind, poly, _ in cands:
        need = len(poly) - 1
        if need <= 0:
            continue
        if len(segs) + need > segment_cap:
            poly2 = rdp(poly, 0.01)
            need = len(poly2) - 1
            if need <= 0 or len(segs) + need > segment_cap:
                continue
            poly = poly2
        for i in range(len(poly) - 1):
            lat1, lon1 = poly[i]
            lat2, lon2 = poly[i + 1]
            segs.append((kind, lat1, lon1, lat2, lon2))
        if len(segs) >= segment_cap:
            break

    labels = DEFAULT_LABELS[:label_cap]
    lines = ["ORCMAP1\n"]
    for kind, lat1, lon1, lat2, lon2 in segs[:segment_cap]:
        lines.append(f"{kind} {lat1:.6f} {lon1:.6f} {lat2:.6f} {lon2:.6f}\n")
    for lat, lon, name in labels:
        lines.append(f"L {lat:.6f} {lon:.6f} {name}\n")
    return "".join(lines), len(segs[:segment_cap]), len(labels)


def main() -> None:
    import json

    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--osm", type=Path, required=True, help="Overpass JSON with way geometries")
    ap.add_argument("--out", type=Path, required=True, help="Output ORCMAP1 .idx path")
    ap.add_argument("--segment-cap", type=int, default=640)
    ap.add_argument("--label-cap", type=int, default=32)
    args = ap.parse_args()
    osm = json.loads(args.osm.read_text())
    text, nseg, nlab = build(osm, args.segment_cap, args.label_cap)
    args.out.write_text(text)
    print(f"wrote {args.out} segments={nseg} labels={nlab}")


if __name__ == "__main__":
    main()
