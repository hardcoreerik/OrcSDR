#!/usr/bin/env python3
"""Validate and replay the local LoRa ORCIQ regression corpus."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[1]))
from decode_orciq import _message_summary, decode_capture, read_capture


REQUIRED_FIELDS = {
    "capture_id", "filename", "path", "sha256", "captured_at",
    "sample_rate_sps", "center_frequency_hz", "bandwidth_hz",
    "spreading_factor", "modem_preset", "region", "expected", "reference",
    "host", "native", "differential_class", "classification", "setup", "notes",
}
CLASSIFICATIONS = {
    "controlled-positive", "uncontrolled-background-lora",
    "confirmed-non-lora-negative", "unknown",
}
DIFFERENTIAL_CLASSES = {"A", "B", "C", "D", "unknown"}
DIFFERENTIAL = {
    ("pass", "pass"): "A",
    ("pass", "fail"): "B",
    ("fail", "fail"): "C",
    ("fail", "pass"): "D",
}


def load_manifest(path: Path) -> dict:
    manifest = json.loads(path.read_text(encoding="utf-8"))
    validate_manifest(manifest)
    return manifest


def validate_manifest(manifest: dict) -> None:
    if manifest.get("schema_version") != 1 or not isinstance(manifest.get("captures"), list):
        raise ValueError("unsupported manifest schema")
    seen = set()
    for entry in manifest["captures"]:
        missing = REQUIRED_FIELDS - entry.keys()
        if missing:
            raise ValueError("missing fields: " + ", ".join(sorted(missing)))
        capture_id = entry.get("capture_id")
        if capture_id in seen:
            raise ValueError(f"duplicate capture_id: {capture_id}")
        seen.add(capture_id)
        path = Path(entry["path"])
        if path.is_absolute() or ".." in path.parts or path.suffix.lower() != ".orciq":
            raise ValueError(f"relative .orciq path required: {entry['path']}")
        if path.name != entry["filename"]:
            raise ValueError("filename does not match path")
        if not re.fullmatch(r"[0-9a-f]{64}", entry["sha256"]):
            raise ValueError("invalid sha256")
        if entry["classification"] not in CLASSIFICATIONS:
            raise ValueError("invalid classification")
        if entry["differential_class"] not in DIFFERENTIAL_CLASSES:
            raise ValueError("invalid differential_class")
        if capture_id != f"orciq-{entry['sha256'][:16]}":
            raise ValueError("capture_id does not match sha256")


def verify_capture(entry: dict, corpus_root: Path) -> dict:
    path = corpus_root / entry["path"]
    with path.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    if digest != entry["sha256"]:
        raise ValueError(f"SHA-256 mismatch for {entry['capture_id']}")
    rate, frequency, sf, bandwidth, raw = read_capture(path)
    header = {
        "sample_rate_sps": rate,
        "center_frequency_hz": frequency,
        "bandwidth_hz": bandwidth,
        "spreading_factor": sf,
        "raw_bytes": len(raw),
    }
    for field in ("sample_rate_sps", "center_frequency_hz", "bandwidth_hz", "spreading_factor"):
        if header[field] != entry[field]:
            raise ValueError(f"header {field} mismatch for {entry['capture_id']}")

    started = time.perf_counter()
    try:
        decoded = decode_capture(path)
        exception = None
    except ValueError as error:
        decoded = []
        exception = str(error)
    elapsed_ms = round((time.perf_counter() - started) * 1000, 3)
    packets = [
        {
            "from": result["from"],
            "packet_id": result["id"],
            "port": result["port"],
            "summary": _message_summary(result["port"], result["payload"]),
        }
        for result in decoded
        if "error" not in result
    ]
    host_result = "pass" if packets else "fail"
    expected = entry["expected"]
    matches_expected = expected is None or any(
        packet["packet_id"] == expected["packet_id"]
        and packet["summary"] == expected["plaintext"]
        for packet in packets
    )
    native_result = entry["native"]["result"]
    differential_class = DIFFERENTIAL.get((host_result, native_result), "unknown")
    return {
        "capture_id": entry["capture_id"],
        "sha256_ok": True,
        "header": header,
        "host": {
            "result": host_result,
            "elapsed_ms": elapsed_ms,
            "packets": packets,
            "rejections": [result["error"] for result in decoded if "error" in result],
            "exception": exception,
            "matches_expected": matches_expected,
        },
        "native": entry["native"],
        "differential_class": differential_class,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--corpus-root", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    manifest = load_manifest(args.manifest)
    captures = [verify_capture(entry, args.corpus_root) for entry in manifest["captures"]]
    by_id = {entry["capture_id"]: entry for entry in manifest["captures"]}
    for result in captures:
        expected = by_id[result["capture_id"]]
        if result["host"]["result"] != expected["host"]["result"]:
            raise ValueError(f"host result mismatch for {result['capture_id']}")
        if not result["host"]["matches_expected"]:
            raise ValueError(f"expected packet mismatch for {result['capture_id']}")
        if result["differential_class"] != expected["differential_class"]:
            raise ValueError(f"differential class mismatch for {result['capture_id']}")
    report = {"schema_version": 1, "manifest": str(args.manifest), "captures": captures}
    text = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
