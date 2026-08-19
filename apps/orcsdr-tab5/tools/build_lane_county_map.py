#!/usr/bin/env python3
"""Convert simplified OSM GeoJSON LineString features into ORCMAP1 SD runtime data."""
import json
import sys

KIND = {"road": "R", "water": "W", "airport": "A"}

def main(source, output):
    data = json.load(open(source, encoding="utf-8"))
    with open(output, "w", encoding="ascii", newline="\n") as out:
        out.write("ORCMAP1\n")
        for feature in data.get("features", []):
            kind = KIND.get(feature.get("properties", {}).get("orcsdr_kind"))
            geometry = feature.get("geometry", {})
            if not kind or geometry.get("type") != "LineString":
                continue
            points = geometry.get("coordinates", [])
            for a, b in zip(points, points[1:]):
                out.write(f"{kind} {a[1]:.6f} {a[0]:.6f} {b[1]:.6f} {b[0]:.6f}\n")

if __name__ == "__main__":
    main(*sys.argv[1:])
