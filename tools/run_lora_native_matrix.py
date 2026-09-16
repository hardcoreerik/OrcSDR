#!/usr/bin/env python3
"""Run a resumable two-capture native LoRa weak-signal matrix."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

import replay_lora_orciq


DEFAULT_CAPTURE_IDS = ("orciq-0f4812e88b88bd75", "orciq-d9f355473ea17c15")


def _matrix_key(capture_id, seed, snr_db, target_rms):
    return capture_id, seed, snr_db, target_rms


def _build_row(capture, snr_db, seed, report):
    done = replay_lora_orciq._parse_fields(report["done"])
    profile = replay_lora_orciq._parse_fields(report["profile"])
    expected = capture.get("expected") or {}
    expected_packet_id = expected.get("packet_id")
    host_exact = bool(report.get("host_decode")) and report.get("host_packet_id") == expected_packet_id
    packets = report.get("packets", [])
    final = next((packet for packet in packets
                  if packet.get("packet_id") == expected_packet_id), None)
    native_exact = final is not None and int(done.get("crc_ok", 0)) > 0
    difference = report.get("symbol_difference") or {}
    coverage = report.get("alternate_coverage") or {}
    error_indices = difference.get("indices", [])
    covered_indices = coverage.get("covered_indices", [])
    alternate_line = next((line for line in report.get("traces", [])
                           if line.startswith("RTL_LORA_NATIVE_ALTERNATES ")), None)
    alternates = replay_lora_orciq._parse_symbol_alternates(alternate_line)
    symbol_line = next((line for line in report.get("traces", [])
                        if line.startswith("RTL_LORA_NATIVE_SYMBOLS ")), None)
    native_symbols = ([int(value) for value in symbol_line.split("values=", 1)[1].split(",")]
                      if symbol_line else [])
    bins = 1 << int(capture.get("spreading_factor", 11))
    candidate_changed_indices = [
        index for index, alternate in alternates.items()
        if index < len(native_symbols) and
        (alternate["symbol"] + 1) % bins == native_symbols[index]
    ]
    recovered = [
        {"index": index, "symbol": alternates[index]["symbol"],
         "ratio": alternates[index]["ratio"]}
        for index in covered_indices if index in alternates
    ]
    if not host_exact:
        classification = "HOST_FAIL"
    elif native_exact:
        classification = "PASS"
    elif int(done.get("preambles", 0)) == 0:
        classification = "C"
    elif int(done.get("header_failures", 0)) > 0 and int(done.get("crc_failures", 0)) == 0:
        classification = "D"
    elif error_indices and len(covered_indices) == len(error_indices):
        classification = "A"
    elif error_indices:
        classification = "B"
    else:
        classification = "E"
    header_state = ("not_reached" if int(done.get("preambles", 0)) == 0 else
                    "fail" if int(done.get("header_failures", 0)) > 0 else "pass")
    crc_state = ("pass" if int(done.get("crc_ok", 0)) > 0 else
                 "fail" if int(done.get("crc_failures", 0)) > 0 else "not_reached")
    return {
        "capture_id": capture["capture_id"],
        "capture_path": capture["path"],
        "antenna": capture.get("setup", {}).get("antenna"),
        "seed": seed,
        "requested_impairment_db": snr_db,
        "target_rms": (report.get("impairment") or {}).get("target_rms"),
        "host_exact": host_exact,
        "host_packet_id": report.get("host_packet_id"),
        "native_exact": native_exact,
        "classification": classification,
        "header_state": header_state,
        "payload_crc_state": crc_state,
        "primary_symbol_errors": int(difference.get("different", 0)),
        "primary_error_indices": error_indices,
        "recovered": recovered,
        "candidate_changed_indices": candidate_changed_indices,
        "candidate_collateral_indices": [index for index in candidate_changed_indices
                                         if index not in error_indices],
        "recovery_attempted": int(profile.get("recovery_attempted", 0)),
        "recovery_symbols_considered": int(profile.get("recovery_symbols_considered", 0)),
        "recovery_candidates_tested": int(profile.get("recovery_candidates_tested", 0)),
        "recovery_success": int(profile.get("recovery_success", 0)),
        "recovery_exhausted": bool(int(profile.get("recovery_exhausted", 0))),
        "cfo_hypotheses": int(profile.get("cfo_hypotheses", 0)),
        "clock_hypotheses": int(profile.get("clock_hypotheses", 0)),
        "timing_offsets": int(profile.get("timing_offsets", 0)),
        "fft_calls": int(profile.get("fft_calls", 0)),
        "runtime_ms": int(done.get("elapsed_ms", 0)),
        "expected_packet_id": expected_packet_id,
        "final_packet_id": final.get("packet_id") if final else None,
        "final_sender": final.get("sender") if final else None,
        "final_destination": final.get("destination") if final else None,
        "memory": report.get("memory", []),
    }


def _save(path, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps({"rows": rows}, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def _restore_live(port, pairing_key):
    from help_media import Tab5
    tab = Tab5(port, pairing_key)
    try:
        tab.authenticate()
        tab.send("RTL_LISTEN LORA")
        state = tab.wait(("RTL_CAPTURE_QUEUED", "RTL_CAPTURE_BUSY_OR_UNAVAILABLE"), 15)
        if state.startswith("RTL_CAPTURE_QUEUED"):
            state = tab.wait(("RTL_START", "RTL_ERROR"), 30)
        print(f"MATRIX RESTORE {state}", flush=True)
    finally:
        tab.close()


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path,
                        default=repo / "tools/lora_lab/corpus_manifest.json")
    parser.add_argument("--capture-id", action="append", dest="capture_ids")
    parser.add_argument("--snr", action="append", type=float, dest="snrs")
    parser.add_argument("--seed", action="append", type=int, dest="seeds")
    parser.add_argument("--target-rms", type=float, default=0.02)
    parser.add_argument("--port", default="COM17")
    parser.add_argument("--pairing-key", type=Path,
                        default=repo / ".orclink/ui-doc.key")
    parser.add_argument("--output", type=Path,
                        default=repo / "artifacts/lora_validation/weak_matrix/matrix.json")
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    captures_by_id = {capture["capture_id"]: capture for capture in manifest["captures"]}
    captures = [captures_by_id[value] for value in (args.capture_ids or DEFAULT_CAPTURE_IDS)]
    snrs = args.snrs or [-21.0, -22.0, -23.0]
    seeds = args.seeds or list(range(90500, 90505))
    rows = []
    if args.output.exists():
        rows = json.loads(args.output.read_text(encoding="utf-8")).get("rows", [])
    completed = {_matrix_key(row["capture_id"], row["seed"],
                             row["requested_impairment_db"], row.get("target_rms"))
                 for row in rows}
    reports = args.output.parent / "reports"
    corpus_root = repo / manifest["corpus_root"]
    try:
        for capture in captures:
            for snr_db in snrs:
                for seed in seeds:
                    key = _matrix_key(capture["capture_id"], seed, snr_db, args.target_rms)
                    if key in completed:
                        continue
                    report_path = reports / f"{capture['capture_id']}-s{seed}-{abs(snr_db):g}db.json"
                    command = [
                        sys.executable, str(repo / "tools/replay_lora_orciq.py"),
                        str(corpus_root / capture["path"]), "--port", args.port,
                        "--pairing-key", str(args.pairing_key), "--compare-host",
                        "--awgn-snr", str(snr_db), "--seed", str(seed),
                        "--target-rms", str(args.target_rms), "--report", str(report_path),
                    ]
                    subprocess.run(command, cwd=repo, stdout=subprocess.DEVNULL, check=True)
                    report = json.loads(report_path.read_text(encoding="utf-8"))
                    row = _build_row(capture, snr_db, seed, report)
                    rows.append(row)
                    completed.add(key)
                    _save(args.output, rows)
                    print(
                        f"MATRIX {capture['capture_id']} snr={snr_db:g} seed={seed} "
                        f"host={int(row['host_exact'])} native={int(row['native_exact'])} "
                        f"class={row['classification']} recovery={row['recovery_candidates_tested']} "
                        f"runtime_ms={row['runtime_ms']}", flush=True)
    finally:
        _restore_live(args.port, args.pairing_key)


if __name__ == "__main__":
    main()
