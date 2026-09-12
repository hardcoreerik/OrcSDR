#!/usr/bin/env python3
import json
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
NORMALIZER = ROOT / "tools" / "data_catalog" / "normalize_ised_broadcast.py"


def ised_row(frequency: str, call: str, city: str, province: str) -> str:
    row = [""] * 61
    row[0], row[1], row[2], row[33], row[31], row[39], row[47], row[56] = "TX", frequency, f"frequency-{call}", call, city, province, f"id-{call}", "OP"
    row[40], row[41], row[58] = "45.0", "-75.0", "1000"
    return ",".join(f'\"{value}\"' for value in row)


class IsedBroadcastTest(unittest.TestCase):
    def test_normalizes_am_and_fm_and_excludes_non_broadcast_band(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            source = work / "ised.zip"
            with zipfile.ZipFile(source, "w") as archive:
                archive.writestr("broadcast.csv", "\n".join([
                    ised_row("0.99", "CFAM", "Windsor", "ON"),
                    ised_row("99.9", "CKFM-FM", "Toronto", "ON"),
                    ised_row("300.0", "NOT-FM", "Elsewhere", "ON"),
                ]))
            output = work / "out.ndjson"
            result = subprocess.run([sys.executable, str(NORMALIZER), "--input", str(source),
                                     "--out", str(output)], check=True, capture_output=True, text=True)
            self.assertEqual(json.loads(result.stdout)["services"], {"am": 1, "fm": 1})
            records = [json.loads(line) for line in output.read_text(encoding="utf-8").splitlines()]
            self.assertEqual([(row["callsign"], row["frequency_hz"]) for row in records],
                             [("CFAM", 990000), ("CKFM-FM", 99900000)])
            self.assertEqual(len({row["id"] for row in records}), 2)


if __name__ == "__main__":
    unittest.main()
