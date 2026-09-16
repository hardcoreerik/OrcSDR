#!/usr/bin/env python3
"""Stage an OrcMaps world basemap for the Tab5 `orcmaps` flash partition.

Produces the raw image written at the partition's offset, plus a provenance
record naming exactly which pack is in the firmware. It does not flash
anything and does not build the application.

The pack is an ordinary PMTiles archive. Nothing here converts it into a
firmware-specific format: the bytes placed in flash are byte-identical to the
bytes OrcMaps' own tooling produced, so the same archive can be read from SD,
from a host file, or from this partition through the same PmTilesReader.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

# Must match apps/orcsdr-tab5/partitions.csv. Checked rather than assumed,
# because a partition that moved or shrank silently turns into a map that
# reads as garbage on the device.
PARTITION_NAME = "orcmaps"
PARTITION_OFFSET = 0x450000
PARTITION_SIZE = 6 * 1024 * 1024

CSV_ROW = re.compile(
    r"^\s*(?P<name>[A-Za-z0-9_]+)\s*,\s*(?P<type>\w+)\s*,\s*(?P<subtype>\w+)\s*,"
    r"\s*(?P<offset>\S*)\s*,\s*(?P<size>\S+?)\s*,")

SIZE_SUFFIX = {"K": 1024, "M": 1024 * 1024}


def parse_size(text: str) -> int:
    text = text.strip().rstrip(",")
    if text.lower().startswith("0x"):
        return int(text, 16)
    if text and text[-1].upper() in SIZE_SUFFIX:
        return int(text[:-1]) * SIZE_SUFFIX[text[-1].upper()]
    return int(text)


def partition_size_from_csv(csv_path: Path) -> int:
    """Read the partition's declared size out of the real CSV.

    The offset is left blank in the CSV for ESP-IDF to compute, so only the
    size can be cross-checked here; the offset is verified against the built
    partition table by verify_partition_table().
    """
    for line in csv_path.read_text(encoding="utf-8").splitlines():
        if line.lstrip().startswith("#"):
            continue
        match = CSV_ROW.match(line)
        if match and match.group("name") == PARTITION_NAME:
            return parse_size(match.group("size"))
    raise ValueError(f"no '{PARTITION_NAME}' partition in {csv_path}")


def verify_partition_table(binary: Path) -> tuple[int, int]:
    """Read the built partition-table.bin and return the map partition's
    (offset, size) as the bootloader will actually see them."""
    import struct

    data = binary.read_bytes()
    for i in range(0, len(data), 32):
        entry = data[i:i + 32]
        if len(entry) < 32 or entry[:2] != b"\xaa\x50":
            break
        offset, size = struct.unpack("<II", entry[4:12])
        name = entry[12:28].rstrip(b"\x00").decode("ascii", "replace")
        if name == PARTITION_NAME:
            return offset, size
    raise ValueError(f"no '{PARTITION_NAME}' entry in {binary}")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pack", type=Path, required=True,
                        help="OrcMaps world overview .pmtiles")
    parser.add_argument("--manifest", type=Path,
                        help="Pack manifest (default: the pack's own stem)")
    parser.add_argument("--output", type=Path, required=True,
                        help="Raw image to write at the partition offset")
    parser.add_argument("--partitions-csv", type=Path,
                        help="Cross-check the declared partition size")
    parser.add_argument("--partition-table-bin", type=Path,
                        help="Cross-check the built table's offset and size")
    parser.add_argument("--expect-sha256",
                        help="Refuse a pack whose hash is not this")
    args = parser.parse_args(argv)

    if not args.pack.is_file():
        raise FileNotFoundError(f"pack not found: {args.pack}")

    manifest_path = args.manifest
    if manifest_path is None:
        candidate = args.pack.with_suffix(".manifest.json")
        manifest_path = candidate if candidate.is_file() else None
    manifest = (json.loads(manifest_path.read_text(encoding="utf-8"))
                if manifest_path else {})

    offset = PARTITION_OFFSET
    size = PARTITION_SIZE
    if args.partitions_csv:
        size = partition_size_from_csv(args.partitions_csv)
    if args.partition_table_bin:
        offset, size = verify_partition_table(args.partition_table_bin)

    pack_bytes = args.pack.stat().st_size
    if pack_bytes > size:
        raise ValueError(
            f"pack is {pack_bytes} bytes but the '{PARTITION_NAME}' partition "
            f"holds {size}; shrink the pack or grow the partition")

    digest = sha256(args.pack)
    # A recorded hash that disagrees with the file means the artifact changed
    # under us, which must stop the build rather than ship an unknown map.
    if args.expect_sha256 and digest != args.expect_sha256.lower():
        raise ValueError(
            f"pack SHA-256 {digest} does not match expected "
            f"{args.expect_sha256.lower()}")
    recorded = str(manifest.get("output_sha256", "")).lower()
    if recorded and recorded != digest:
        raise ValueError(
            f"pack SHA-256 {digest} disagrees with its manifest "
            f"({recorded}); the pack or its manifest is stale")

    # The image is the archive verbatim. It is NOT padded to the partition
    # size: esptool writes only the bytes given, and leaving the tail erased
    # both shortens the flash write and leaves the unused space obvious.
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(args.pack.read_bytes())

    record = {
        "partition": PARTITION_NAME,
        "offset": f"0x{offset:06X}",
        "partition_size_bytes": size,
        "image": args.output.name,
        "image_bytes": pack_bytes,
        "free_bytes": size - pack_bytes,
        "sha256": digest,
        "pack_id": manifest.get("pack_id"),
        "pack_version": manifest.get("pack_version"),
        "schema_version": manifest.get("schema_version"),
        "content_profile": manifest.get("content_profile"),
        "min_zoom": manifest.get("min_zoom"),
        "max_zoom": manifest.get("max_zoom"),
        "bounds": manifest.get("bounds"),
        "sources": manifest.get("sources"),
        "required_attribution": manifest.get("required_attribution"),
        "attribution_links": manifest.get("attribution_links"),
    }
    provenance = args.output.with_name("orcmaps-world-provenance.json")
    provenance.write_text(json.dumps(record, indent=2, ensure_ascii=False) + "\n",
                          encoding="utf-8")
    print(json.dumps(record, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(2)
