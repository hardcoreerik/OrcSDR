#!/usr/bin/env python3
"""Smoke/negative tests for the actual C++ D1 replay bridge executable.
This test does not claim to decode the private real-air IQ corpus.
"""
import json
import struct
import subprocess
import sys
import unittest


class ReplayCliTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if len(sys.argv) != 2:
            raise SystemExit("test_replay_cli.py expects one executable path")
        cls.executable = sys.argv[1]

    @classmethod
    def args(cls):
        return [cls.executable, "--input-format", "cf32-le",
                "--sample-rate-hz", "48000", "--fixture-id", "test-negative"]

    def invoke(self, data, arguments=None):
        return subprocess.run(arguments if arguments is not None else self.args(),
                              input=data, capture_output=True, timeout=15, check=False)

    def test_exact_full_buffer_no_signal(self):
        # Exactly one 256-IQ sample read block; EOF must not be treated as empty input.
        data = struct.pack("<ff", 0.1, 0.0) * 256
        result = self.invoke(data)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
        rows = [json.loads(x) for x in result.stdout.splitlines()]
        self.assertEqual(rows, [{"schema": "orcsdr-dmr-d1-bridge-event-v1",
                                "type": "summary", "sample_offset": 255,
                                "consumed_samples": 256}])

    def test_two_blocks_partial_final(self):
        data = struct.pack("<ff", 0.25, 0.0) * (256 * 2 + 13)
        result = self.invoke(data)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
        rows = [json.loads(x) for x in result.stdout.splitlines()]
        self.assertEqual(rows[-1]["consumed_samples"], 525)
        self.assertEqual(rows[-1]["sample_offset"], 524)
        self.assertFalse(any(x["type"] == "burst" for x in rows))

    def test_truncated_sample_is_rejected(self):
        result = self.invoke(struct.pack("<ff", 0.1, 0) * 256 + b"BAD")
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn(b'"type":"summary"', result.stdout)

    def test_empty_and_wrong_args_are_rejected(self):
        self.assertNotEqual(self.invoke(b"").returncode, 0)
        wrong = self.args()
        wrong[2] = "cs16-le"
        self.assertNotEqual(self.invoke(struct.pack("<ff", 0.1, 0), wrong).returncode, 0)

    def test_no_false_identity_from_no_signal(self):
        # 4,800 symbols of unmodulated carrier. Never emit an identity from silence.
        data = struct.pack("<ff", 0.1, 0.0) * 48_000
        result = self.invoke(data)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
        rows = [json.loads(x) for x in result.stdout.splitlines()]
        self.assertEqual(rows[-1]["consumed_samples"], 48_000)
        self.assertFalse(any(x.get("identity_valid") is True for x in rows))


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]], verbosity=2)
