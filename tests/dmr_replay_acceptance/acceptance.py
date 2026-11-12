#!/usr/bin/env python3
"""Offline, hash-gated, 48 kS/s DMR IQ acceptance and D1 JSONL bridge harness.

Only Python standard library. Raw IQ is private and never embedded in reports.
Do not treat upstream assertions as OrcSDR observations.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath
import re
import shlex
import struct
import subprocess
import sys
import threading
import tempfile
from typing import Any

MANIFEST_DEFAULT = Path(__file__).resolve().parents[1] / "digital_voice_replay" / "manifest.json"
FORMAT_BYTES = {"cf32-le": 8, "cs16-le": 4}
SUFFIX_FORMAT = {".cfile": "cf32-le", ".cf32": "cf32-le", ".cs16": "cs16-le"}
DIRECT_IDS = ("dmr-directmode-keyup", "dmr-directmode-second-ptt")
NEGATIVE_IDS = ("auto-negative-noise", "auto-negative-am")
TIER3_IDS = ("dmr-tier3-control", "dmr-tier3-voice-terminator")
SYNC_PATTERNS = frozenset(("bs_voice", "bs_data", "ms_voice", "ms_data", "direct_ts1_voice",
                            "direct_ts1_data", "direct_ts2_voice", "direct_ts2_data"))
FRAME_CLASSES = frozenset(("voice", "data", "control", "unknown"))
STATES = frozenset(("pass", "fail", "unknown"))
CHECK_KINDS = frozenset(("none", "crc16", "rs12_9", "embedded_lc"))
HEX40 = re.compile(r"[0-9a-f]{40}\Z")
HEX64 = re.compile(r"[0-9a-f]{64}\Z")


class AcceptanceError(ValueError):
    pass


def safe_relative(name: Any) -> PurePosixPath:
    if not isinstance(name, str) or not name or "\\" in name or "//" in name or "\0" in name:
        raise AcceptanceError("invalid fixture path")
    path = PurePosixPath(name)
    if path.is_absolute() or any(x in (".", "..", "") for x in name.split("/")) or ":" in path.parts[0]:
        raise AcceptanceError("unsafe fixture path")
    return path


def load_metadata(manifest: Path) -> tuple[dict, dict[str, dict]]:
    try:
        data = json.loads(manifest.read_text(encoding="utf-8"))
    except (ValueError, OSError) as exc:
        raise AcceptanceError(f"manifest unavailable or invalid: {exc}") from exc
    if not isinstance(data, dict) or data.get("schema") != "orcsdr-digital-voice-replay-fixtures-v1":
        raise AcceptanceError("unsupported fixture manifest schema")
    if not isinstance(data.get("files"), list) or not data["files"]:
        raise AcceptanceError("empty fixture manifest")
    for key in ("fixture_repository", "fixture_branch", "fixture_root", "source_commit"):
        if not isinstance(data.get(key), str) or not data[key]:
            raise AcceptanceError(f"missing manifest provenance: {key}")
    by_id: dict[str, dict] = {}
    paths: set[str] = set()
    for row in data["files"]:
        if not isinstance(row, dict):
            raise AcceptanceError("non-object fixture entry")
        fid = row.get("id")
        if not isinstance(fid, str) or not re.fullmatch(r"[a-z][a-z0-9-]*", fid) or fid in by_id:
            raise AcceptanceError("invalid or duplicate fixture ID")
        rel = row.get("path")
        safe_relative(rel)
        if rel in paths:
            raise AcceptanceError("duplicate fixture path")
        paths.add(rel)
        fmt = row.get("format")
        if fmt not in FORMAT_BYTES or (PurePosixPath(rel).suffix.lower() in SUFFIX_FORMAT
                                        and SUFFIX_FORMAT[PurePosixPath(rel).suffix.lower()] != fmt):
            raise AcceptanceError(f"unsupported/conflicting IQ format for {fid}")
        size, rate = row.get("bytes"), row.get("sample_rate_hz")
        if type(size) is not int or size <= 0 or size % FORMAT_BYTES[fmt] or type(rate) is not int or rate != 48000:
            raise AcceptanceError(f"expected raw aligned 48000 sample/s IQ for {fid}")
        sha = row.get("git_blob_sha1")
        sha256 = row.get("sha256")
        if not isinstance(sha, str) or not HEX40.fullmatch(sha) or (sha256 is not None and
            (not isinstance(sha256, str) or not HEX64.fullmatch(sha256))):
            raise AcceptanceError(f"missing/bad fixture hash pin for {fid}")
        if row.get("capture_stage") != "channelized_iq":
            raise AcceptanceError(f"fixture not channelized IQ for {fid}")
        if not isinstance(row.get("upstream_expected", {}), dict):
            raise AcceptanceError("invalid upstream expectations")
        by_id[fid] = row
    return data, by_id


def find_fixture(root: Path, row: dict) -> Path:
    root = root.resolve(strict=True)
    path = root.joinpath(*safe_relative(row["path"]).parts).resolve(strict=False)
    if not path.is_relative_to(root):
        raise AcceptanceError("fixture resolves outside private root")
    return path


def verify(root: Path, row: dict) -> tuple[Path, dict]:
    path = find_fixture(root, row)
    base = {"id": row["id"], "path": row["path"], "format": row["format"],
            "sample_rate_hz": row["sample_rate_hz"], "expected_bytes": row["bytes"],
            "sha256_pinned": row.get("sha256") is not None}
    if not path.is_file():
        return path, {**base, "verified": False, "error": "missing"}
    try:
        size = path.stat().st_size
        if size != row["bytes"]:
            return path, {**base, "verified": False, "actual_bytes": size, "error": "length_mismatch"}
        gitsha = hashlib.sha1(f"blob {size}\0".encode("ascii"))
        sha = hashlib.sha256()
        with path.open("rb") as src:
            while block := src.read(1024 * 1024):
                gitsha.update(block)
                sha.update(block)
    except OSError as exc:
        return path, {**base, "verified": False, "error": f"io_error: {exc.strerror}"}
    actual = {**base, "actual_bytes": size, "git_blob_sha1": gitsha.hexdigest(),
              "actual_sha256": sha.hexdigest(), "samples": size // FORMAT_BYTES[row["format"]]}
    if gitsha.hexdigest() != row["git_blob_sha1"]:
        return path, {**actual, "verified": False, "error": "git_blob_sha1_mismatch"}
    if row.get("sha256") is not None and sha.hexdigest() != row["sha256"]:
        return path, {**actual, "verified": False, "error": "sha256_mismatch"}
    return path, {**actual, "verified": True}


def _u32(value: Any, name: str, limit: int = 0xFFFFFFFF) -> int:
    if type(value) is not int or not (0 <= value <= limit):
        raise AcceptanceError(f"bad {name}: {value!r}")
    return value


def aggregate_events(lines: list[str], samples: int, *, negative: bool, upstream: dict) -> dict:
    """Validate bridge claims without elevating sync/uncorrected frames to RF identity."""
    summary: dict[str, Any] = {
        "sync": {name: 0 for name in sorted(SYNC_PATTERNS)}, "sync_total": 0,
        "bursts": 0, "emitted_rejected_bursts": 0, "fec": {name: 0 for name in sorted(STATES)},
        "check": {name: 0 for name in sorted(STATES)},
        "crc": {name: 0 for name in sorted(STATES)},
        "rs": {name: 0 for name in sorted(STATES)},
        "embedded_lc": {name: 0 for name in sorted(STATES)},
        "check_and_fec_valid": 0,
        "slots_observed": [], "slots_check_fec_bursts": [],
        "color_codes": [], "source_ids": [], "talkgroups": [],
        "unverified_identity_claims": 0, "false_identity_detections": 0,
        "upstream_checks": {}, "source_expected_is_upstream_only": True,
        "bridge_samples_consumed": None,
    }
    seen_slots: set[int] = set()
    valid_slots: set[int] = set()
    colors: set[int] = set()
    sources: set[int] = set()
    talkgroups: set[int] = set()
    previous = -1
    summary_seen = False
    event_trace = hashlib.sha256()  # canonical, ordered per-event evidence across IQ chunking
    for line_number, raw in enumerate(lines, 1):
        if not raw.strip():
            continue
        if len(raw) > 16384:
            raise AcceptanceError("oversized bridge event")
        try:
            event = json.loads(raw)
        except ValueError as exc:
            raise AcceptanceError(f"invalid bridge JSONL line {line_number}") from exc
        if not isinstance(event, dict) or event.get("schema") != "orcsdr-dmr-d1-bridge-event-v1":
            raise AcceptanceError(f"unsupported bridge event schema on line {line_number}")
        offset = _u32(event.get("sample_offset"), "sample_offset", max(0, samples - 1))
        if offset < previous:
            raise AcceptanceError("unordered sample offsets")
        previous = offset
        kind = event.get("type")
        if summary_seen:
            raise AcceptanceError("events after bridge end-of-stream summary")
        if kind == "sync":
            if set(event) != {"schema", "type", "sample_offset", "pattern"} or event.get("pattern") not in SYNC_PATTERNS:
                raise AcceptanceError("invalid sync event")
            summary["sync"][event["pattern"]] += 1
            summary["sync_total"] += 1
        elif kind == "burst":
            if set(event) != {"schema", "type", "sample_offset", "kind", "slot", "color_code", "fec", "check", "check_kind", "identity_valid", "identity"}:
                raise AcceptanceError("invalid burst event fields")
            if event.get("kind") not in FRAME_CLASSES or event.get("fec") not in STATES or event.get("check") not in STATES or event.get("check_kind") not in CHECK_KINDS or type(event.get("identity_valid")) is not bool:
                raise AcceptanceError("invalid burst evidence status")
            slot = event.get("slot")
            color = event.get("color_code")
            if slot is not None:
                _u32(slot, "slot", 2)
                if slot == 0:
                    raise AcceptanceError("DMR slot must be 1 or 2")
                seen_slots.add(slot)
            if color is not None:
                _u32(color, "color_code", 15)
            summary["bursts"] += 1
            # Count only FAILED burst events that were actually emitted by the bridge.
            # Decoder-internal discarded candidates are not observable in JSONL v1.
            if event["fec"] == "fail" or event["check"] == "fail":
                summary["emitted_rejected_bursts"] += 1
            summary["fec"][event["fec"]] += 1
            summary["check"][event["check"]] += 1
            if event["check_kind"] == "none" and event["check"] != "unknown":
                raise AcceptanceError("check result requires explicit check kind")
            if event["check_kind"] != "none":
                label = {"crc16": "crc", "rs12_9": "rs", "embedded_lc": "embedded_lc"}[event["check_kind"]]
                summary[label][event["check"]] += 1
            checked = event["fec"] == "pass" and event["check"] == "pass"
            if checked:
                summary["check_and_fec_valid"] += 1
                if slot is not None:
                    valid_slots.add(slot)
                if color is not None:
                    colors.add(color)
            identity = event["identity"]
            if event["identity_valid"] and identity is None:
                raise AcceptanceError("identity_valid event without identity")
            trusted = checked and event["identity_valid"] and event["check_kind"] in ("rs12_9", "embedded_lc")
            if identity is not None:
                if not isinstance(identity, dict) or not identity or not set(identity).issubset({"source_id", "talkgroup"}):
                    raise AcceptanceError("invalid identity structure")
                for k, v in identity.items():
                    _u32(v, k, 0xFFFFFF)
                    if v == 0:
                        raise AcceptanceError("zero-valued identifier requires explicit unknown/null")
                if not trusted:
                    summary["unverified_identity_claims"] += 1
                else:
                    if "source_id" in identity:
                        sources.add(identity["source_id"])
                    if "talkgroup" in identity:
                        talkgroups.add(identity["talkgroup"])
                    if negative:
                        summary["false_identity_detections"] += 1
        elif kind == "summary":
            if set(event) != {"schema", "type", "sample_offset", "consumed_samples"}:
                raise AcceptanceError("bad end-of-stream summary fields")
            consumed = _u32(event.get("consumed_samples"), "consumed_samples")
            if consumed != samples or offset != samples - 1:
                raise AcceptanceError("bridge did not acknowledge all IQ samples")
            summary["bridge_samples_consumed"] = consumed
            summary_seen = True
        else:
            raise AcceptanceError(f"unknown bridge event type {kind!r}")
        if line_number > 200000:
            raise AcceptanceError("too many bridge events")
        # Fingerprint the complete validated event stream, including sample offsets.
        # Counts alone can hide event-level chunk-boundary differences.
        event_trace.update(json.dumps(event, sort_keys=True, separators=(",", ":")).encode("utf-8"))
        event_trace.update(b"\n")
    if not summary_seen:
        raise AcceptanceError("bridge missing end-of-stream sample count acknowledgment")
    summary["event_trace_sha256"] = event_trace.hexdigest()
    summary["slots_observed"] = sorted(seen_slots)
    summary["slots_check_fec_bursts"] = sorted(valid_slots)
    summary["color_codes"] = sorted(colors)
    summary["source_ids"] = sorted(sources)
    summary["talkgroups"] = sorted(talkgroups)
    for field, observed in (("color_code", colors), ("source_id", sources), ("group_id", talkgroups)):
        if field in upstream:
            value = upstream[field]
            if type(value) is not int:
                summary["upstream_checks"][field] = "not_comparable"
            else:
                summary["upstream_checks"][field] = "observed" if value in observed else "not_observed"
    if "min_data_sync" in upstream:
        mode = "direct_" if "group_id" in upstream and "source_id" in upstream else ""
        total = sum(v for k, v in summary["sync"].items() if k.endswith("_data") and (not mode or k.startswith(mode)))
        summary["upstream_checks"]["min_data_sync"] = "met" if total >= upstream["min_data_sync"] else "not_met"
    if "min_voice_sync" in upstream:
        mode = "direct_" if "group_id" in upstream and "source_id" in upstream else ""
        total = sum(v for k, v in summary["sync"].items() if k.endswith("_voice") and (not mode or k.startswith(mode)))
        summary["upstream_checks"]["min_voice_sync"] = "met" if total >= upstream["min_voice_sync"] else "not_met"
    for key in upstream:
        summary["upstream_checks"].setdefault(key, "not_assessed")
    summary["status"] = ("negative_false_identity" if negative and summary["false_identity_detections"] else
                          "negative_no_valid_identity" if negative else
                          "decoder_events_observed" if summary["bursts"] or summary["sync_total"] else
                          "no_decoder_events")
    return summary


def _stream_verified(path: Path, row: dict, sink, chunk_samples: int) -> tuple[int, str]:
    """Stream little-endian interleaved complex IQ as CF32. Rehash same stream.

    No resampling or new IQ files. Chunk memory bounded by chunk_samples.
    """
    frame_size = FORMAT_BYTES[row["format"]]
    gitsha = hashlib.sha1(f"blob {row['bytes']}\0".encode("ascii"))
    sha = hashlib.sha256()
    count = 0
    with path.open("rb") as source:
        signature = source.read(12)
        if signature.startswith((b"RIFF", b"RIFX", b"RF64", b"FORM")):
            raise AcceptanceError("container detected, expected headerless raw IQ")
        source.seek(0)
        while raw := source.read(frame_size * chunk_samples):
            if len(raw) % frame_size:
                raise AcceptanceError("partial complex sample")
            gitsha.update(raw)
            sha.update(raw)
            count += len(raw) // frame_size
            if row["format"] == "cf32-le":
                # Iteration also rejects all nonfinite CF32 (NaNs can falsely lock).
                for i, q in struct.iter_unpack("<ff", raw):
                    if not (math.isfinite(i) and math.isfinite(q)):
                        raise AcceptanceError("non-finite CF32 IQ")
                sink.write(raw)
            else:
                buffer = bytearray()
                for i, q in struct.iter_unpack("<hh", raw):
                    buffer.extend(struct.pack("<ff", i / 32768.0, q / 32768.0))
                sink.write(buffer)
    if count != row["bytes"] // frame_size or gitsha.hexdigest() != row["git_blob_sha1"] or (
            row.get("sha256") is not None and sha.hexdigest() != row["sha256"]):
        raise AcceptanceError("IQ changed while streaming; event evidence discarded")
    return count, sha.hexdigest()


def run_bridge(root: Path, row: dict, command: list[str], chunk_samples: int = 4096,
               timeout_s: int = 30) -> dict:
    if not command or not all(isinstance(c, str) and c for c in command):
        raise AcceptanceError("bridge command is required")
    if not (1 <= chunk_samples <= 65536) or not (1 <= timeout_s <= 600):
        raise AcceptanceError("invalid stream limits")
    path, integrity = verify(root, row)
    result: dict[str, Any] = {"id": row["id"], "integrity": integrity,
                               "upstream_expected_unverified": row.get("upstream_expected", {}),
                               "bridge_command": command, "bridge_protocol": "orcsdr-dmr-d1-bridge-event-v1"}
    if not integrity["verified"]:
        raise AcceptanceError(f"{row['id']}: integrity not verified: {integrity.get('error')}")
    expected_samples = row["bytes"] // FORMAT_BYTES[row["format"]]
    args = command + ["--input-format", "cf32-le", "--sample-rate-hz", "48000", "--fixture-id", row["id"]]
    with tempfile.TemporaryFile(mode="w+b") as stdout, tempfile.TemporaryFile(mode="w+b") as stderr:
        try:
            proc = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=stdout, stderr=stderr)
        except OSError as exc:
            raise AcceptanceError(f"cannot start decoder bridge: {exc}") from exc
        worker: dict[str, Any] = {}
        failures: list[Exception] = []

        def write_input() -> None:
            try:
                assert proc.stdin is not None
                with proc.stdin:
                    worker["result"] = _stream_verified(path, row, proc.stdin, chunk_samples)
            except Exception as exc:
                failures.append(exc)

        thread = threading.Thread(target=write_input, name="dmr-iq-feed", daemon=True)
        thread.start()
        try:
            try:
                exit_code = proc.wait(timeout=timeout_s)
            except subprocess.TimeoutExpired as exc:
                proc.kill()
                proc.wait()
                thread.join(timeout=3)
                raise AcceptanceError("decoder bridge timeout") from exc
            thread.join(timeout=3)
            if thread.is_alive():
                raise AcceptanceError("bridge IQ feed did not terminate")
            if failures:
                err = failures[0]
                if isinstance(err, AcceptanceError):
                    raise err
                raise AcceptanceError(f"decoder bridge stream error: {err}") from err
            if exit_code:
                stderr.seek(0)
                msg = stderr.read(2048).decode("utf-8", "replace")
                raise AcceptanceError(f"decoder bridge failed ({exit_code}): {msg}")
            count, digest = worker["result"]
            if count != expected_samples:
                raise AcceptanceError("stream sample count mismatch")
            stdout.seek(0, os.SEEK_END)
            if stdout.tell() > 8 * 1024 * 1024:
                raise AcceptanceError("decoder event output too large")
            stdout.seek(0)
            try:
                lines = stdout.read().decode("utf-8").splitlines()
            except UnicodeError as exc:
                raise AcceptanceError("decoder emitted non-UTF8 events") from exc
            negative = row.get("protocol") == "UNKNOWN" and row.get("origin") == "deterministic_synthetic"
            result["iq_samples_streamed"] = count
            result["sha256_streamed"] = digest
            result["metrics"] = aggregate_events(lines, count, negative=negative,
                                                 upstream=row.get("upstream_expected", {}))
            return result
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
            thread.join(timeout=3)



def choose_ids(by_id: dict, user_ids: list[str] | None, controls: bool, tier3: bool) -> list[str]:
    ids = user_ids if user_ids else list(DIRECT_IDS) + (list(TIER3_IDS) if tier3 else [])
    if controls:
        ids += [fid for fid in NEGATIVE_IDS if fid not in ids]
    out = list(dict.fromkeys(ids))
    for fid in out:
        if fid not in by_id:
            raise AcceptanceError(f"fixture not found in manifest: {fid}")
        if fid not in DIRECT_IDS + NEGATIVE_IDS + TIER3_IDS:
            raise AcceptanceError(f"out of DMR acceptance scope: {fid}")
    return out


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("list", "verify", "run"))
    parser.add_argument("--manifest", type=Path, default=MANIFEST_DEFAULT)
    parser.add_argument("--fixtures-root", type=Path)
    parser.add_argument("--id", action="append", dest="ids")
    parser.add_argument("--include-tier3", action="store_true")
    parser.add_argument("--controls", action="store_true", help="include two synthetic no-DMR controls")
    parser.add_argument("--bridge-command", help="host executable and arguments, without shell expansion")
    parser.add_argument("--chunk-samples", type=int, default=4096)
    parser.add_argument("--timeout", type=int, default=30)
    parser.add_argument("--report", type=Path, help="write JSON report; never write raw IQ")
    args = parser.parse_args(argv)
    try:
        meta, index = load_metadata(args.manifest)
        ids = choose_ids(index, args.ids, args.controls or (args.command == "run" and not args.ids), args.include_tier3)
        report = {"schema": "orcsdr-dmr-acceptance-report-v1", "source_manifest": str(args.manifest),
                  "source_repository": meta["fixture_repository"],
                  "source_branch": meta["fixture_branch"],
                  "source_commit": meta["source_commit"], "run_kind": args.command,
                  "fixtures": []}
        failures = 0
        for fid in ids:
            row = index[fid]
            if args.command == "list":
                report["fixtures"].append({"id": fid, "format": row["format"],
                                            "sample_rate_hz": row["sample_rate_hz"],
                                            "bytes": row["bytes"], "upstream_expected_unverified": row.get("upstream_expected", {})})
            else:
                if args.fixtures_root is None:
                    raise AcceptanceError("--fixtures-root is required to verify or run")
                if args.command == "verify":
                    _, item = verify(args.fixtures_root, row)
                    report["fixtures"].append(item)
                    failures += not item["verified"]
                else:
                    if args.bridge_command is None:
                        raise AcceptanceError("--bridge-command is required for real decoder evidence")
                    result = run_bridge(args.fixtures_root, row, shlex.split(args.bridge_command),
                                        args.chunk_samples, args.timeout)
                    report["fixtures"].append(result)
                    failures += result["metrics"]["false_identity_detections"] > 0
                    failures += result["metrics"]["unverified_identity_claims"] > 0
        report["outcome"] = "failed" if failures else "verified" if args.command == "verify" else \
            "bridge_evidence_collected" if args.command == "run" else "metadata_only"
        report["not_claimed"] = ("D1 decode acceptance requires independently audited bridge provenance, "
                                 "direct-mode IQ replay and full protocol checks; upstream assertions are not measurements")
        output = json.dumps(report, sort_keys=True, indent=2) + "\n"
        if args.report:
            if args.report.resolve().is_relative_to(Path(__file__).resolve().parents[2]):
                raise AcceptanceError("report destination must be outside OrcSDR repository")
            args.report.parent.mkdir(parents=True, exist_ok=True)
            with args.report.open("x", encoding="utf-8") as dest:
                dest.write(output)
        print(output, end="")
        return 2 if failures else 0
    except (AcceptanceError, OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"dmr acceptance error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
