import hashlib
import sys
import tempfile
import unittest
from copy import deepcopy
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[1]))
from corpus_manifest import validate_manifest, verify_capture
from decode_orciq import HEADER, MAGIC


class CorpusManifestTests(unittest.TestCase):
    @staticmethod
    def entry(capture_id):
        return {
            "capture_id": capture_id,
            "filename": "capture.orciq",
            "path": "mla30/capture.orciq",
            "sha256": "a" * 64,
            "captured_at": "2026-09-13T00:00:00.000Z",
            "sample_rate_sps": 960000,
            "center_frequency_hz": 906875000,
            "bandwidth_hz": 250000,
            "spreading_factor": 11,
            "modem_preset": "LONGFAST",
            "region": "US",
            "expected": None,
            "reference": {"result": "unknown"},
            "host": {"result": "fail"},
            "native": {"result": "unknown", "evidence_source": None},
            "differential_class": "unknown",
            "classification": "confirmed-non-lora-negative",
            "setup": {"antenna": "MLA-30+", "location": "outdoor ~50 ft"},
            "notes": "No preamble in host replay.",
        }

    def test_manifest_rejects_duplicate_capture_ids(self):
        manifest = {
            "schema_version": 1,
            "captures": [
                self.entry("orciq-aaaaaaaaaaaaaaaa"),
                self.entry("orciq-aaaaaaaaaaaaaaaa"),
            ],
        }

        with self.assertRaisesRegex(ValueError, "duplicate capture_id"):
            validate_manifest(manifest)

    def test_manifest_rejects_unsafe_or_malformed_entries(self):
        cases = []
        missing = self.entry("missing")
        del missing["sha256"]
        cases.append(("missing fields", missing))
        for message, field, value in (
            ("relative .orciq path", "path", "../capture.orciq"),
            ("filename does not match", "filename", "other.orciq"),
            ("invalid sha256", "sha256", "not-a-digest"),
            ("invalid classification", "classification", "noise-ish"),
            ("invalid differential_class", "differential_class", "E"),
        ):
            entry = deepcopy(self.entry(message))
            entry[field] = value
            cases.append((message, entry))

        for message, entry in cases:
            with self.subTest(message=message), self.assertRaisesRegex(ValueError, message):
                validate_manifest({"schema_version": 1, "captures": [entry]})

    def test_manifest_rejects_capture_id_not_bound_to_content(self):
        entry = self.entry("orciq-not-the-hash")

        with self.assertRaisesRegex(ValueError, "capture_id does not match sha256"):
            validate_manifest({"schema_version": 1, "captures": [entry]})

    def test_verify_capture_detects_hash_mismatch(self):
        raw = b"\x7f\x80\x81\x82"
        header = HEADER.pack(MAGIC, HEADER.size, 960000, 906875000, len(raw), 1, 11, 0, 250000, 0)
        original = header + raw
        entry = self.entry("hash-check")
        entry["sha256"] = hashlib.sha256(original).hexdigest()

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / entry["path"]
            path.parent.mkdir()
            path.write_bytes(original[:-1] + b"\x83")
            with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                verify_capture(entry, Path(directory))

    def test_verify_capture_reports_header_and_host_failure(self):
        raw = b"\x7f\x80" * 4096
        header = HEADER.pack(MAGIC, HEADER.size, 960000, 906875000, len(raw), 1, 11, 0, 250000, 0)
        content = header + raw
        entry = self.entry("no-preamble")
        entry["sha256"] = hashlib.sha256(content).hexdigest()

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / entry["path"]
            path.parent.mkdir()
            path.write_bytes(content)
            result = verify_capture(entry, Path(directory))

        self.assertEqual(result["header"], {
            "sample_rate_sps": 960000,
            "center_frequency_hz": 906875000,
            "bandwidth_hz": 250000,
            "spreading_factor": 11,
            "raw_bytes": 8192,
        })
        self.assertEqual(result["host"]["result"], "fail")
        self.assertIn("no LoRa preamble", result["host"]["exception"])


if __name__ == "__main__":
    unittest.main()
