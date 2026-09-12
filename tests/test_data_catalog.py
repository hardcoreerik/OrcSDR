#!/usr/bin/env python3
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
BROADCAST_BUILDER = ROOT / "tools" / "data_catalog" / "build_broadcast_index.py"
BROADCAST_SAMPLE = ROOT / "tools" / "data_catalog" / "broadcast-stations.example.ndjson"


def openssl_path():
    fallback = Path(r"C:\Program Files\Git\usr\bin\openssl.exe")
    return shutil.which("openssl") or (str(fallback) if fallback.is_file() else None)


class P25CatalogTest(unittest.TestCase):
    def test_broadcast_pack_requires_index_and_destination(self):
        openssl = openssl_path()
        self.assertIsNotNone(openssl)
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            runtime = work / "broadcast.idx"
            subprocess.run([sys.executable, str(BROADCAST_BUILDER), "build", "--input",
                            str(BROADCAST_SAMPLE), "--out", str(runtime), "--source-date",
                            "2026-09-10"], check=True, capture_output=True, text=True)
            archive = work / "source.zip"
            with zipfile.ZipFile(archive, "w") as bundle:
                bundle.writestr("SOURCE.txt", "test provenance")
            key, public = work / "key.pem", work / "public.pem"
            subprocess.run([openssl, "ecparam", "-name", "prime256v1", "-genkey", "-noout",
                            "-out", key], check=True, capture_output=True)
            subprocess.run([openssl, "ec", "-in", key, "-pubout", "-out", public],
                           check=True, capture_output=True)
            spec = {"schema": "catalog-input-v1", "generated_at": "2026-09-10",
                    "minimum_firmware": "0.2.0", "packs": [{
                        "id": "fcc_broadcast", "version": "2026-09-10",
                        "source_date": "2026-09-10", "source_url": "https://example.invalid/source",
                        "redistribution": "test only", "source_ids": ["fcc_lms"], "runtime": str(runtime),
                        "archive": str(archive), "runtime_destination": "/orcsdr/data/fcc_broadcast.idx",
                        "archive_destination": "/orcsdr/data/fcc_broadcast_source.zip"}]}
            source = work / "input.json"
            source.write_text(json.dumps(spec), encoding="utf-8")
            command = [sys.executable, str(BUILDER), str(source), "--out", str(work / "out"),
                       "--private-key", str(key), "--verify-public-key", str(public),
                       "--release-base", "https://example.invalid/release", "--openssl", openssl]
            subprocess.run(command, check=True, capture_output=True, text=True)
            spec["packs"][0]["source_ids"] = ["hfcc"]
            source.write_text(json.dumps(spec), encoding="utf-8")
            self.assertNotEqual(subprocess.run(command, capture_output=True, text=True).returncode, 0)
            spec["packs"][0]["source_ids"] = ["fcc_lms"]
            spec["packs"][0]["runtime_destination"] = "/orcsdr/data/wrong.idx"
            source.write_text(json.dumps(spec), encoding="utf-8")
            self.assertNotEqual(subprocess.run(command, capture_output=True, text=True).returncode, 0)

    def test_signed_p25_pack_and_destination_gate(self):
        openssl = openssl_path()
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


if __name__ == "__main__":
    unittest.main()
