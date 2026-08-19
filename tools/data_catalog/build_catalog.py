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

PACK_IDS = ("faa_aircraft", "faa_aviation", "noaa_weather", "fcc_broadcast", "lane_county_map")


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
    elif pack_id == "faa_aircraft":
        if prefix != b"ORCADSB1":
            raise ValueError(f"{pack_id} runtime index must start with ORCADSB1: {source}")
    elif pack_id == "lane_county_map":
        if prefix[:7] != b"ORCMAP1":
            raise ValueError(f"{pack_id} runtime index must start with ORCMAP1: {source}")
    elif prefix != b"ORCCAT1\n":
        raise ValueError(f"{pack_id} runtime index must start with ORCCAT1: {source}")


def copy_artifact(source: Path, release_dir: Path, release_base: str, name: str) -> dict[str, object]:
    if not source.is_file():
        raise ValueError(f"missing artifact: {source}")
    target = release_dir / name
    shutil.copy2(source, target)
    return {"url": f"{release_base.rstrip('/')}/{target.name}", "bytes": target.stat().st_size,
            "sha256": sha256(target)}


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
    args = parser.parse_args()

    spec = json.loads(args.input.read_text(encoding="utf-8"))
    if spec.get("schema") != "catalog-input-v1":
        raise ValueError("expected catalog-input-v1")
    packs = spec.get("packs", [])
    ids = [pack.get("id") for pack in packs]
    expected_ids = [pack_id for pack_id in PACK_IDS if pack_id in ids]
    if not ids or ids != expected_ids or len(set(ids)) != len(ids):
        raise ValueError("packs must be unique supported IDs in canonical order")
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
        catalog_packs.append({
            "id": pack["id"], "version": pack["version"],
            "source_date": pack["source_date"], "source_url": pack["source_url"],
            "redistribution": pack["redistribution"],
            "artifacts": {"runtime": runtime, "archive": archive},
        })
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
