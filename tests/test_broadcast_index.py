#!/usr/bin/env python3
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BUILDER = ROOT / "tools" / "data_catalog" / "build_broadcast_index.py"
SAMPLE = ROOT / "tools" / "data_catalog" / "broadcast-stations.example.ndjson"


class BroadcastIndexTest(unittest.TestCase):
    def run_builder(self, *arguments: str) -> dict:
        result = subprocess.run([sys.executable, str(BUILDER), *arguments], check=True,
                                capture_output=True, text=True)
        return json.loads(result.stdout)

    def test_build_and_inspect_seekable_index(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "broadcast.idx"
            report = self.run_builder("build", "--input", str(SAMPLE), "--out", str(output),
                                      "--source-date", "2026-09-10")
            self.assertEqual(report["records"], 3)
            self.assertEqual(report["services"], {"am": 1, "fm": 1, "shortwave": 1})
            inspected = self.run_builder("inspect", "--input", str(output))
            self.assertEqual(inspected["source_date"], "2026-09-10")
            self.assertEqual(inspected["records"], 3)
            self.assertEqual(inspected["sha256"], report["sha256"])

    def test_invalid_record_is_never_silently_included(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "bad.ndjson"
            source.write_text('{"id":"bad","source":"test","source_id":"1","service":"fm","frequency_hz":"101700000"}\n', encoding="utf-8")
            output = Path(directory) / "bad.idx"
            failed = subprocess.run([sys.executable, str(BUILDER), "build", "--input", str(source),
                                     "--out", str(output)], capture_output=True, text=True)
            self.assertNotEqual(failed.returncode, 0)
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
