#!/usr/bin/env python3
"""Self-contained host tests for D1-R gating, no real IQ and no fake RF claims."""
import hashlib
import contextlib
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import real_iq_gate as gate


def metrics(fid, samples):
    is_negative = fid in gate.NEGATIVE
    keyup = fid == gate.IDS[0]
    data = {
        "sync": {"direct_ts1_data": 0 if is_negative else 9,
                 "direct_ts1_voice": 0 if is_negative else 3},
        "sync_total": 0 if is_negative else 12,
        "bursts": 0 if is_negative else 5,
        "fec": {"pass": 0 if is_negative else 3, "fail": 0, "unknown": 0},
        "check": {"pass": 0 if is_negative else 3, "fail": 0, "unknown": 0},
        "crc": {"pass": 0, "fail": 0, "unknown": 0},
        "rs": {"pass": 0 if is_negative else 3, "fail": 0, "unknown": 0},
        "embedded_lc": {"pass": 0, "fail": 0, "unknown": 0},
        "check_and_fec_valid": 0 if is_negative else 3,
        "color_codes": [] if is_negative else [1],
        "slots_observed": [] if is_negative else [1],
        "slots_check_fec_bursts": [] if is_negative else [1],
        "source_ids": [3024109] if keyup else [],
        "talkgroups": [99] if keyup else [],
        "false_identity_detections": 0,
        "unverified_identity_claims": 0,
        "bridge_samples_consumed": samples,
        "event_trace_sha256": hashlib.sha256((fid + " synthetic bridge stub").encode()).hexdigest(),
    }
    return data


class FakeAPI:
    def __init__(self, tweak=None):
        self.calls = []
        self.tweak = tweak

    def run_bridge(self, root, row, command, chunk_samples, timeout_s):
        self.calls.append((row["id"], chunk_samples))
        data = (root / row["path"]).read_bytes()
        result = {"sha256_streamed": hashlib.sha256(data).hexdigest(),
                  "iq_samples_streamed": len(data) // 4,
                  "metrics": metrics(row["id"], len(data) // 4)}
        if self.tweak:
            self.tweak(result, row["id"], chunk_samples)
        return result


class D1RTests(unittest.TestCase):
    def setUp(self):
        self.t = tempfile.TemporaryDirectory()
        self.addCleanup(self.t.cleanup)
        self.home = Path(self.t.name)
        self.root = self.home / "private"
        self.root.mkdir()
        self.bridge = self.home / "dmr_d1_replay_bridge"
        self.bridge.write_bytes(b"#!/bin/sh\nexit 0\n")
        self.bridge.chmod(0o755)
        self.manifest = self.home / "manifest.json"
        self.rows = []
        for index, fid in enumerate(gate.IDS):
            rel = f"{'DMR' if fid in gate.POSITIVE else 'AUTO'}/{fid}.cs16"
            data = bytes((k + index) % 256 for k in range(64))
            dest = self.root / rel
            dest.parent.mkdir(exist_ok=True)
            dest.write_bytes(data)
            sh = hashlib.sha1(b"blob 64\0" + data).hexdigest()
            row = {"id": fid, "path": rel, "format": "cs16-le", "sample_rate_hz": 48000,
                   "capture_stage": "channelized_iq", "bytes": 64, "git_blob_sha1": sh,
                   "sha256": hashlib.sha256(data).hexdigest() if fid in gate.NEGATIVE else None,
                   "protocol": "DMR direct-mode" if fid in gate.POSITIVE else "UNKNOWN",
                   "origin": "real_air" if fid in gate.POSITIVE else "deterministic_synthetic",
                   "upstream_expected": {"source_id": 3024109, "group_id": 99, "color_code": 1,
                                         "min_data_sync": 8, "min_voice_sync": 2}
                   if index == 0 else {"note": "no validated identity"}}
            self.rows.append(row)
        self.write_manifest()

    def write_manifest(self):
        self.manifest.write_text(json.dumps({"schema": "orcsdr-digital-voice-replay-fixtures-v1",
                 "fixture_repository": gate.EXPECTED_REPOSITORY, "fixture_branch": gate.EXPECTED_BRANCH,
                 "source_commit": "a" * 40, "files": self.rows}), encoding="utf-8")

    def run_gate(self, api=None):
        return gate.make_report(self.manifest, self.root, self.bridge, (257, 4096), 30,
                                api=api if api is not None else FakeAPI())

    def test_nominal_gate_is_operational_only(self):
        api = FakeAPI()
        r = self.run_gate(api)
        self.assertEqual(r["status"], "PASS")
        self.assertEqual(r["measurement_class"], "native_bridge_real_iq")
        self.assertEqual(api.calls, [(fid, size) for fid in gate.IDS for size in (257, 4096)])
        self.assertTrue(all(x["status"] == "verified" for x in r["fixtures"]))
        self.assertTrue(all(x["chunk_invariant_exact_events"] for x in r["fixtures"]))
        self.assertEqual(r["fixtures"][0]["orcsdr_observed"]["source_ids"], [3024109])
        self.assertIn("Upstream expected values", r["limitations"][0])

    def test_missing_first_capture_blocks_before_any_bridge(self):
        (self.root / self.rows[0]["path"]).unlink()
        api = FakeAPI()
        r = self.run_gate(api)
        self.assertEqual(r["status"], "BLOCKED")
        self.assertEqual(api.calls, [])
        self.assertEqual(r["measurement_class"], "not_observed")

    def test_missing_negative_blocks_before_positive_decode(self):
        (self.root / self.rows[-1]["path"]).unlink()
        api = FakeAPI()
        self.assertEqual(self.run_gate(api)["status"], "BLOCKED")
        self.assertEqual(api.calls, [])

    def test_same_size_hash_tamper_fails_closed(self):
        target = self.root / self.rows[0]["path"]
        data = bytearray(target.read_bytes()); data[4] ^= 1; target.write_bytes(data)
        api = FakeAPI()
        r = self.run_gate(api)
        self.assertEqual(r["status"], "FAIL")
        self.assertIn("integrity", r["reason"])
        self.assertEqual(api.calls, [])

    def test_negative_identity_rejected(self):
        def tweak(result, fid, chunk):
            if fid == gate.IDS[-1]:
                result["metrics"]["false_identity_detections"] = 1
                result["metrics"]["source_ids"] = [456]
        r = self.run_gate(FakeAPI(tweak))
        self.assertEqual(r["status"], "FAIL")
        self.assertIn("non-DMR", str(r["fixtures"][-1]["assessment"]["issues"]))

    def test_unverified_identity_rejected(self):
        def tweak(result, fid, chunk):
            if fid == gate.IDS[0]: result["metrics"]["unverified_identity_claims"] = 1
        r = self.run_gate(FakeAPI(tweak))
        self.assertEqual(r["status"], "FAIL")

    def test_differing_chunk_event_trace_fails(self):
        def tweak(result, fid, chunk):
            if fid == gate.IDS[1] and chunk == 4096:
                result["metrics"]["event_trace_sha256"] = "b" * 64
        r = self.run_gate(FakeAPI(tweak))
        self.assertEqual(r["status"], "FAIL")
        self.assertFalse(r["fixtures"][1]["chunk_invariant_exact_events"])

    def test_missing_event_trace_is_not_chunk_consistent(self):
        def tweak(result, fid, chunk):
            if fid == gate.IDS[0]: del result["metrics"]["event_trace_sha256"]
        r = self.run_gate(FakeAPI(tweak))
        self.assertEqual(r["status"], "FAIL")
        self.assertIn("trace", r["reason"])

    def test_upstream_mismatch_is_review_not_fake_false_identity(self):
        def tweak(result, fid, chunk):
            if fid == gate.IDS[0]: result["metrics"]["source_ids"] = [73]
        r = self.run_gate(FakeAPI(tweak))
        self.assertEqual(r["status"], "REVIEW")
        self.assertEqual(r["fixtures"][0]["assessment"]["status"], "REVIEW")
        self.assertEqual(r["fixtures"][0]["orcsdr_observed"]["source_ids"], [73])

    def test_no_sync_is_native_failure_not_success(self):
        def tweak(result, fid, chunk):
            if fid == gate.IDS[1]: result["metrics"]["sync_total"] = 0
        r = self.run_gate(FakeAPI(tweak))
        self.assertEqual(r["status"], "FAIL")

    def test_missing_native_executable_blocked(self):
        self.bridge.unlink()
        r = self.run_gate(FakeAPI())
        self.assertEqual(r["status"], "BLOCKED")

    def test_stream_sha256_must_match_preflight(self):
        def tweak(result, fid, chunk):
            if fid == gate.IDS[0]: result["sha256_streamed"] = "0" * 64
        r = self.run_gate(FakeAPI(tweak))
        self.assertEqual(r["status"], "FAIL")
        self.assertIn("changed", r["reason"])

    def test_stream_sample_count_must_match(self):
        def tweak(result, fid, chunk):
            if fid == gate.IDS[0]: result["metrics"]["bridge_samples_consumed"] -= 1
        r = self.run_gate(FakeAPI(tweak))
        self.assertEqual(r["status"], "FAIL")

    def test_fixture_path_escape_rejected(self):
        self.rows[0]["path"] = "../outside.cs16"
        self.write_manifest()
        api = FakeAPI()
        r = self.run_gate(api)
        self.assertEqual(r["status"], "BLOCKED")
        self.assertFalse(api.calls)

    def test_wrong_source_branch_blocks(self):
        contents = json.loads(self.manifest.read_text())
        contents["fixture_branch"] = "main"
        self.manifest.write_text(json.dumps(contents))
        self.assertEqual(self.run_gate(FakeAPI())["status"], "BLOCKED")

    def test_duplicate_chunk_size_fails_preflight(self):
        r = gate.make_report(self.manifest, self.root, self.bridge, (257, 257), 30,
                             api=FakeAPI())
        self.assertEqual(r["status"], "BLOCKED")

    def test_48k_and_raw_format_strict(self):
        self.rows[0]["format"] = "cf32-le"
        self.write_manifest()
        self.assertEqual(self.run_gate(FakeAPI())["status"], "BLOCKED")

    def test_cli_missing_private_outputs_explicit_blocked_report(self):
        report_file = self.home / "report.json"
        with contextlib.redirect_stdout(io.StringIO()) as printed:
            code = gate.main(["--manifest", str(self.manifest),
                              "--fixtures-root", str(self.home / "not-mounted"),
                              "--bridge", str(self.bridge), "--report", str(report_file)],
                             api=FakeAPI())
        self.assertEqual(code, 3)
        self.assertEqual(json.loads(report_file.read_text())["status"], "BLOCKED")
        self.assertEqual(json.loads(printed.getvalue())["status"], "BLOCKED")
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(gate.main(["--manifest", str(self.manifest),
                "--fixtures-root", str(self.home / "not-mounted"), "--report", str(report_file)],
                api=FakeAPI()), 2)  # no overwrite

    def test_real_acceptance_parser_ordered_event_fingerprint(self):
        try:
            import acceptance
        except ImportError:
            self.skipTest("existing acceptance.py available only in checked-out OrcSDR")
        header = {"schema": "orcsdr-dmr-d1-bridge-event-v1"}
        def run(sync_offset, reverse_keys=False):
            items = [dict(header, type="sync", sample_offset=sync_offset,
                          pattern="direct_ts1_data"),
                     dict(header, type="summary", sample_offset=9, consumed_samples=10)]
            raw = [json.dumps(x, sort_keys=reverse_keys, separators=(",", ":") if reverse_keys else None) for x in items]
            return acceptance.aggregate_events(raw, 10, negative=False, upstream={})
        # Same synchronization count, different absolute sample index.
        first = run(1)
        different_offset = run(2)
        self.assertEqual(first["sync_total"], different_offset["sync_total"])
        self.assertNotEqual(first["event_trace_sha256"], different_offset["event_trace_sha256"])
        self.assertEqual(first["event_trace_sha256"], run(1, reverse_keys=True)["event_trace_sha256"])


if __name__ == "__main__":
    unittest.main()
