#!/usr/bin/env python3
import json
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
NORMALIZER = ROOT / "tools" / "data_catalog" / "normalize_fcc_lms.py"

FACILITY_HEADER = "|".join([
    "facility_id", "callsign", "service_code", "community_served_city",
    "community_served_state", "facility_status", "frequency", "^",
])


def facility_row(facility_id: str, callsign: str, service: str, city: str,
                 state: str, status: str, frequency: str) -> str:
    return "|".join([facility_id, callsign, service, city, state, status,
                     frequency, "^"])


LKP_SERVICE_HEADER = "active_ind|service_code|service_group_code|^"
LKP_SERVICE_ROWS = [
    "Y|AM|AM|^", "Y|AX|AM|^",
    "Y|FM|FM|^", "Y|FL|FM|^", "Y|FX|FM|^", "Y|FB|FM|^", "Y|FS|FM|^",
    "Y|FA|FM|^", "Y|FR|FM|^",
    "Y|DTV|DTV|^", "Y|323|F323|^",
]
LKP_STATE_HEADER = "active_ind|country_code|state_code|^"
LKP_STATE_ROWS = [
    "Y|US|OR|^", "Y|US|WA|^", "Y|US|CA|^", "Y|US|MO|^",
    "Y|MX|CI|^", "Y|CA|YT|^",
]


def build_lms_zip(path: Path, facility_body: str) -> None:
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("facility.dat", facility_body)
        archive.writestr("lkp_service_code.dat",
                         "\n".join([LKP_SERVICE_HEADER, *LKP_SERVICE_ROWS]))
        archive.writestr("lkp_state.dat",
                         "\n".join([LKP_STATE_HEADER, *LKP_STATE_ROWS]))


class FccLmsNormalizerTest(unittest.TestCase):
    def test_extracts_us_am_fm_translators_rejects_tv_allotment_foreign_bad(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            source = work / "fcc-lms.zip"
            body = "\n".join([
                FACILITY_HEADER,
                # keep — AM licensed
                facility_row("33053", "KEX", "AM", "Portland", "OR", "LICEN", "1190"),
                # keep — Full FM licensed with FCC's binary-noise MHz
                facility_row("59586", "KUOW-FM", "FM", "Seattle", "WA", "LICEN",
                             "94.900001525878906"),
                # keep — Low Power FM
                facility_row("60001", "KLPF-LP", "FL", "Los Angeles", "CA", "LICEN", "101.5"),
                # keep — FM translator, preserved even when void
                facility_row("60002", "K300AA", "FX", "Sacramento", "CA", "FVOID", "107.9"),
                # drop — DTV service
                facility_row("70000", "KATU", "DTV", "Portland", "OR", "LICEN", "43"),
                # drop — FCC placeholder (negative id)
                facility_row("-3", "NEW", "FM", "EMMONAK", "OR", "UNKNO", "93.3"),
                # drop — FM Allotment (planning record, not a station)
                facility_row("80001", "NEW", "FA", "JUNCTION", "CA", "UNKNO", "104.5"),
                # drop — foreign state code (Mexican coordination row)
                facility_row("80002", "XHTFFM", "FM", "MONCLOVA", "CI", "UNKNO", "88.5"),
                # drop — bad frequency
                facility_row("80003", "BAD", "AM", "Portland", "OR", "LICEN", "not-a-number"),
                # drop — out of band FM
                facility_row("80004", "OOR", "FM", "Portland", "OR", "LICEN", "9999"),
                # drop — empty frequency (unassigned)
                facility_row("80005", "NEW", "FM", "Portland", "OR", "UNKNO", ""),
            ])
            build_lms_zip(source, body)
            output = work / "out.ndjson"
            result = subprocess.run(
                [sys.executable, str(NORMALIZER),
                 "--input", str(source), "--out", str(output)],
                check=True, capture_output=True, text=True)
            report = json.loads(result.stdout)
            self.assertEqual(report["records"], 4)
            self.assertEqual(report["services"], {"am": 1, "fm": 3})
            records = [json.loads(line)
                       for line in output.read_text(encoding="utf-8").splitlines()]
            self.assertEqual(
                [(r["callsign"], r["service"], r["frequency_hz"], r["status"])
                 for r in records],
                [("KEX", "am", 1190000, "LICEN"),
                 ("KUOW-FM", "fm", 94900000, "LICEN"),
                 ("KLPF-LP", "fm", 101500000, "LICEN"),
                 ("K300AA", "fm", 107900000, "FVOID")])
            for record in records:
                self.assertEqual(record["source"], "fcc_lms")
                self.assertEqual(record["country"], "US")
                self.assertTrue(record["id"].startswith("fcc_lms:"))
                self.assertEqual(record["source_id"], record["id"].split(":")[1])


if __name__ == "__main__":
    unittest.main()
