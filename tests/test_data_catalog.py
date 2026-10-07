#!/usr/bin/env python3
import csv
import importlib.util
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BUILDER = ROOT / "tools" / "data_catalog" / "build_catalog.py"
AIRBAND_BUILDER = ROOT / "tools" / "data_catalog" / "build_ourairports_aviation_index.py"


class MapPackCatalogTest(unittest.TestCase):
    def test_map_pack_is_pinned_by_url_and_hash(self):
        openssl = shutil.which("openssl")
        self.assertIsNotNone(openssl)
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            pmtiles = work / "pack.pmtiles"
            pmtiles.write_bytes(b"PMTiles\x03" + b"\0" * 512)
            manifest = work / "pack.manifest.json"
            manifest.write_text(json.dumps({"manifest_version": 1}), encoding="utf-8")
            key, public = work / "key.pem", work / "public.pem"
            subprocess.run([openssl, "ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", key],
                           check=True, capture_output=True)
            subprocess.run([openssl, "ec", "-in", key, "-pubout", "-out", public], check=True, capture_output=True)
            base = "https://github.com/hardcoreerik/orcmaps/releases/download/world-overview-1/"
            pack = {
                "id": "orcmaps_world_z7", "title": "WORLD OVERVIEW z0-7", "version": "2026-09-14",
                "source_date": "2026-09-14", "source_url": "https://www.naturalearthdata.com/",
                "redistribution": "Natural Earth, public domain", "runtime": str(pmtiles), "archive": str(manifest),
                "runtime_url": base + "orcmaps-world-overview-z0-z7.pmtiles",
                "archive_url": base + "orcmaps-world-overview-z0-z7.manifest.json",
                "runtime_destination": "/orcmaps/orcmaps_world_z7.pmtiles",
                "archive_destination": "/orcmaps/orcmaps_world_z7.manifest.json",
            }
            spec = {"schema": "catalog-input-v1", "generated_at": "2026-10-07", "minimum_firmware": "0.2.0",
                    "packs": [pack]}
            source = work / "input.json"
            source.write_text(json.dumps(spec), encoding="utf-8")
            command = [sys.executable, str(BUILDER), str(source), "--out", str(work / "out"),
                       "--private-key", str(key), "--verify-public-key", str(public),
                       "--release-base", "https://example.invalid/release", "--openssl", openssl]
            subprocess.run(command, check=True, capture_output=True, text=True)
            catalog = json.loads((work / "out" / "catalog-v1.json").read_text(encoding="utf-8"))
            entry = catalog["packs"][0]
            self.assertEqual(entry["title"], "WORLD OVERVIEW z0-7")
            runtime = entry["artifacts"]["runtime"]
            self.assertEqual(runtime["url"], pack["runtime_url"])           # pinned in place, not copied
            self.assertEqual(runtime["bytes"], pmtiles.stat().st_size)
            self.assertEqual(len(runtime["sha256"]), 64)
            self.assertFalse((work / "out" / "orcmaps_world_z7-runtime.pmtiles").exists())

            for bad in ({"runtime_destination": "/orcsdr/data/x.pmtiles"}, {"runtime_url": "http://insecure/x"}):
                spec["packs"] = [dict(pack, **bad)]
                source.write_text(json.dumps(spec), encoding="utf-8")
                self.assertNotEqual(subprocess.run(command, capture_output=True, text=True).returncode, 0)
            pmtiles.write_bytes(b"NOTPMTILES" + b"\0" * 64)
            spec["packs"] = [pack]
            source.write_text(json.dumps(spec), encoding="utf-8")
            self.assertNotEqual(subprocess.run(command, capture_output=True, text=True).returncode, 0)


class P25CatalogTest(unittest.TestCase):
    def test_signed_p25_pack_and_destination_gate(self):
        openssl = shutil.which("openssl")
        self.assertIsNotNone(openssl)
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            profile = work / "profile.cfg"
            profile.write_text(
                "version=2\nsystem_name=Catalog Test\ncontrol_channel_hz=851012500\n",
                encoding="utf-8",
            )
            archive = work / "source.zip"
            with zipfile.ZipFile(archive, "w") as bundle:
                bundle.writestr("SOURCE.txt", "test provenance")
            key, public = work / "key.pem", work / "public.pem"
            subprocess.run(
                [openssl, "ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", key],
                check=True, capture_output=True,
            )
            subprocess.run(
                [openssl, "ec", "-in", key, "-pubout", "-out", public],
                check=True, capture_output=True,
            )
            spec = {
                "schema": "catalog-input-v1",
                "generated_at": "2026-09-06",
                "minimum_firmware": "0.2.0",
                "packs": [{
                    "id": "p25_catalog_test",
                    "title": "CATALOG TEST",
                    "version": "2026-09-06",
                    "source_date": "2026-09-06",
                    "source_url": "https://example.invalid/source",
                    "redistribution": "test only",
                    "runtime": str(profile),
                    "archive": str(archive),
                    "runtime_destination": "/orcsdr/p25/p25_catalog_test/profile.cfg",
                    "archive_destination": "/orcsdr/data/p25_catalog_test_source.zip",
                }],
            }
            source = work / "input.json"
            source.write_text(json.dumps(spec), encoding="utf-8")
            command = [
                sys.executable, str(BUILDER), str(source), "--out", str(work / "out"),
                "--private-key", str(key), "--verify-public-key", str(public),
                "--release-base", "https://example.invalid/release", "--openssl", openssl,
            ]
            subprocess.run(command, check=True, capture_output=True, text=True)
            catalog = json.loads((work / "out" / "catalog-v1.json").read_text(encoding="utf-8"))
            self.assertEqual(catalog["packs"][0]["title"], "CATALOG TEST")

            profile.write_text(
                "system_name=version=2\nnote=control_channel_hz=851012500\n",
                encoding="utf-8",
            )
            rejected = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(rejected.returncode, 0)

            profile.write_text(
                "version=2\nsystem_name=Catalog Test\ncontrol_channel_hz=851012500\n",
                encoding="utf-8",
            )
            spec["packs"][0]["runtime_destination"] = "/orcsdr/p25/wrong/profile.cfg"
            source.write_text(json.dumps(spec), encoding="utf-8")
            rejected = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(rejected.returncode, 0)


    def test_global_aviation_schema_and_legacy_alias(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            module_spec = importlib.util.spec_from_file_location("orcsdr_catalog_builder", BUILDER)
            module = importlib.util.module_from_spec(module_spec)
            module_spec.loader.exec_module(module)
            runtime = work / "aviation.idx"
            runtime.write_text(
                "ORCAIR2\n"
                "COM\t135019000\t1039940000\t118600000\tTOWER\tSG\tWSSS\t"
                "Singapore Changi Airport\t\tCOMMUNITY\tOURAIRPORTS\tWSSS TOWER\n",
                encoding="ascii", newline="\n",
            )
            module.validate_artifact("aviation", runtime, False)
            module.validate_artifact("faa_aviation", runtime, False)
            runtime.write_text(
                "ORCCAT1\nATC 441246000 -1232119000 124150000 KEUG TOWER\n",
                encoding="ascii", newline="\n",
            )
            module.validate_artifact("aviation", runtime, False)
            runtime.write_text("BADSCHEMA\n", encoding="ascii", newline="\n")
            with self.assertRaises(ValueError):
                module.validate_artifact("aviation", runtime, False)

    def test_ourairports_country_normalizer(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            airports = work / "airports.csv"
            frequencies = work / "airport-frequencies.csv"
            with airports.open("w", encoding="utf-8", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=[
                    "ident", "name", "latitude_deg", "longitude_deg",
                    "iso_country", "iso_region"])
                writer.writeheader()
                writer.writerows([
                    {"ident": "KEUG", "name": "Mahlon Sweet Field",
                     "latitude_deg": "44.1246", "longitude_deg": "-123.2119",
                     "iso_country": "US", "iso_region": "US-OR"},
                    {"ident": "WSSS", "name": "Singapore Changi Airport",
                     "latitude_deg": "1.35019", "longitude_deg": "103.994",
                     "iso_country": "SG", "iso_region": "SG-01"},
                ])
            with frequencies.open("w", encoding="utf-8", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=[
                    "airport_ident", "type", "description", "frequency_mhz"])
                writer.writeheader()
                writer.writerows([
                    {"airport_ident": "KEUG", "type": "TWR",
                     "description": "Tower", "frequency_mhz": "124.150"},
                    {"airport_ident": "WSSS", "type": "GND",
                     "description": "Ground", "frequency_mhz": "124.300"},
                    {"airport_ident": "WSSS", "type": "TWR",
                     "description": "Tower", "frequency_mhz": "118.600"},
                ])
            output = work / "aviation.idx"
            run = subprocess.run(
                [sys.executable, str(AIRBAND_BUILDER),
                 "--airports", str(airports), "--frequencies", str(frequencies),
                 "--country", "SG", "--output", str(output)],
                check=True, capture_output=True, text=True)
            self.assertIn("wrote 2 ORCAIR2 records", run.stdout)
            text_value = output.read_text(encoding="ascii")
            self.assertTrue(text_value.startswith("ORCAIR2\n"))
            self.assertIn("\tSG\tWSSS\tSingapore Changi Airport\t", text_value)
            self.assertIn("\tCOMMUNITY\tOURAIRPORTS\t", text_value)
            self.assertNotIn("KEUG", text_value)

if __name__ == "__main__":
    unittest.main()
