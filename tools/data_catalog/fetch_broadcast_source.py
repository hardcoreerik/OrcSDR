#!/usr/bin/env python3
"""Fetch one approved raw broadcast source and write a provenance receipt."""

from __future__ import annotations

import argparse
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
from urllib.request import Request, urlopen


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ledger", type=Path, default=Path(__file__).with_name("broadcast-sources.json"))
    parser.add_argument("--source", required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--file", type=Path, help="already downloaded source to receipt only")
    args = parser.parse_args()

    ledger = json.loads(args.ledger.read_text(encoding="utf-8"))
    source = next((row for row in ledger["sources"] if row["id"] == args.source), None)
    if source is None:
        raise SystemExit(f"unknown source: {args.source}")
    if not source["release_allowed"]:
        raise SystemExit(f"source is gated: {source['redistribution']}")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    if args.file:
        if not args.file.is_file():
            raise SystemExit(f"source file not found: {args.file}")
        args.out.write_bytes(args.file.read_bytes())
        url = source["catalog_url"]
    else:
        url = source.get("download_url")
        if not url:
            raise SystemExit(f"manual download required: {source['catalog_url']}")
        request = Request(url, headers={"User-Agent": "OrcSDR station-data importer/1.0"})
        with urlopen(request, timeout=60) as response, args.out.open("wb") as output:
            output.write(response.read())
    with args.out.open("rb") as raw:
        digest = hashlib.file_digest(raw, "sha256").hexdigest()
    receipt = {"source": source["id"], "url": url,
               "retrieved_at": datetime.now(timezone.utc).isoformat(),
               "sha256": digest,
               "bytes": args.out.stat().st_size, "redistribution": source["redistribution"]}
    args.out.with_suffix(args.out.suffix + ".receipt.json").write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(receipt, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
