#!/usr/bin/env python3
"""Verify Git blob SHA-1 of canonical sources; optionally check reference checkout copies.

No credentials, network, or git executable required. The pinned reference
heads are evidence snapshots; comparing a newer branch requires regenerating
and reviewing the inventory, not silently treating drift as compatibility.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
INVENTORY = HERE / "source_inventory.json"
SPDX_ADSB = b"// SPDX-License-Identifier: AGPL-3.0-only\n"


def blob_sha(content):
    header = b"blob " + str(len(content)).encode("ascii") + b"\0"
    return hashlib.sha1(header + content).hexdigest()


def check(path, expected, label, errors, remove_spdx=False):
    if not path.is_file():
        errors.append(f"{label}: missing {path}")
        return
    content = path.read_bytes()
    if remove_spdx:
        if not content.startswith(SPDX_ADSB):
            errors.append(f"{label}: expected added SPDX line missing: {path}")
            return
        content = content[len(SPDX_ADSB):]
    actual = blob_sha(content)
    if actual != expected:
        errors.append(f"{label}: {path} blob {actual} != pinned {expected}")


def run():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--headless", type=Path, help="Root of an optional pinned Headless checkout")
    p.add_argument("--mobile", type=Path, help="Root of an optional pinned Mobile checkout")
    args = p.parse_args()
    data = json.loads(INVENTORY.read_text(encoding="utf-8"))
    errors = []
    for row in data["headless_vendor"]:
        if row["canonical_blob"] is None:
            errors.append(f"Missing canonical mapping for {row['headless_path']}")
            continue
        check(ROOT / row["canonical_path"], row["canonical_blob"], "canonical", errors)
        if args.headless:
            check(args.headless / row["headless_path"], row["headless_blob"], "headless", errors)
    for row in data["mobile_extracts"]:
        check(ROOT / row["canonical_path"], row["canonical_blob"], "canonical", errors)
        if args.mobile:
            check(args.mobile / row["mobile_path"], row["mobile_blob"], "mobile", errors)
            if row["normalization"] == "strip_spdx_line":
                check(args.mobile / row["mobile_path"], row["canonical_blob"], "mobile after SPDX", errors, True)
    print(f"Canonical: {len(data['headless_vendor'])} Headless-mapped files + {len(data['mobile_extracts'])} Mobile mappings checked")
    if args.headless:
        print("Headless: snapshot content checked (not proof of host/WASM/device equivalence)")
    if args.mobile:
        print("Mobile: snapshot content and SPDX-only ADS-B adaptation checked")
    if errors:
        for error in errors:
            print("FAIL: " + error, file=sys.stderr)
        return 1
    print("PASS: Git blob hashes match recorded source snapshots")
    return 0


if __name__ == "__main__":
    raise SystemExit(run())
