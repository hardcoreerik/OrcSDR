#!/usr/bin/env python3
"""Convert simplified OSM GeoJSON LineString features into ORCMAP1 SD runtime data."""
import json
import sys
from itertools import pairwise
from pathlib import Path

KIND = {"road": "R", "water": "W", "airport": "A"}
RUNTIME_SEGMENT_CAPACITY = 640
RUNTIME_LABEL_CAPACITY = 32

def main(source, output):
    with open(source, encoding="utf-8") as stream:
        data = json.load(stream)
    records = []
    segment_count = label_count = 0
    for feature in data.get("features", []):
        kind = KIND.get(feature.get("properties", {}).get("orcsdr_kind"))
        geometry = feature.get("geometry", {})
        if geometry.get("type") == "Point" and feature.get("properties", {}).get("orcsdr_kind") == "label":
            label = feature["properties"].get("name", "").strip()
            if label and label.isascii() and "\n" not in label:
                lon, lat = geometry["coordinates"]
                records.append(f"L {lat:.6f} {lon:.6f} {label[:23]}\n")
                label_count += 1
            continue
        if not kind or geometry.get("type") != "LineString":
            continue
        points = geometry.get("coordinates", [])
        for a, b in pairwise(points):
            records.append(f"{kind} {a[1]:.6f} {a[0]:.6f} {b[1]:.6f} {b[0]:.6f}\n")
            segment_count += 1
    if segment_count > RUNTIME_SEGMENT_CAPACITY:
        raise ValueError(f"{segment_count} map segments exceed device capacity "
                         f"{RUNTIME_SEGMENT_CAPACITY}")
    if label_count > RUNTIME_LABEL_CAPACITY:
        raise ValueError(f"{label_count} map labels exceed device capacity "
                         f"{RUNTIME_LABEL_CAPACITY}")
    target = Path(output)
    temporary = target.with_suffix(target.suffix + ".tmp")
    try:
        with temporary.open("w", encoding="ascii", newline="\n") as stream:
            stream.write("ORCMAP1\n" + "".join(records))
        temporary.replace(target)
    finally:
        temporary.unlink(missing_ok=True)

if __name__ == "__main__":
    main(*sys.argv[1:])
