#!/usr/bin/env python3
"""Check unchanged regions in two native PNGs; never trust a preview as pixel evidence."""
import argparse
import json
from PIL import Image, ImageChops


def compare_regions(before, after, regions):
    if before.size != after.size:
        raise ValueError("compare captures from the same resolution and orientation")
    before, after = before.convert("RGB"), after.convert("RGB")
    results = []
    for name, (x, y, width, height) in regions:
        if x < 0 or y < 0 or width <= 0 or height <= 0 or x + width > before.width or y + height > before.height:
            raise ValueError("region outside image: " + name)
        box = (x, y, x + width, y + height)
        changed = ImageChops.difference(before.crop(box), after.crop(box)).getbbox()
        results.append({"name": name, "unchanged": changed is None, "changed_bounds_in_region": changed})
    return results


def region(text):
    try:
        name, bounds = text.split(":", 1)
        values = tuple(int(v) for v in bounds.split(","))
        if not name or len(values) != 4:
            raise ValueError()
        return name, values
    except ValueError:
        raise argparse.ArgumentTypeError("use NAME:X,Y,WIDTH,HEIGHT") from None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before")
    parser.add_argument("after")
    parser.add_argument("--retain", action="append", required=True, type=region)
    args = parser.parse_args()
    try:
        with Image.open(args.before) as before, Image.open(args.after) as after:
            results = compare_regions(before, after, args.retain)
    except (ValueError, OSError) as error:
        parser.error(str(error))
    print(json.dumps({"regions": results}, indent=2))
    return 0 if all(item["unchanged"] for item in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
