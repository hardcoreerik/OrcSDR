#!/usr/bin/env python3
"""Tiny fixtures for FAA NASR FRQ airband normalization."""

from __future__ import annotations

import csv
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NORMALIZER = ROOT / "tools" / "data_catalog" / "normalize_faa_nasr_frq.py"

FRQ_HEADER = [
    "EFF_DATE",
    "FACILITY",
    "FAC_NAME",
    "FACILITY_TYPE",
    "ARTCC_OR_FSS_ID",
    "CPDLC",
    "TOWER_HRS",
    "SERVICED_FACILITY",
    "SERVICED_FAC_NAME",
    "SERVICED_SITE_TYPE",
    "LAT_DECIMAL",
    "LONG_DECIMAL",
    "SERVICED_CITY",
    "SERVICED_STATE",
    "SERVICED_COUNTRY",
    "TOWER_OR_COMM_CALL",
    "PRIMARY_APPROACH_RADIO_CALL",
    "FREQ",
    "SECTORIZATION",
    "FREQ_USE",
    "REMARK",
]

APT_HEADER = [
    "EFF_DATE",
    "SITE_NO",
    "SITE_TYPE_CODE",
    "STATE_CODE",
    "ARPT_ID",
    "CITY",
    "COUNTRY_CODE",
    "REGION_CODE",
    "ADO_CODE",
    "STATE_NAME",
    "COUNTY_NAME",
    "COUNTY_ASSOC_STATE",
    "ARPT_NAME",
    "OWNERSHIP_TYPE_CODE",
    "FACILITY_USE_CODE",
    "LAT_DEG",
    "LAT_MIN",
    "LAT_SEC",
    "LAT_HEMIS",
    "LAT_DECIMAL",
    "LONG_DEG",
    "LONG_MIN",
    "LONG_SEC",
    "LONG_HEMIS",
    "LONG_DECIMAL",
    "ICAO_ID",
]


def frq_row(**overrides: str) -> dict[str, str]:
    base = {key: "" for key in FRQ_HEADER}
    base.update(
        {
            "EFF_DATE": "2026/09/03",
            "FACILITY": "EUG",
            "FAC_NAME": "EUGENE",
            "FACILITY_TYPE": "ATCT-TRACON",
            "SERVICED_FACILITY": "EUG",
            "SERVICED_FAC_NAME": "MAHLON SWEET FLD",
            "SERVICED_SITE_TYPE": "AIRPORT",
            "LAT_DECIMAL": "44.12458333",
            "LONG_DECIMAL": "-123.21197222",
            "SERVICED_CITY": "EUGENE",
            "SERVICED_STATE": "OR",
            "SERVICED_COUNTRY": "US",
            "FREQ": "118.9",
            "FREQ_USE": "LCL/P",
        }
    )
    base.update(overrides)
    return base


def write_csv(path: Path, header: list[str], rows: list[dict[str, str]]) -> None:
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=header, extrasaction="ignore")
        writer.writeheader()
        for row in rows:
            writer.writerow({key: row.get(key, "") for key in header})


class NormalizeFaaNasrFrqTest(unittest.TestCase):
    def test_ctaf_and_multi_freq_and_navaid_skip(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            frq = work / "FRQ.csv"
            apt = work / "APT_BASE.csv"
            write_csv(
                frq,
                FRQ_HEADER,
                [
                    frq_row(FREQ="122.8", FREQ_USE="CTAF"),
                    frq_row(
                        FREQ="118.900 & 124.150",
                        FREQ_USE="LCL/P",
                        SECTORIZATION="16R-34L/16L-34R",
                    ),
                    frq_row(FREQ="112.9/76X", FREQ_USE="EUG VORTAC"),  # navaid — drop
                    frq_row(FREQ="243.0", FREQ_USE="EMERG"),  # UHF — drop
                    frq_row(
                        FACILITY="ZZZ",
                        SERVICED_FACILITY="ZZZ",
                        FREQ="119.1",
                        FREQ_USE="ATIS",
                        LAT_DECIMAL="",
                        LONG_DECIMAL="",
                    ),  # coords from APT
                ],
            )
            write_csv(
                apt,
                APT_HEADER,
                [
                    {
                        "ARPT_ID": "EUG",
                        "CITY": "EUGENE",
                        "STATE_CODE": "OR",
                        "COUNTRY_CODE": "US",
                        "LAT_DECIMAL": "44.12458333",
                        "LONG_DECIMAL": "-123.21197222",
                        "ICAO_ID": "KEUG",
                    },
                    {
                        "ARPT_ID": "ZZZ",
                        "CITY": "TESTVILLE",
                        "STATE_CODE": "OR",
                        "COUNTRY_CODE": "US",
                        "LAT_DECIMAL": "45.0",
                        "LONG_DECIMAL": "-122.0",
                        "ICAO_ID": "KZZZ",
                    },
                ],
            )
            ndjson = work / "out.ndjson"
            reviewed = work / "out.csv"
            receipt = work / "receipt.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(NORMALIZER),
                    "--frq",
                    str(frq),
                    "--apt-base",
                    str(apt),
                    "--ndjson",
                    str(ndjson),
                    "--csv",
                    str(reviewed),
                    "--receipt",
                    str(receipt),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            summary = json.loads(result.stdout)
            self.assertEqual(summary["output_rows"], 4)

            records = [json.loads(line) for line in ndjson.read_text(encoding="utf-8").splitlines()]
            labels = sorted(row["label"] for row in records)
            self.assertEqual(labels, ["EUG CTAF", "EUG TWR", "EUG TWR", "ZZZ ATIS"])

            twr = [row for row in records if row["label"] == "EUG TWR"]
            self.assertEqual(sorted(row["frequency_mhz"] for row in twr), [118.9, 124.15])
            self.assertTrue(all(row["icao_id"] == "KEUG" for row in twr))
            self.assertTrue(all(118_000_000 <= int(row["frequency_hz"]) <= 136_975_000 for row in records))

            zzz = next(row for row in records if row["airport_id"] == "ZZZ")
            self.assertEqual(zzz["icao_id"], "KZZZ")
            self.assertEqual(zzz["latitude"], 45.0)
            self.assertEqual(zzz["id"], "faa_nasr_frq:ZZZ:119.100:ATIS")

            with reviewed.open(encoding="utf-8", newline="") as csv_stream:
                csv_rows = list(csv.DictReader(csv_stream))
            self.assertEqual(len(csv_rows), 4)
            self.assertEqual(set(csv_rows[0].keys()), {"latitude", "longitude", "frequency_mhz", "label"})

            receipt_obj = json.loads(receipt.read_text(encoding="utf-8"))
            self.assertEqual(receipt_obj["cycle_date"], "2026-09-03")
            self.assertEqual(receipt_obj["output_rows"], 4)
            self.assertIn("frq_csv", receipt_obj["inputs"])


if __name__ == "__main__":
    unittest.main()
