#!/usr/bin/env python3
"""D1-R: reproducible real-IQ acceptance gate, no captured IQ stored or bundled.

Verifies local private fixture bytes independently against the manifest, then
replays them through the existing real D1 bridge via acceptance.run_bridge().
Reports missing IQ as BLOCKED, not as a pass or a measured decoder failure.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
from typing import Any

HERE = Path(__file__).resolve().parent
DEFAULT_MANIFEST = HERE.parent / "digital_voice_replay" / "manifest.json"
IDS = ("dmr-directmode-keyup", "dmr-directmode-second-ptt",
       "auto-negative-noise", "auto-negative-am")
POSITIVE = frozenset(IDS[:2])
NEGATIVE = frozenset(IDS[2:])
SCHEMA = "orcsdr-dmr-d1-real-iq-gate-v1"
EXPECTED_REPOSITORY = "hardcoreerik/REA-Results"
EXPECTED_BRANCH = "chatgpt/digital-voice-rsa-rea-2026-11-01"
HEX40 = re.compile(r"[0-9a-f]{40}\Z")
HEX64 = re.compile(r"[0-9a-f]{64}\Z")


class GateError(Exception):
    pass


def read_manifest(path: Path) -> tuple[dict, dict]:
    doc = json.loads(path.read_text(encoding="utf-8"))
    if (doc.get("schema") != "orcsdr-digital-voice-replay-fixtures-v1" or
            doc.get("fixture_repository") != EXPECTED_REPOSITORY or
            doc.get("fixture_branch") != EXPECTED_BRANCH or
            not isinstance(doc.get("source_commit"), str) or
            not HEX40.fullmatch(doc["source_commit"])):
        raise GateError("wrong manifest schema, repository, branch or source revision")
    rows = {r["id"]: r for r in doc["files"]}
    if len(rows) != len(doc["files"]):
        raise GateError("duplicate manifest fixture IDs")
    for fid in IDS:
        if fid not in rows:
            raise GateError(f"missing pinned manifest entry: {fid}")
        r = rows[fid]
        rel = r.get("path")
        fmt = r.get("format")
        if (not isinstance(rel, str) or not rel or rel.startswith("/") or
                "\\" in rel or any(x in ("..", "", ".") for x in rel.split("/")) or
                not rel.endswith(".cs16") or fmt != "cs16-le" or
                r.get("sample_rate_hz") != 48000 or r.get("capture_stage") != "channelized_iq" or
                type(r.get("bytes")) is not int or r["bytes"] <= 0 or r["bytes"] % 4 or
                not isinstance(r.get("git_blob_sha1"), str) or
                not HEX40.fullmatch(r["git_blob_sha1"])):
            raise GateError(f"unusable pinned 48 kS/s CS16 fixture metadata: {fid}")
        pin = r.get("sha256")
        if pin is not None and (not isinstance(pin, str) or not HEX64.fullmatch(pin)):
            raise GateError(f"invalid SHA-256 pin: {fid}")
        if fid in NEGATIVE and (r.get("origin") != "deterministic_synthetic" or
                                r.get("protocol") != "UNKNOWN"):
            raise GateError(f"negative control has changed provenance: {fid}")
    return doc, rows


def verify_local(root: Path, row: dict) -> dict:
    """Never follow a fixture path outside the private fixture root."""
    fid = row["id"]
    base = {"id": fid, "format": row["format"], "sample_rate_hz": 48000,
            "expected_bytes": row["bytes"], "pinned_git_blob_sha1": row["git_blob_sha1"],
            "pinned_sha256_available": row.get("sha256") is not None}
    if not root.is_dir():
        return {**base, "status": "missing", "reason": "private fixture root unavailable"}
    root = root.resolve(strict=True)
    p = (root / row["path"]).resolve(strict=False)
    if not p.is_relative_to(root):
        return {**base, "status": "invalid", "reason": "fixture escapes private root"}
    if not p.is_file():
        return {**base, "status": "missing", "reason": "private fixture unavailable"}
    try:
        size = p.stat().st_size
        if size != row["bytes"]:
            return {**base, "status": "mismatch", "actual_bytes": size, "reason": "length_mismatch"}
        git = hashlib.sha1(f"blob {size}\0".encode("ascii"))
        sha = hashlib.sha256()
        with p.open("rb") as stream:
            while True:
                block = stream.read(1024 * 1024)
                if not block:
                    break
                git.update(block)
                sha.update(block)
    except OSError as exc:
        return {**base, "status": "error", "reason": f"read error ({exc.errno})"}
    result = {**base, "actual_bytes": size, "samples": size // 4,
              "observed_git_blob_sha1": git.hexdigest(), "observed_sha256": sha.hexdigest()}
    if git.hexdigest() != row["git_blob_sha1"] or (row.get("sha256") is not None and
                                                    sha.hexdigest() != row["sha256"]):
        return {**result, "status": "mismatch", "reason": "hash_mismatch"}
    return {**result, "status": "verified"}


def metrics_comparable(metrics: dict) -> dict:
    """All event-derived metrics, including exact ordered JSONL event digest."""
    if not isinstance(metrics, dict) or not HEX64.fullmatch(str(metrics.get("event_trace_sha256", ""))):
        raise GateError("decoder bridge lacks ordered-event SHA-256 trace (upgrade acceptance.py)")
    required = ("sync", "sync_total", "bursts", "fec", "crc", "rs", "embedded_lc",
                "check", "check_and_fec_valid", "color_codes", "slots_observed",
                "slots_check_fec_bursts", "source_ids", "talkgroups",
                "false_identity_detections", "unverified_identity_claims",
                "bridge_samples_consumed", "event_trace_sha256")
    missing = set(required) - set(metrics)
    if missing:
        raise GateError("bridge metrics missing: " + ", ".join(sorted(missing)))
    return {key: metrics[key] for key in required}


def assess_fixture(fid: str, metrics: dict, upstream: dict) -> dict:
    failures = []
    reviews = []
    if metrics["unverified_identity_claims"]:
        failures.append("bridge emitted identities without full integrity evidence")
    if fid in NEGATIVE:
        if metrics["false_identity_detections"] or metrics["source_ids"] or metrics["talkgroups"]:
            failures.append("synthetic non-DMR control produced a trusted DMR identity")
    else:
        if not metrics["sync_total"]:
            failures.append("no OrcSDR synchronization observed in real capture")
        if not metrics["slots_observed"]:
            failures.append("no DMR time slot attributed")
        if fid == IDS[0]:
            if not metrics["check_and_fec_valid"]:
                failures.append("no burst passed both FEC and integrity check")
            if not metrics["source_ids"] or not metrics["talkgroups"]:
                failures.append("no integrity-gated source ID and group/talkgroup recovered")
            if not metrics["color_codes"]:
                failures.append("no checked color-code observation")
            if not metrics["slots_check_fec_bursts"]:
                failures.append("no time-slot attribution on an integrity-checked burst")
        for prop, key in (("source_id", "source_ids"), ("group_id", "talkgroups"),
                          ("color_code", "color_codes")):
            if prop in upstream and upstream[prop] not in metrics[key]:
                reviews.append(f"OrcSDR did not reproduce upstream-only {prop}={upstream[prop]}")
        for prop in ("min_data_sync", "min_voice_sync"):
            if prop in upstream:
                kind = "_data" if prop == "min_data_sync" else "_voice"
                observed = sum(n for key, n in metrics["sync"].items()
                               if key.startswith("direct_") and key.endswith(kind))
                if observed < upstream[prop]:
                    reviews.append(f"OrcSDR direct-mode {prop}={observed} below upstream-only {upstream[prop]}")
    return {"issues": failures, "upstream_disagreements": reviews,
            "status": "FAIL" if failures else "REVIEW" if reviews else "PASS"}


def bridge_identity(binary: Path) -> dict:
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise GateError("native D1 replay executable unavailable or not executable")
    sha = hashlib.sha256()
    with binary.open("rb") as inp:
        for chunk in iter(lambda: inp.read(1024 * 1024), b""):
            sha.update(chunk)
    return {"name": binary.name, "sha256": sha.hexdigest()}


def make_report(manifest: Path, root: Path, bridge: Path | None,
                chunk_sizes: tuple[int, ...], timeout: int, *, api=None) -> dict:
    """All four byte pins must pass before any decoder is invoked."""
    report: dict[str, Any] = {"schema": SCHEMA, "status": "BLOCKED",
                              "measurement_class": "not_observed", "capture_order": list(IDS),
                              "chunk_sizes": list(chunk_sizes), "fixtures": [],
                              "bridge": None, "limitations": [
                                  "Upstream expected values are assertions, not independent RF truth",
                                  "A passing bridge run is not calibrated field sensitivity or hardware validation"]}
    try:
        if len(chunk_sizes) < 2 or len(set(chunk_sizes)) != len(chunk_sizes) or any(
                type(n) is not int or not 1 <= n <= 65536 for n in chunk_sizes):
            raise GateError("provide at least two distinct chunk sizes from 1..65536")
        if not 1 <= timeout <= 600:
            raise GateError("timeout must be 1..600 seconds")
        meta, rows = read_manifest(manifest)
        report["provenance"] = {"repository": meta["fixture_repository"],
                                "branch": meta["fixture_branch"],
                                "source_commit": meta["source_commit"],
                                "manifest_sha256": hashlib.sha256(manifest.read_bytes()).hexdigest()}
        # Preflight every capture before any bridge process starts; no partial
        # successful report if even one private input is missing or changed.
        for fid in IDS:
            status = verify_local(root, rows[fid])
            status["upstream_expected_unverified"] = rows[fid].get("upstream_expected", {})
            report["fixtures"].append(status)
        missing = [x["id"] for x in report["fixtures"] if x["status"] == "missing"]
        bad = [x["id"] for x in report["fixtures"] if x["status"] in ("mismatch", "invalid", "error")]
        if bad:
            report.update(status="FAIL", reason="local fixture integrity failure: " + ", ".join(bad))
            return report
        if missing:
            report.update(status="BLOCKED", reason="private IQ not mounted: " + ", ".join(missing))
            return report
        if bridge is None:
            report.update(status="BLOCKED", reason="--bridge path is required for real D1 replay")
            return report
        try:
            report["bridge"] = bridge_identity(bridge)
        except (GateError, OSError) as exc:
            report.update(status="BLOCKED", reason=str(exc))
            return report
        if api is None:
            # The existing acceptance.py is the only IQ streamer/JSONL parser.
            import acceptance as api
        report["measurement_class"] = "native_bridge_real_iq_partial"
        failure = False
        review = False
        for index, fid in enumerate(IDS):
            row = rows[fid]
            outputs = []
            for chunk in chunk_sizes:
                try:
                    measured = api.run_bridge(root, row, [str(bridge.resolve())],
                                              chunk_samples=chunk, timeout_s=timeout)
                except Exception as exc:
                    report.update(status="FAIL", reason=f"D1 replay failed for {fid} chunk={chunk}: {exc}")
                    return report
                if measured.get("sha256_streamed") != report["fixtures"][index]["observed_sha256"] or (
                        measured.get("iq_samples_streamed") != report["fixtures"][index]["samples"]):
                    report.update(status="FAIL", reason=f"IQ changed during native replay: {fid}")
                    return report
                try:
                    outputs.append(metrics_comparable(measured["metrics"]))
                except (GateError, KeyError, TypeError) as exc:
                    report.update(status="FAIL", reason=f"invalid native bridge evidence for {fid}: {exc}")
                    return report
                if outputs[-1]["bridge_samples_consumed"] != report["fixtures"][index]["samples"]:
                    report.update(status="FAIL", reason=f"decoder did not acknowledge all samples: {fid}")
                    return report
            consistent = all(m == outputs[0] for m in outputs[1:])
            assessment = assess_fixture(fid, outputs[0], row.get("upstream_expected", {}))
            report["fixtures"][index]["orcsdr_observed"] = outputs[0]
            report["fixtures"][index]["chunk_invariant_exact_events"] = consistent
            report["fixtures"][index]["assessment"] = assessment
            if not consistent:
                assessment["issues"].append("ordered decoder event stream changed across IQ chunk sizes")
                assessment["status"] = "FAIL"
            failure |= assessment["status"] == "FAIL"
            review |= assessment["status"] == "REVIEW"
        report.update(status="FAIL" if failure else "REVIEW" if review else "PASS",
                      reason="measured OrcSDR decoder evidence; compare only with explicitly unverified upstream assertions",
                      measurement_class="native_bridge_real_iq")
        return report
    except (GateError, OSError, ValueError, KeyError, TypeError, ImportError, json.JSONDecodeError) as exc:
        report.update(status="BLOCKED", reason=f"preflight unavailable or invalid: {exc}")
        return report


def main(argv: list[str] | None = None, *, api=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--fixtures-root", type=Path, required=True,
                        help="external READ-ONLY private REA fixture root, containing DMR/ and AUTO/")
    parser.add_argument("--bridge", type=Path, help="built dmr_d1_replay_bridge executable")
    parser.add_argument("--chunk-sizes", type=int, nargs="+", default=[257, 4096])
    parser.add_argument("--timeout", type=int, default=45)
    parser.add_argument("--report", type=Path, help="new report JSON (outside OrcSDR checkout)")
    args = parser.parse_args(argv)
    result = make_report(args.manifest, args.fixtures_root, args.bridge,
                         tuple(args.chunk_sizes), args.timeout, api=api)
    raw = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.report:
        target = args.report.resolve()
        repo_root = HERE.parents[1]
        if target.is_relative_to(repo_root) or target.is_relative_to(args.fixtures_root.resolve()):
            print("refusing report inside repository or private IQ root", file=sys.stderr)
            return 2
        try:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            with args.report.open("x", encoding="utf-8") as out:
                out.write(raw)
        except OSError as exc:
            print(f"report write failure: {exc}", file=sys.stderr)
            return 2
    print(raw, end="")
    return {"PASS": 0, "REVIEW": 2, "FAIL": 2, "BLOCKED": 3}[result["status"]]


if __name__ == "__main__":
    raise SystemExit(main())
