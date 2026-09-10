#!/usr/bin/env python3
import csv
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
NORMALIZER = ROOT / "tools" / "data_catalog" / "normalize_ofca_broadcast.py"


HEADER = [
    "BROADCASTER",
    "CH_NAME",
    "TX_STATION",
    "MODULATION",
    "FREQ",
    "ERP",
    "REBROADCAST_TUNNEL",
    "REMARK",
]


def write_csv(path: Path, rows: list[list[str]]) -> None:
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(HEADER)
        writer.writerows(rows)


class OfcaBroadcastTest(unittest.TestCase):
    def test_normalizes_am_and_fm_rows(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            source = work / "ofca.csv"
            write_csv(source, [
                [
                    "Hong Kong Commercial Broadcasting Company Limited (CRHK)",
                    "CR 1",
                    "Mount Gough",
                    "VHF/FM",
                    "88.1",
                    "3000",
                    "Cross Harbour",
                    "",
                ],
                [
                    "Radio Television Hong Kong (RTHK)",
                    "RTHK Radio 3",
                    "Golden Hill",
                    "MF/AM",
                    "0.567",
                    "20000",
                    "",
                    "",
                ],
                [
                    "Radio Television Hong Kong (RTHK)",
                    "RTHK Radio 1",
                    "Hill 374",
                    "VHF/FM",
                    "200.0",
                    "50",
                    "",
                    "Locally available",
                ],
                [
                    "Hong Kong Commercial Broadcasting Company Limited (CRHK)",
                    "CR 1",
                    "Mount Gough",
                    "VHF/FM",
                    "88.1",
                    "3000",
                    "Cross Harbour",
                    "",
                ],
            ])
            output = work / "out.ndjson"
            result = subprocess.run(
                [sys.executable, str(NORMALIZER), "--input", str(source), "--out", str(output)],
                check=True,
                capture_output=True,
                text=True,
            )
            report = json.loads(result.stdout)
            self.assertEqual(report["services"], {"am": 1, "fm": 1})
            self.assertEqual(report["countries"], {"HK": 2})
            self.assertEqual(report["rejected"]["out_of_fm_band"], 1)
            self.assertEqual(report["rejected"].get("duplicate_identical", 0), 1)
            records = [json.loads(line) for line in output.read_text(encoding="utf-8").splitlines()]
            self.assertEqual(len(records), 2)
            by_service = {row["service"]: row for row in records}
            fm = by_service["fm"]
            am = by_service["am"]
            self.assertEqual(fm["source"], "ofca_sound_freq_table")
            self.assertEqual(fm["country"], "HK")
            self.assertEqual(fm["frequency_hz"], 88100000)
            self.assertEqual(fm["power_w"], 3000)
            self.assertEqual(fm["name"], "CR 1")
            self.assertEqual(fm["transmitter"], "Mount Gough")
            self.assertEqual(fm["source_id"], "CR 1|Mount Gough|88.1|VHF/FM")
            self.assertTrue(fm["id"].startswith("ofca_sound_freq_table:"))
            self.assertNotIn("latitude_e7", fm)
            self.assertEqual(am["frequency_hz"], 567000)
            self.assertEqual(am["power_w"], 20000)
            self.assertEqual(am["transmitter"], "Golden Hill")
            self.assertEqual(am["country"], "HK")


if __name__ == "__main__":
    unittest.main()
