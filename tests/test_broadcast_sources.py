#!/usr/bin/env python3
import json
import unittest
from pathlib import Path


LEDGER = Path(__file__).resolve().parents[1] / "tools" / "data_catalog" / "broadcast-sources.json"


class BroadcastSourcesTest(unittest.TestCase):
    def test_every_source_has_a_real_gate_and_provenance(self):
        sources = json.loads(LEDGER.read_text(encoding="utf-8"))["sources"]
        self.assertGreaterEqual(len(sources), 9)
        for source in sources:
            self.assertTrue(source["id"])
            self.assertTrue(source["catalog_url"].startswith("https://"))
            self.assertTrue(source["redistribution"])
            self.assertIsInstance(source["release_allowed"], bool)
        self.assertFalse(next(row for row in sources if row["id"] == "hfcc")["release_allowed"])
        nrta = next(row for row in sources if row["id"] == "nrta_prefecture_broadcast_directory")
        self.assertFalse(nrta["release_allowed"])
        self.assertEqual(nrta["country"], "CN")
        self.assertIn("Permission required", nrta["redistribution"])
        ofca = next(row for row in sources if row["id"] == "ofca_sound_freq_table")
        self.assertTrue(ofca["release_allowed"])
        self.assertEqual(ofca["country"], "HK")
        self.assertIn("DATA.GOV.HK", ofca["redistribution"])
        self.assertTrue(ofca["download_url"].endswith("analogue_sound_broadcasting_frequency_en.csv"))

        ncc = next(row for row in sources if row["id"] == "ncc_am_fm_stations")
        self.assertTrue(ncc["release_allowed"])
        self.assertEqual(ncc["country"], "TW")
        self.assertEqual(set(ncc["services"]), {"am", "fm"})
        self.assertIn("OGDL", ncc["redistribution"])
        self.assertIn("data.gov.tw/dataset/6445", ncc["catalog_url"])
        self.assertTrue(ncc["download_url"].startswith("https://api.ncc.gov.tw/uploaddowndoc"))
        self.assertIn("6446", ncc.get("am_catalog_url", ""))


if __name__ == "__main__":
    unittest.main()
