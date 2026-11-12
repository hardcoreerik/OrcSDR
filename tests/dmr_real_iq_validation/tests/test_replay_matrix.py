"""Offline tests for public acquisition integrity and evidence comparison.

Never use fake IQ as a real-air acceptance result.
"""
from __future__ import annotations

import hashlib
import importlib.util
import io
import os
from pathlib import Path
from pathlib import PurePosixPath
import sys
import tempfile
import types
import unittest

# The module normally imports OrcSDR's actual acceptance harness. For these
# isolated orchestration unit tests we stub ONLY that external seam.
SOURCE = Path(__file__).resolve().parents[1] / "replay_matrix.py"
acceptance = types.ModuleType("acceptance")
acceptance.safe_relative = lambda name: PurePosixPath(name)
def fake_verify(root, row):
    path = root / row["path"]
    if not path.is_file():
        return path, {"id": row["id"], "verified": False, "error": "missing"}
    length = path.stat().st_size
    h = hashlib.sha1(f"blob {length}\0".encode("ascii") + path.read_bytes()).hexdigest()
    return path, {"id": row["id"], "verified": h == row["git_blob_sha1"] and length == row["bytes"]}
acceptance.verify = fake_verify
old = sys.modules.get("acceptance")
sys.modules["acceptance"] = acceptance
try:
    spec = importlib.util.spec_from_file_location("orcsdr_matrix_tested", SOURCE)
    matrix = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(matrix)
finally:
    if old is None:
        del sys.modules["acceptance"]
    else:
        sys.modules["acceptance"] = old


class Response(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


class TestMatrix(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.payload = b"captured bytes for integrity unit test"
        self.row = {
            "id": matrix.PUBLIC_IDS[0],
            "path": "DMR/dmr-directmode-446500-keyup-48k.cs16",
            "bytes": len(self.payload),
            "git_blob_sha1": hashlib.sha1(
                f"blob {len(self.payload)}\0".encode("ascii") + self.payload).hexdigest(),
        }

    def tearDown(self):
        self.tmp.cleanup()

    def test_blob_sha_is_not_raw_sha1(self):
        path = self.root / "sample"
        path.write_bytes(self.payload)
        length, git, raw256 = matrix.git_blob_hash(path)
        self.assertEqual(length, len(self.payload))
        self.assertEqual(git, self.row["git_blob_sha1"])
        self.assertEqual(raw256, hashlib.sha256(self.payload).hexdigest())
        self.assertNotEqual(git, hashlib.sha1(self.payload).hexdigest())

    def test_fetch_then_verify_without_network(self):
        calls = []
        def opener(url, timeout):
            calls.append((url, timeout))
            return Response(self.payload)
        result = matrix.download_one(self.root, self.row, opener=opener)
        self.assertTrue(result["verified"])
        self.assertEqual(result["fetch"], "downloaded")
        self.assertIn(matrix.SOURCE_COMMIT, calls[0][0])
        self.assertEqual((self.root / self.row["path"]).read_bytes(), self.payload)
        # Source already verified; never refetch or overwrite.
        again = matrix.download_one(self.root, self.row, opener=lambda *_a, **_kw: self.fail("redownload"))
        self.assertEqual(again["fetch"], "already_verified")

    def test_reject_corruption_without_leaking_partial_files(self):
        with self.assertRaises(ValueError):
            matrix.download_one(self.root, self.row, opener=lambda *_a, **_kw: Response(self.payload[:-1] + b"?"))
        self.assertFalse((self.root / self.row["path"]).exists())
        self.assertFalse(list((self.root / "DMR").glob("*.part")))

    def test_reject_oversize_without_adding_file(self):
        with self.assertRaisesRegex(ValueError, "larger"):
            matrix.download_one(self.root, self.row, opener=lambda *_a, **_kw: Response(self.payload + b"x"))
        self.assertFalse((self.root / self.row["path"]).exists())

    def test_reject_unpinned_download_id(self):
        self.row["id"] = "auto-negative-noise"
        with self.assertRaises(ValueError):
            matrix.download_one(self.root, self.row)

    def test_refuse_inside_repo(self):
        with self.assertRaises(ValueError):
            matrix.external_path(matrix.REPO / "data.bin")
        with self.assertRaises(ValueError):
            matrix.external_path(matrix.REPO)

    def test_fingerprint_consistency(self):
        base = [{"status": "EXECUTED", "chunk_samples": x,
                 "metrics": {"event_trace_sha256": "a" * 64}} for x in (1, 7, 257, 1024, 4096)]
        self.assertEqual(matrix.compare_matrix(base)["status"], "PASS")
        base[-1]["metrics"]["event_trace_sha256"] = "b" * 64
        self.assertEqual(matrix.compare_matrix(base)["status"], "FAIL")
        base[-1]["status"] = "FAILED"
        self.assertEqual(matrix.compare_matrix(base)["status"], "NOT_ASSESSED")

    def test_direct_keyup_does_not_promote_sync_into_identity(self):
        m = {"sync_total": 10, "check_and_fec_valid": 0, "source_ids": [],
             "talkgroups": [], "color_codes": [], "unverified_identity_claims": 0}
        status = matrix.assess(self.row, m)
        self.assertEqual(status["status"], "FAIL")
        self.assertIn("no FEC/integrity validated burst", status["issues"])

    def test_controls_fail_on_false_identity(self):
        self.row["id"] = "auto-negative-noise"
        self.assertEqual(matrix.assess(self.row, {"false_identity_detections": 1})["status"], "FAIL")

    def test_unmeasured_chunk_hash_never_passes(self):
        data = [{"status": "EXECUTED", "chunk_samples": 1, "metrics": {}}]
        self.assertEqual(matrix.compare_matrix(data)["status"], "NOT_ASSESSED")


if __name__ == "__main__":
    unittest.main()
