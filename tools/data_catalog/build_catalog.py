#!/usr/bin/env python3
"""Build and sign one reproducible OrcSDR catalog-v1 GitHub Release directory.

The input file names the already-normalized runtime index and the untouched
source archive for each approved U.S. data pack. This script never puts a
private key in the repository or uploads a release.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

PACK_IDS = ("faa_aircraft", "faa_aviation", "noaa_weather", "fcc_broadcast", "lane_county_map",
            "international_broadcast", "hf_schedules")
BROADCAST_DESTINATIONS = {
    "fcc_broadcast": "/orcsdr/data/fcc_broadcast.idx",
    "international_broadcast": "/orcsdr/data/international_broadcast.idx",
    "hf_schedules": "/orcsdr/data/hf_schedules.idx",
}
DEFAULT_BROADCAST_LEDGER = Path(__file__).with_name("broadcast-sources.json")


def is_p25_pack(pack_id: str) -> bool:
    return (pack_id.startswith("p25_") and len(pack_id) < 20 and
            all(character.isalnum() or character in "-_" for character in pack_id))


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_artifact(pack_id: str, source: Path, archive: bool) -> None:
    with source.open("rb") as stream:
        prefix = stream.read(8)
    if archive:
        if prefix[:4] not in (b"PK\x03\x04", b"PK\x05\x06"):
            raise ValueError(f"{pack_id} source archive must be a ZIP: {source}")
    elif is_p25_pack(pack_id):
        fields: dict[str, list[str]] = {}
        for raw_line in source.read_text(encoding="utf-8").splitlines():
            line = raw_line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = (part.strip() for part in line.split("=", 1))
            fields.setdefault(key, []).append(value)
        channels = fields.get("control_channel_hz", [])
        if fields.get("version") != ["2"] or not channels or any(
                not value.isdecimal() for value in channels):
            raise ValueError(f"{pack_id} runtime must be a version-2 P25 profile: {source}")
    elif pack_id == "faa_aircraft":
        if prefix != b"ORCADSB1":
            raise ValueError(f"{pack_id} runtime index must start with ORCADSB1: {source}")
    elif pack_id == "lane_county_map":
        if prefix != b"ORCMAP1\n":
            raise ValueError(f"{pack_id} runtime index must start with ORCMAP1: {source}")
    elif pack_id in BROADCAST_DESTINATIONS:
        if prefix != b"ORCBRD1\n":
            raise ValueError(f"{pack_id} runtime index must start with ORCBRD1: {source}")
    elif prefix != b"ORCCAT1\n":
        raise ValueError(f"{pack_id} runtime index must start with ORCCAT1: {source}")


def copy_artifact(source: Path, release_dir: Path, release_base: str, name: str) -> dict[str, object]:
    if not source.is_file():
        raise ValueError(f"missing artifact: {source}")
    target = release_dir / name
    shutil.copy2(source, target)
    return {"url": f"{release_base.rstrip('/')}/{target.name}", "bytes": target.stat().st_size,
            "sha256": sha256(target)}


def validate_broadcast_sources(pack: dict[str, object], allowed_sources: dict[str, dict[str, object]]) -> None:
    source_ids = pack.get("source_ids")
    if not isinstance(source_ids, list) or not source_ids or not all(isinstance(value, str) for value in source_ids):
        raise ValueError(f"{pack['id']} requires non-empty source_ids")
    for source_id in source_ids:
        source = allowed_sources.get(source_id)
        if source is None:
            raise ValueError(f"{pack['id']} references unknown source {source_id}")
        if not source.get("release_allowed"):
            raise ValueError(f"{pack['id']} source {source_id} is not approved for release")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path, help="approved pack-input JSON")
    parser.add_argument("--out", type=Path, required=True, help="release-asset directory")
    parser.add_argument("--private-key", type=Path, required=True,
                        help="offline P-256 PEM key; never commit it")
    parser.add_argument("--verify-public-key", type=Path,
                        help="optional PEM key used to verify the generated signature")
    parser.add_argument("--release-base", required=True,
                        help="final GitHub Release asset URL prefix")
    parser.add_argument("--openssl", default="openssl")
    parser.add_argument("--broadcast-ledger", type=Path, default=DEFAULT_BROADCAST_LEDGER)
    args = parser.parse_args()

    spec = json.loads(args.input.read_text(encoding="utf-8"))
    if spec.get("schema") != "catalog-input-v1":
        raise ValueError("expected catalog-input-v1")
    packs = spec.get("packs", [])
    source_rows = json.loads(args.broadcast_ledger.read_text(encoding="utf-8")).get("sources", [])
    allowed_sources = {row.get("id"): row for row in source_rows if isinstance(row, dict) and row.get("id")}
    ids = [pack.get("id") for pack in packs]
    if (not ids or len(set(ids)) != len(ids) or
            any(pack_id not in PACK_IDS and not is_p25_pack(pack_id) for pack_id in ids) or
            len(ids) > 16):
        raise ValueError("packs must use unique supported IDs")
    args.out.mkdir(parents=True, exist_ok=True)
    catalog_packs = []
    for pack in packs:
        runtime_source = Path(pack["runtime"])
        archive_source = Path(pack["archive"])
        validate_artifact(pack["id"], runtime_source, False)
        validate_artifact(pack["id"], archive_source, True)
        runtime = copy_artifact(runtime_source, args.out, args.release_base,
                                f"{pack['id']}-runtime{runtime_source.suffix}")
        archive = copy_artifact(archive_source, args.out, args.release_base,
                                f"{pack['id']}-source{archive_source.suffix}")
        runtime["destination"] = pack["runtime_destination"]
        archive["destination"] = pack["archive_destination"]
        catalog_pack = {
            "id": pack["id"], "version": pack["version"],
            "source_date": pack["source_date"], "source_url": pack["source_url"],
            "redistribution": pack["redistribution"],
            "artifacts": {"runtime": runtime, "archive": archive},
        }
        if is_p25_pack(pack["id"]):
            expected_destination = f"/orcsdr/p25/{pack['id']}/profile.cfg"
            if pack["runtime_destination"] != expected_destination or not pack.get("title"):
                raise ValueError(f"{pack['id']} requires title and runtime_destination {expected_destination}")
            catalog_pack["title"] = pack["title"]
        elif pack["id"] in BROADCAST_DESTINATIONS:
            validate_broadcast_sources(pack, allowed_sources)
            expected_destination = BROADCAST_DESTINATIONS[pack["id"]]
            if pack["runtime_destination"] != expected_destination:
                raise ValueError(f"{pack['id']} requires runtime_destination {expected_destination}")
        catalog_packs.append(catalog_pack)
    catalog = {"schema": "catalog-v1", "generated_at": spec["generated_at"],
               "minimum_firmware": spec["minimum_firmware"], "packs": catalog_packs}
    catalog_path = args.out / "catalog-v1.json"
    catalog_path.write_text(json.dumps(catalog, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    der_path = args.out / "catalog-v1.sig.der"
    subprocess.run([args.openssl, "dgst", "-sha256", "-sign", str(args.private_key),
                    "-out", str(der_path), str(catalog_path)], check=True)
    if args.verify_public_key:
        subprocess.run([args.openssl, "dgst", "-sha256", "-verify", str(args.verify_public_key),
                        "-signature", str(der_path), str(catalog_path)], check=True)
    (args.out / "catalog-v1.sig").write_bytes(base64.b64encode(der_path.read_bytes()) + b"\n")
    der_path.unlink()
    print(json.dumps({"catalog": str(catalog_path), "sha256": sha256(catalog_path),
                      "signature": str(args.out / "catalog-v1.sig")}, indent=2))


if __name__ == "__main__":
    main()
