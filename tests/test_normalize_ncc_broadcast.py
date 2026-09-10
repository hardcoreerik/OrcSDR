#!/usr/bin/env python3
import csv
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NORMALIZER = ROOT / "tools" / "data_catalog" / "normalize_ncc_broadcast.py"


def write_fm_csv(path: Path) -> None:
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["FM 電臺名稱", "頻率(MHz)", "發射機地址", "東經", "北緯", "電臺類別"])
        writer.writerow(["測試調頻電臺", "88.1", "臺北市信義區", "121.5654", "25.0330", "學校"])
        writer.writerow(["測試調頻電臺", "88.1", "臺北市信義區", "121.5654", "25.0330", "學校"])
        writer.writerow(["壞頻率", "200.0", "某處", "121.0", "25.0", "甲"])


def write_am_csv(path: Path) -> None:
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["AM 電臺名稱", "頻率(kHz)", "發射機地址", "東經", "北緯", "電臺類別"])
        writer.writerow(["測試調幅電臺", "531", "新北市土城區", "121.436667", "24.983889", "丙"])
        writer.writerow(["海外短波電臺", "6085", "新北市淡水區", "121.415833", "25.186667", "海外電臺"])
        writer.writerow(["壞頻率", "50", "某處", "121.0", "25.0", "甲"])


class NccBroadcastTest(unittest.TestCase):
    def test_normalizes_am_fm_and_shortwave(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            fm = work / "fm.csv"
            am = work / "am.csv"
            write_fm_csv(fm)
            write_am_csv(am)
            output = work / "out.ndjson"
            result = subprocess.run(
                [sys.executable, str(NORMALIZER), "--fm", str(fm), "--am", str(am), "--out", str(output)],
                check=True,
                capture_output=True,
                text=True,
            )
            report = json.loads(result.stdout)
            self.assertEqual(report["countries"], {"TW": 3})
            self.assertEqual(report["services"], {"am": 1, "fm": 1, "shortwave": 1})
            self.assertEqual(report["rejected"]["out_of_fm_band"], 1)
            self.assertEqual(report["rejected"]["out_of_band"], 1)
            self.assertEqual(report["rejected"].get("duplicate_identical", 0), 1)
            records = [json.loads(line) for line in output.read_text(encoding="utf-8").splitlines()]
            self.assertEqual(len(records), 3)
            by_service = {row["service"]: row for row in records}
            fm_row = by_service["fm"]
            am_row = by_service["am"]
            sw_row = by_service["shortwave"]
            self.assertEqual(fm_row["source"], "ncc_am_fm_stations")
            self.assertEqual(fm_row["country"], "TW")
            self.assertEqual(fm_row["frequency_hz"], 88100000)
            self.assertEqual(fm_row["latitude_e7"], 250330000)
            self.assertEqual(fm_row["longitude_e7"], 1215654000)
            self.assertEqual(fm_row["status"], "學校")
            self.assertEqual(am_row["frequency_hz"], 531000)
            self.assertEqual(am_row["latitude_e7"], 249838890)
            self.assertEqual(am_row["status"], "丙")
            self.assertEqual(sw_row["frequency_hz"], 6085000)
            self.assertEqual(sw_row["status"], "海外電臺")
            self.assertTrue(fm_row["id"].startswith("ncc_am_fm_stations:"))


if __name__ == "__main__":
    unittest.main()
