#!/usr/bin/env python3
"""Pure stdlib host tests; no RF hardware, network or private IQ required."""
import contextlib
import hashlib
import io
import json
import math
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

import replay


def hash_bytes(data):
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()


class ReplayTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def make_fixture(self, iq, fmt="cs16-le", name="test.cs16", sha256=True):
        content = replay.encode_iq(iq, fmt)
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
        return replay.Fixture("test", name, "SYNTHETIC", fmt, 48000, len(content),
                              hash_bytes(content), hashlib.sha256(content).hexdigest() if sha256 else None,
                              "deterministic_synthetic", {"fixture_repository": "test"}, {})

    def test_pinned_metadata_all_11_with_formats_and_samples(self):
        raw, files = replay.load_manifest()
        self.assertEqual(len(files), 11)
        self.assertEqual(sum(f.bytes for f in files), 4_048_236)
        self.assertEqual(sum(f.format == "cf32-le" for f in files), 4)
        self.assertEqual(sum(f.format == "cs16-le" for f in files), 7)
        self.assertEqual(sum(f.origin == "real_air" for f in files), 9)
        self.assertEqual(sum(f.sha256 is not None for f in files), 2)
        self.assertTrue(all(f.sample_rate_hz == 48000 for f in files))
        self.assertEqual(raw["fixture_repository"], "hardcoreerik/REA-Results")
        self.assertEqual([f.sample_count for f in files if f.id == "dmr-directmode-second-ptt"], [81601])

    def test_manifest_formats_cannot_be_guessed_from_length(self):
        # Both formats can hold 16 bytes; size alone does not identify encoding.
        self.assertEqual(replay.detect_sample_format("foo.iq"), (None, "unknown"))
        self.assertEqual(replay.detect_sample_format("foo.cfile"), ("cf32-le", "filename_hint"))
        self.assertEqual(replay.detect_sample_format("foo.cs16"), ("cs16-le", "filename_hint"))
        self.assertEqual(replay.detect_sample_format("foo.iq", "cs16-le"), ("cs16-le", "manifest"))
        with self.assertRaisesRegex(replay.FixtureError, "conflict"):
            replay.detect_sample_format("foo.cfile", "cs16-le")
        with self.assertRaisesRegex(replay.FixtureError, "unsupported"):
            replay.detect_sample_format("foo.iq", "cu8")

    def test_real_pinned_manifest_rejects_path_escape_duplicate_and_alignment(self):
        raw, _ = replay.load_manifest()
        variants = [
            (lambda m: m["files"][0].update(path="../secret.cfile"), "unsafe"),
            (lambda m: m["files"][0].update(path="/tmp/secret.cfile"), "unsafe"),
            (lambda m: m["files"][0].update(bytes=499201), "unaligned"),
            (lambda m: m["files"][1].update(id=m["files"][0]["id"]), "duplicate"),
            (lambda m: m["files"][1].update(path=m["files"][0]["path"]), "duplicate"),
            (lambda m: m["files"][0].update(format="cs16-le"), "conflict"),
        ]
        for mutate, message in variants:
            with self.subTest(message=message, modifier=str(mutate)):
                data = json.loads(json.dumps(raw))
                mutate(data)
                filename = self.root / "broken.json"
                filename.write_text(json.dumps(data), encoding="utf-8")
                with self.assertRaisesRegex(replay.FixtureError, message):
                    replay.load_manifest(filename)

    def test_verify_git_blob_sha1_and_sha256(self):
        fixture = self.make_fixture([(-1.0, 0.0), (32767/32768, -3/32768)])
        r = replay.verify_fixture(fixture, self.root)
        self.assertEqual(r["status"], "ok")
        self.assertTrue(r["sha256_pinned"])
        self.assertEqual(r["actual_samples"], 2)
        self.assertEqual(r["actual_git_blob_sha1"], fixture.git_blob_sha1)

    def test_verify_missing_or_changed_same_size(self):
        fixture = self.make_fixture([(0.0, 0.0)])
        path = self.root / fixture.path
        path.write_bytes(struct.pack("<hh", 1, 0))
        result = replay.verify_fixture(fixture, self.root)
        self.assertEqual(result["status"], "mismatch")
        self.assertEqual(result["error"], "Git blob SHA-1 mismatch")
        path.unlink()
        self.assertEqual(replay.verify_fixture(fixture, self.root)["status"], "missing")

    def test_verify_wrong_sha256_and_length(self):
        fixture = self.make_fixture([(0.0, 0.0)])
        wrong = replay.Fixture(**{**fixture.__dict__, "sha256": "f" * 64})
        self.assertEqual(replay.verify_fixture(wrong, self.root)["error"], "SHA-256 mismatch")
        (self.root / fixture.path).write_bytes(b"\0\0")
        self.assertEqual(replay.verify_fixture(fixture, self.root)["error"], "byte length mismatch")

    def test_verify_does_not_escape_root_with_symlink(self):
        external = self.root.parent / (self.root.name + "-outside.cs16")
        external.write_bytes(b"\0" * 4)
        self.addCleanup(lambda: external.unlink(missing_ok=True))
        if not hasattr(os, "symlink"):
            self.skipTest("symlinks unavailable")
        (self.root / "outside.cs16").symlink_to(external)
        fixture = replay.Fixture("test", "outside.cs16", "SYNTHETIC", "cs16-le", 48000, 4,
                                 hash_bytes(external.read_bytes()), None, "test", {}, {})
        with self.assertRaisesRegex(replay.FixtureError, "escapes root"):
            replay.verify_fixture(fixture, self.root)

    def test_cs16_conversion_boundaries_and_lossless_roundtrip(self):
        points = [(-1.0, 32767/32768), (-0.5, 0.5), (0.0, -1/32768),
                  (1/32768, 0.0)]
        raw = replay.encode_iq(points, "cs16-le")
        fixture = self.make_fixture(points)
        actual = [sample for chunk in replay.iter_iq(self.root / fixture.path, "cs16-le", 1) for sample in chunk]
        self.assertEqual(actual, points)
        self.assertEqual(replay.encode_iq(actual, "cs16-le"), raw)
        self.assertEqual(replay.encode_iq([(1.7, -3.4)], "cs16-le"), struct.pack("<hh", 32767, -32768))

    def test_cf32_native_float_not_divided_by_32768(self):
        fixture = self.make_fixture([(0.25, -0.75), (2.0, 0.0)], fmt="cf32-le", name="source.cfile")
        chunks = list(replay.iter_iq(self.root / fixture.path, fixture.format, 1))
        self.assertEqual(chunks, [[(0.25, -0.75)], [(2.0, 0.0)]])
        captured = []
        r = replay.replay_iq(fixture, self.root, captured.extend, 1)
        self.assertEqual(r["samples"], 2)
        self.assertEqual(r["source_format"], "cf32-le")
        self.assertFalse(r["decoded"])
        self.assertEqual(r["duration_s"], 2 / 48000)
        self.assertEqual(captured, [(0.25, -0.75), (2.0, 0.0)])
        self.assertAlmostEqual(r["rms_magnitude"], math.sqrt((.25**2 + .75**2 + 4) / 2))

    def test_corrupt_cf32_nan_rejected_even_with_matching_hash(self):
        data = struct.pack("<ff", float("nan"), .5)
        path = self.root / "test.cfile"
        path.write_bytes(data)
        fixture = replay.Fixture("test", "test.cfile", "SYNTHETIC", "cf32-le", 48000,
                                 len(data), hash_bytes(data), hashlib.sha256(data).hexdigest(), "test", {}, {})
        self.assertTrue(replay.verify_fixture(fixture, self.root)["verified"])
        with self.assertRaisesRegex(replay.FixtureError, "non-finite"):
            replay.replay_iq(fixture, self.root, lambda _: None)

    def test_reject_wav_and_truncated_raw(self):
        wav = self.root / "spoof.cs16"
        wav.write_bytes(b"RIFF0000WAVE" + b"\0" * 4)
        with self.assertRaisesRegex(replay.FixtureError, "WAV"):
            list(replay.iter_iq(wav, "cs16-le"))
        wav.write_bytes(b"\0" * 5)
        with self.assertRaisesRegex(replay.FixtureError, "truncated"):
            list(replay.iter_iq(wav, "cs16-le"))

    def test_replay_fails_closed_on_hash_change(self):
        fixture = self.make_fixture([(0, 0), (1/32768, 1/32768)])
        (self.root / fixture.path).write_bytes(b"\0" * fixture.bytes)
        calls = []
        with self.assertRaisesRegex(replay.FixtureError, "mismatch"):
            replay.replay_iq(fixture, self.root, calls.append)
        self.assertFalse(calls)

    def test_convert_cs16_to_cf32_back_without_clobber(self):
        fixture = self.make_fixture([(-1.0, 32767/32768), (0.25, -0.25)])
        target = self.root / "converted.cfile"
        result = replay.convert_fixture(fixture, self.root, target, "cf32-le", chunk_samples=1)
        self.assertEqual(result["output_bytes"], 16)
        self.assertEqual(result["samples"], 2)
        self.assertEqual(list(replay.iter_iq(target, "cf32-le")), [[(-1.0, 32767/32768), (0.25, -0.25)]])
        with self.assertRaises(FileExistsError):
            replay.convert_fixture(fixture, self.root, target, "cf32-le")
        self.assertEqual(target.stat().st_size, 16)

    def test_convert_cf32_to_cs16_clips_and_normalizes(self):
        fixture = self.make_fixture([(2.0, -2.0), (0.5, -0.5)], "cf32-le", "input.cfile")
        target = self.root / "converted.cs16"
        replay.convert_fixture(fixture, self.root, target, "cs16-le")
        self.assertEqual(target.read_bytes(), struct.pack("<hhhh", 32767, -32768, 16384, -16384))

    def test_convert_rejects_wrong_suffix_and_repository_output(self):
        fixture = self.make_fixture([(0.0, 0.0)])
        with self.assertRaisesRegex(replay.FixtureError, "suffix conflicts"):
            replay.convert_fixture(fixture, self.root, self.root / "wrong.cs16", "cf32-le")
        with self.assertRaisesRegex(replay.FixtureError, "inside"):
            replay.convert_fixture(fixture, self.root, replay.HERE / "never.cfile", "cf32-le")
        self.assertFalse((replay.HERE / "never.cfile").exists())

    def test_cli_list_and_optional_verify(self):
        capture = io.StringIO()
        with contextlib.redirect_stdout(capture):
            self.assertEqual(replay.main(["list", "--json"]), 0)
        self.assertEqual(len(json.loads(capture.getvalue())["fixtures"]), 11)
        capture = io.StringIO()
        with contextlib.redirect_stdout(capture):
            self.assertEqual(replay.main(["verify", "--fixtures-root", str(self.root), "--json"]), 2)
        self.assertEqual(len(json.loads(capture.getvalue())["results"]), 11)
        self.assertFalse(json.loads(capture.getvalue())["all_verified"])

    def test_private_real_fixtures_if_explicitly_available(self):
        root = os.environ.get("ORCSDR_DV_FIXTURES_ROOT")
        if not root:
            self.skipTest("Set ORCSDR_DV_FIXTURES_ROOT to private REA-Results/inbox/fixtures/digital-voice")
        _, fixtures = replay.load_manifest()
        failed = [replay.verify_fixture(f, Path(root)) for f in fixtures]
        failures = [f for f in failed if not f["verified"]]
        self.assertFalse(failures, failures)
        for f in fixtures:
            stats = replay.replay_iq(f, Path(root), lambda _: None, 257)
            self.assertEqual(stats["samples"], f.sample_count)
            self.assertFalse(stats["decoded"])


if __name__ == "__main__":
    unittest.main()
