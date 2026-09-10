#!/usr/bin/env python3
import csv
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
NORMALIZER = ROOT / "tools" / "data_catalog" / "normalize_ofcom_broadcast.py"


def ofcom_header() -> list[str]:
    header = [""] * 166
    header[0] = "Date"
    header[1] = "Station"
    header[2] = "Area"
    header[3] = "Site"
    header[4] = "Licence "
    header[5] = "Frequency"
    header[6] = "NGR"
    header[7] = "RDS PS"
    header[8] = "RDS PI"
    header[14] = "In-UseERP/HP"
    header[15] = "In-Use ERP/VP"
    header[89] = "Licensed ERP/HP "
    header[90] = "Licensed ERP/VP"
    header[163] = "Lat"
    header[164] = "Long"
    return header


def ofcom_row(
    station: str = "Test FM",
    area: str = "Test Area",
    site: str = "Test Site",
    licence: str = "CR999999",
    frequency: str = "97.7",
    rds_ps: str = "TEST_FM_",
    rds_pi: str = "C123",
    in_use_hp: str = "0.000,000",
    in_use_vp: str = "0.025,000",
    licensed_hp: str = "0.025,000",
    licensed_vp: str = "0.025,000",
    lat: str = "51.5",
    lon: str = "-0.12",
) -> list[str]:
    row = [""] * 166
    row[1], row[2], row[3], row[4], row[5] = station, area, site, licence, frequency
    row[7], row[8] = rds_ps, rds_pi
    row[14], row[15], row[89], row[90] = in_use_hp, in_use_vp, licensed_hp, licensed_vp
    row[163], row[164] = lat, lon
    return row


def ofcom_mf_header() -> list[str]:
    header = [""] * 87
    header[0] = "Date"
    header[1] = "Station"
    header[2] = "Area"
    header[3] = "Site"
    header[4] = "Licence"
    header[5] = "Frequency"
    header[6] = "NGR"
    header[9] = "In-Use EMRP (kW)"
    header[48] = "Licensed EMRP"
    header[85] = "Lat"
    header[86] = "Long"
    return header


def ofcom_mf_row(
    station: str = "Test AM",
    area: str = "Test Area",
    site: str = "Test MF Site",
    licence: str = "AN999999",
    frequency: str = "909",
    in_use_emrp: str = "1.500,000",
    licensed_emrp: str = "2.000,000",
    lat: str = "51.5",
    lon: str = "-0.12",
) -> list[str]:
    row = [""] * 87
    row[1], row[2], row[3], row[4], row[5] = station, area, site, licence, frequency
    row[9], row[48] = in_use_emrp, licensed_emrp
    row[85], row[86] = lat, lon
    return row


class OfcomBroadcastTest(unittest.TestCase):
    def test_normalizes_vhf_fm_row(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            source = work / "txparamsvhf.csv"
            with source.open("w", encoding="cp1252", newline="") as handle:
                writer = csv.writer(handle)
                writer.writerow(ofcom_header())
                writer.writerow(ofcom_row())
                writer.writerow(ofcom_row(frequency="300.0", licence="CRBAD"))
                writer.writerow(ofcom_row())  # true duplicate
            output = work / "out.ndjson"
            result = subprocess.run(
                [sys.executable, str(NORMALIZER), "--input", str(source), "--out", str(output)],
                check=True,
                capture_output=True,
                text=True,
            )
            report = json.loads(result.stdout)
            self.assertEqual(report["services"], {"fm": 1})
            self.assertEqual(report["countries"], {"GB": 1})
            self.assertEqual(report["rejected"]["out_of_fm_band"], 1)
            self.assertEqual(report["rejected"].get("duplicate_identical", 0), 1)
            records = [json.loads(line) for line in output.read_text(encoding="utf-8").splitlines()]
            self.assertEqual(len(records), 1)
            row = records[0]
            self.assertEqual(row["source"], "ofcom_txparams")
            self.assertEqual(row["source_id"], "CR999999")
            self.assertEqual(row["country"], "GB")
            self.assertEqual(row["service"], "fm")
            self.assertEqual(row["frequency_hz"], 97700000)
            self.assertEqual(row["latitude_e7"], 515000000)
            self.assertEqual(row["longitude_e7"], -1200000)
            self.assertEqual(row["power_w"], 25)
            self.assertEqual(row["name"], "Test FM")
            self.assertEqual(row["transmitter"], "Test Site")
            self.assertEqual(row["city"], "Test Area")
            self.assertTrue(row["id"].startswith("ofcom_txparams:CR999999:97700000:"))

    def test_normalizes_mf_am_row(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            source = work / "txparamsmf.csv"
            with source.open("w", encoding="cp1252", newline="") as handle:
                writer = csv.writer(handle)
                writer.writerow(ofcom_mf_header())
                writer.writerow(ofcom_mf_row())
                writer.writerow(ofcom_mf_row(frequency="3000", licence="ANBAD"))  # shortwave-ish, reject
                writer.writerow(ofcom_mf_row())  # true duplicate
            output = work / "out.ndjson"
            result = subprocess.run(
                [sys.executable, str(NORMALIZER), "--mf", str(source), "--out", str(output)],
                check=True,
                capture_output=True,
                text=True,
            )
            report = json.loads(result.stdout)
            self.assertEqual(report["services"], {"am": 1})
            self.assertEqual(report["countries"], {"GB": 1})
            self.assertEqual(report["rejected"]["out_of_am_band"], 1)
            self.assertEqual(report["rejected"].get("duplicate_identical", 0), 1)
            records = [json.loads(line) for line in output.read_text(encoding="utf-8").splitlines()]
            self.assertEqual(len(records), 1)
            row = records[0]
            self.assertEqual(row["source"], "ofcom_txparams")
            self.assertEqual(row["source_id"], "AN999999")
            self.assertEqual(row["country"], "GB")
            self.assertEqual(row["service"], "am")
            self.assertEqual(row["frequency_hz"], 909000)
            self.assertEqual(row["latitude_e7"], 515000000)
            self.assertEqual(row["longitude_e7"], -1200000)
            self.assertEqual(row["power_w"], 1500)
            self.assertEqual(row["name"], "Test AM")
            self.assertEqual(row["transmitter"], "Test MF Site")
            self.assertNotIn("rds_ps", row)
            self.assertTrue(row["id"].startswith("ofcom_txparams:AN999999:909000:"))


    def test_maps_crown_dependency_country_and_keeps_transmitter(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            source = work / "txparamsvhf.csv"
            with source.open("w", encoding="cp1252", newline="") as handle:
                writer = csv.writer(handle)
                writer.writerow(ofcom_header())
                writer.writerow(ofcom_row(
                    station="Manx Radio",
                    area="Isle of Man",
                    site="Foxdale",
                    licence="CRIOM001",
                    frequency="97.2",
                    lat="54.15",
                    lon="-4.48",
                ))
                writer.writerow(ofcom_row(
                    station="BBC Radio Jersey",
                    area="Jersey",
                    site="Les Platons",
                    licence="CRJE001",
                    frequency="88.8",
                    lat="49.24",
                    lon="-2.10",
                ))
                writer.writerow(ofcom_row(
                    station="BBC Radio Guernsey",
                    area="Guernsey",
                    site="Les Touillets",
                    licence="CRGG001",
                    frequency="93.2",
                    lat="49.45",
                    lon="-2.58",
                ))
                writer.writerow(ofcom_row())  # GB baseline
            output = work / "out.ndjson"
            result = subprocess.run(
                [sys.executable, str(NORMALIZER), "--input", str(source), "--out", str(output)],
                check=True,
                capture_output=True,
                text=True,
            )
            report = json.loads(result.stdout)
            self.assertEqual(report["countries"], {"GB": 1, "GG": 1, "IM": 1, "JE": 1})
            records = {row["country"]: row for row in (
                json.loads(line) for line in output.read_text(encoding="utf-8").splitlines()
            )}
            self.assertEqual(records["IM"]["transmitter"], "Foxdale")
            self.assertEqual(records["IM"]["city"], "Isle of Man")
            self.assertEqual(records["JE"]["transmitter"], "Les Platons")
            self.assertEqual(records["GG"]["transmitter"], "Les Touillets")
            self.assertEqual(records["GB"]["transmitter"], "Test Site")
            self.assertNotIn("site", records["IM"])


    def test_combines_vhf_and_mf(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            vhf = work / "vhf.csv"
            mf = work / "mf.csv"
            with vhf.open("w", encoding="cp1252", newline="") as handle:
                writer = csv.writer(handle)
                writer.writerow(ofcom_header())
                writer.writerow(ofcom_row())
            with mf.open("w", encoding="cp1252", newline="") as handle:
                writer = csv.writer(handle)
                writer.writerow(ofcom_mf_header())
                writer.writerow(ofcom_mf_row())
            output = work / "out.ndjson"
            result = subprocess.run(
                [sys.executable, str(NORMALIZER), "--vhf", str(vhf), "--mf", str(mf), "--out", str(output)],
                check=True,
                capture_output=True,
                text=True,
            )
            report = json.loads(result.stdout)
            self.assertEqual(report["services"], {"am": 1, "fm": 1})
            self.assertEqual(report["countries"], {"GB": 2})
            records = [json.loads(line) for line in output.read_text(encoding="utf-8").splitlines()]
            self.assertEqual({row["service"] for row in records}, {"am", "fm"})


if __name__ == "__main__":
    unittest.main()
