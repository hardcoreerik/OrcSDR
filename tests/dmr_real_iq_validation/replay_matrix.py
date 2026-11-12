#!/usr/bin/env python3
"""Public, pinned DMR IQ fetch + orchestration of OrcSDR's real acceptance bridge.

No decoder is implemented here. IQ/results stay outside the OrcSDR checkout.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import tempfile
import time
from urllib.request import urlopen

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
ACCEPTANCE = REPO / "tests" / "dmr_replay_acceptance"
sys.path.insert(0, str(ACCEPTANCE))
import acceptance  # noqa: E402  -- import existing parser/bridge, not another decoder

SOURCE_COMMIT = "f06bfff493f47fdeb69807c8b08b2b32a253839f"
SOURCE_BASE = ("https://raw.githubusercontent.com/MattCheramie/GopherTrunk/"
               + SOURCE_COMMIT + "/cmd/gophertrunk/testdata/")
PUBLIC_IDS = (
    "dmr-directmode-keyup",
    "dmr-directmode-second-ptt",
    "dmr-tier3-control",
    "dmr-tier3-voice-terminator",
)
CONTROLS = ("auto-negative-noise", "auto-negative-am")
CHUNK_SIZES = (1, 7, 257, 1024, 4096)
SCHEMA = "orcsdr-dmr-public-real-iq-matrix-v1"


def external_path(path: Path) -> Path:
    """Refuse to write any IQ, logs or generated JSON inside the repository."""
    target = path.expanduser().resolve()
    if target == REPO or target.is_relative_to(REPO):
        raise ValueError(f"destination must be outside the OrcSDR checkout: {target}")
    return target


def git_blob_hash(path: Path) -> tuple[int, str, str]:
    size = path.stat().st_size
    git = hashlib.sha1(f"blob {size}\0".encode("ascii"))
    sha256 = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(65536):
            git.update(block)
            sha256.update(block)
    return size, git.hexdigest(), sha256.hexdigest()


def verified_row(root: Path, row: dict) -> dict:
    """Delegate pre-replay pin verification to the existing acceptance implementation."""
    try:
        _, evidence = acceptance.verify(root, row)
        return evidence
    except (OSError, ValueError) as exc:
        return {"id": row["id"], "verified": False, "error": str(exc)}


def download_one(root: Path, row: dict, *, opener=urlopen) -> dict:
    """Stream a pinned public GitHub object to an atomic, external file."""
    if row["id"] not in PUBLIC_IDS:
        raise ValueError("not an allowlisted public real-air DMR capture")
    relative = acceptance.safe_relative(row["path"])
    if len(relative.parts) != 2 or relative.parts[0] != "DMR":
        raise ValueError("unexpected capture directory")
    dest = external_path(root / Path(*relative.parts))
    dest.parent.mkdir(parents=True, exist_ok=True)
    existing = verified_row(root, row)
    if existing.get("verified"):
        return {**existing, "fetch": "already_verified"}
    url = SOURCE_BASE + relative.name
    fd, temp_name = tempfile.mkstemp(prefix=".dmr-download-", suffix=".part", dir=dest.parent)
    try:
        with os.fdopen(fd, "wb") as output, opener(url, timeout=40) as source:
            size = 0
            while block := source.read(65536):
                size += len(block)
                if size > row["bytes"]:
                    raise ValueError("download larger than manifest pin")
                output.write(block)
        actual_size, actual_blob, actual_sha256 = git_blob_hash(Path(temp_name))
        if actual_size != row["bytes"] or actual_blob != row["git_blob_sha1"]:
            raise ValueError("download does not match pinned size/Git blob SHA-1")
        if row.get("sha256") and actual_sha256 != row["sha256"]:
            raise ValueError("download does not match pinned SHA-256")
        os.replace(temp_name, dest)
        return {**verified_row(root, row), "fetch": "downloaded", "source_url": url}
    finally:
        if os.path.exists(temp_name):
            os.unlink(temp_name)


def compare_matrix(entries: list[dict]) -> dict:
    """Compare complete ordered JSONL event fingerprints, not counts only."""
    verified = [x for x in entries if x.get("status") == "EXECUTED"]
    if len(verified) != len(entries):
        return {"status": "NOT_ASSESSED", "reason": "one or more replay attempts did not execute"}
    hashes = [x["metrics"].get("event_trace_sha256") for x in entries]
    if any(not isinstance(x, str) or len(x) != 64 for x in hashes):
        return {"status": "NOT_ASSESSED", "reason": "ordered event fingerprint missing"}
    return {"status": "PASS" if len(set(hashes)) == 1 else "FAIL",
            "event_trace_sha256_by_chunk": {str(x["chunk_samples"]): x["metrics"]["event_trace_sha256"] for x in entries}}


def assess(row: dict, metrics: dict) -> dict:
    """Do not promote sync or a slot guess to an integrity-validated identity."""
    issues = []
    review = []
    if metrics.get("unverified_identity_claims", 0):
        issues.append("identity reported without protocol integrity evidence")
    if row["id"] in CONTROLS:
        if metrics.get("false_identity_detections", 0) or metrics.get("source_ids") or metrics.get("talkgroups"):
            issues.append("false trusted identity in non-DMR control")
    elif row["id"].startswith("dmr-directmode"):
        if metrics.get("sync_total", 0) == 0:
            issues.append("no DMR sync on real direct-mode capture")
        if row["id"] == PUBLIC_IDS[0]:
            if not metrics.get("check_and_fec_valid", 0):
                issues.append("no FEC/integrity validated burst")
            for pin, key in ((3024109, "source_ids"), (99, "talkgroups"), (1, "color_codes")):
                if pin not in metrics.get(key, []):
                    review.append(f"upstream asserted {key}={pin} was not observed")
    else:
        review.append("Tier III exploratory only: D1 Tier II cannot establish complete Tier III parity")
    return {"status": "FAIL" if issues else "REVIEW" if review else "PASS",
            "issues": issues, "upstream_comparison_only": review}


def save_json(path: Path, data: dict) -> None:
    path = external_path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def run_matrix(root: Path, results: Path, rows: dict, bridge: Path, ids: tuple[str, ...],
               chunks: tuple[int, ...], timeout: int) -> dict:
    results = external_path(results)
    results.mkdir(parents=True, exist_ok=True)
    report = {"schema": SCHEMA, "status": "BLOCKED", "upstream_is_not_decoder_output": True,
              "source_commit": SOURCE_COMMIT, "platform": platform.platform(),
              "python": sys.version.split()[0], "bridge": str(bridge), "fixtures": []}
    try:
        report["tested_git_sha"] = subprocess.check_output(
            ["git", "-C", str(REPO), "rev-parse", "HEAD"], text=True, stderr=subprocess.DEVNULL).strip()
    except (OSError, subprocess.CalledProcessError):
        report["tested_git_sha"] = "UNKNOWN"
    bridge_ok = bridge.is_file() and os.access(bridge, os.X_OK)
    for fid in ids:
        row = rows[fid]
        item = {"id": fid, "upstream_expected_not_orcsdr_observed": row.get("upstream_expected", {}),
                "source": verified_row(root, row), "runs": []}
        if not item["source"].get("verified") or not bridge_ok:
            item["status"] = "BLOCKED"
            item["reason"] = "IQ missing/hash-mismatched" if not item["source"].get("verified") else "native D1 bridge missing"
            report["fixtures"].append(item)
            continue
        for chunk in chunks:
            started = time.perf_counter()
            entry = {"chunk_samples": chunk, "status": "NOT_RUN"}
            try:
                run = acceptance.run_bridge(root, row, [str(bridge)], chunk_samples=chunk, timeout_s=timeout)
                entry.update(status="EXECUTED", metrics=run["metrics"],
                             input_sha256=run["sha256_streamed"],
                             sample_count=run["iq_samples_streamed"],
                             acceptance=assess(row, run["metrics"]))
            except (OSError, ValueError) as exc:
                entry.update(status="FAILED", error=str(exc))
            entry["wall_seconds"] = round(time.perf_counter() - started, 6)
            # Do not infer peak process RSS from Python's aggregate child usage.
            entry["peak_bridge_rss_bytes"] = "NOT MEASURED"
            stem = f"{fid}-chunk-{chunk}"
            save_json(results / f"{stem}.json", entry)
            (results / f"{stem}.log").write_text(
                f"fixture={fid}\nchunk_samples={chunk}\nbridge={bridge}\n"
                f"status={entry['status']}\nwall_seconds={entry['wall_seconds']}\n"
                + (f"error={entry['error']}\n" if "error" in entry else ""), encoding="utf-8")
            item["runs"].append(entry)
        item["chunk_invariance"] = compare_matrix(item["runs"])
        item["status"] = ("FAILED" if any(x["status"] == "FAILED" or x.get("acceptance", {}).get("status") == "FAIL"
                                          for x in item["runs"]) or item["chunk_invariance"]["status"] == "FAIL"
                          else "REVIEW" if item["chunk_invariance"]["status"] != "PASS" or
                               any(x.get("acceptance", {}).get("status") == "REVIEW" for x in item["runs"])
                          else "PASS")
        report["fixtures"].append(item)
    states = {i["status"] for i in report["fixtures"]}
    report["status"] = "FAILED" if "FAILED" in states else "BLOCKED" if "BLOCKED" in states else "REVIEW" if "REVIEW" in states else "PASS"
    save_json(results / "real_iq_matrix_summary.json", report)
    return report


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("command", choices=("fetch", "verify", "run", "all"))
    p.add_argument("--fixtures-root", type=Path, default=Path(tempfile.gettempdir()) / "orcsdr-dmr-real-iq")
    p.add_argument("--results-root", type=Path, default=Path(tempfile.gettempdir()) / "orcsdr-dmr-real-iq-results")
    p.add_argument("--bridge", type=Path, default=REPO / "build" / "dmr-d1" / "dmr_d1_replay_bridge")
    p.add_argument("--include-controls", action="store_true")
    p.add_argument("--chunk-sizes", type=int, nargs="+", default=CHUNK_SIZES)
    p.add_argument("--timeout", type=int, default=60)
    a = p.parse_args(argv)
    try:
        root, results = external_path(a.fixtures_root), external_path(a.results_root)
        if len(set(a.chunk_sizes)) != len(a.chunk_sizes) or any(not 1 <= x <= 65536 for x in a.chunk_sizes):
            raise ValueError("chunk sizes must be unique, between 1 and 65536")
        if not 1 <= a.timeout <= 600:
            raise ValueError("timeout must be 1..600 seconds")
        _manifest, rows = acceptance.load_metadata(acceptance.MANIFEST_DEFAULT)
        ids = PUBLIC_IDS + (CONTROLS if a.include_controls else ())
        results.mkdir(parents=True, exist_ok=True)
        if a.command in ("fetch", "all"):
            fetched = []
            for fid in PUBLIC_IDS:
                try:
                    fetched.append(download_one(root, rows[fid]))
                except (OSError, ValueError) as exc:
                    fetched.append({"id": fid, "verified": False, "error": str(exc)})
            save_json(results / "public_download_verification.json", {"source_commit": SOURCE_COMMIT, "files": fetched})
            if a.command == "fetch":
                print(json.dumps(fetched, indent=2))
                return 0 if all(x.get("verified") for x in fetched) else 3
        if a.command == "verify":
            entries = [verified_row(root, rows[fid]) for fid in ids]
            save_json(results / "local_byte_verification.json", {"files": entries})
            print(json.dumps(entries, indent=2))
            return 0 if all(x.get("verified") for x in entries) else 3
        report = run_matrix(root, results, rows, a.bridge.expanduser().resolve(), ids,
                            tuple(a.chunk_sizes), a.timeout)
        print(json.dumps({"status": report["status"], "summary": str(results / "real_iq_matrix_summary.json")}, indent=2))
        return {"PASS": 0, "REVIEW": 2, "FAILED": 2, "BLOCKED": 3}[report["status"]]
    except (OSError, ValueError) as exc:
        print(f"DMR matrix error: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main())
