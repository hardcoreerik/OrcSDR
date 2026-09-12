#!/usr/bin/env python3
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
NORMALIZER = ROOT / "tools" / "data_catalog" / "normalize_bakom_broadcast.py"


SAMPLE = {
    "type": "FeatureCollection",
    "crs": {"type": "name", "properties": {"name": "EPSG:2056"}},
    "features": [
        {  # Radio site with three RADIO services + one DAB+ that must be excluded
            "type": "Feature",
            "geometry": {"type": "Point", "coordinates": [2600000, 1200000]},  # LV95 bern-ish
            "properties": {
                "id": 1, "code": "TESTMTN", "name": "TEST MOUNTAIN",
                "power": "5 kW",
                "service": "RADIO,DAB+,RADIO",
                "program": "TestRadio One,SMC Test,TestRadio Two",
                "freqchan": "88.1 MHz,7A,104.4 MHz",
                "lang": "en",
            },
        },
        {  # Pure DAB+ site — must produce zero records
            "type": "Feature",
            "geometry": {"type": "Point", "coordinates": [2700000, 1200000]},
            "properties": {
                "id": 2, "code": "DABONLY", "name": "DAB ONLY",
                "power": "1 kW",
                "service": "DAB+",
                "program": "SMC D01",
                "freqchan": "7D",
                "lang": "en",
            },
        },
        {  # RADIO with out-of-band frequency — must reject
            "type": "Feature",
            "geometry": {"type": "Point", "coordinates": [2600000, 1200000]},
            "properties": {
                "id": 3, "code": "BADFREQ", "name": "BAD FREQ",
                "power": "1 kW",
                "service": "RADIO",
                "program": "Ghost",
                "freqchan": "150 MHz",
                "lang": "en",
            },
        },
    ],
}


class BakomBroadcastNormalizerTest(unittest.TestCase):
    def test_keeps_fm_radio_drops_dab_and_out_of_band(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            source = work / "bakom.geojson"
            source.write_text(json.dumps(SAMPLE), encoding="utf-8")
            output = work / "out.ndjson"
            subprocess.run(
                [sys.executable, str(NORMALIZER),
                 "--input", str(source), "--out", str(output)],
                check=True, capture_output=True, text=False)
            records = [json.loads(line) for line in
                       output.read_text(encoding="utf-8").splitlines()]
            self.assertEqual(len(records), 2)
            frequencies = sorted(r["frequency_hz"] for r in records)
            self.assertEqual(frequencies, [88100000, 104400000])
            for record in records:
                self.assertEqual(record["service"], "fm")
                self.assertEqual(record["country"], "CH")
                self.assertEqual(record["source"], "bakom_radio_fernsehsender")
                self.assertIn("latitude_e7", record)
                self.assertIn("longitude_e7", record)
                self.assertIn("power_w", record)
                self.assertGreater(record["power_w"], 0)


if __name__ == "__main__":
    unittest.main()
