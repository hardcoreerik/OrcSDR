import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("package", ROOT / "tools/release/embed-c6-rom-package.py")
package = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(package)


class RecoveryPackageTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.folder = Path(self.tmp.name)
        lock = json.loads((ROOT / "tools/release/hosted-c6-release.json").read_text())
        self.manifest = {key: lock[key] for key in ("target", "hosted_version", "source_repository",
                         "source_revision", "source_project", "idf_version")}
        local = "\n".join(name + "=" + package.sha((ROOT / lock["source_project"] /
                            name.replace("\\", "/")).read_bytes()) for name in package.LOCAL_SOURCES)
        self.manifest.update(format="orcsdr-c6-rom-recovery-v1", flash_size_bytes=0x400000,
                             local_source_sha256=package.sha(local.encode()), segments=[])
        for role, (offset, _) in package.ROLES.items():
            data = bytearray(b"\xff" * (8192 if role == "ota_data_initial" else 32))
            if role in ("application", "bootloader"):
                data[0] = 0xE9
                data[12:14] = (13).to_bytes(2, "little")
            name = role + ".bin"
            (self.folder / name).write_bytes(data)
            self.manifest["segments"].append(dict(role=role, file=name, offset=hex(offset),
                size_bytes=len(data), sha256=package.sha(data)))

    def validate(self, manifest=None, digest=None):
        raw = json.dumps(manifest or self.manifest).encode()
        path = self.folder / "manifest.json"
        path.write_bytes(raw)
        return package.validate(path, digest or package.sha(raw), ROOT)

    def test_complete_source_owned_package_and_generated_symbols(self):
        segments = self.validate()
        package.generate(segments, self.folder / "generated")
        cpp = (self.folder / "generated/c6_rom_manifest.cpp").read_text()
        self.assertIn("{true,13,0x400000,4,segments}", cpp)
        self.assertEqual(cpp.count('asm("_binary_'), 4)
        self.assertIn('orcsdr_embedded_c6_rom_version() { return ' + json.dumps(self.manifest["hosted_version"]) + '; }', cpp)

    def test_json_trusted_flag_cannot_replace_builder_pin(self):
        self.manifest["trusted"] = True
        with self.assertRaises(ValueError):
            self.validate(digest="0" * 64)

    def test_missing_extra_wrong_target_and_stale_source_rejected(self):
        for key, value in [("target", "esp32c5"), ("source_revision", "old"),
                           ("local_source_sha256", "0" * 64)]:
            m = copy.deepcopy(self.manifest)
            m[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.validate(m)
        for segments in (self.manifest["segments"][:-1], self.manifest["segments"] * 2):
            m = dict(self.manifest, segments=segments)
            with self.assertRaises(ValueError):
                self.validate(m)

    def test_nvs_phy_and_path_escape_rejected(self):
        for offset in ("0x9000", "0xf000"):
            m = copy.deepcopy(self.manifest)
            m["segments"][0]["offset"] = offset
            with self.assertRaises(ValueError):
                self.validate(m)
        self.manifest["segments"][0]["file"] = "../outside.bin"
        with self.assertRaises(ValueError):
            self.validate()

    def test_corrupt_bytes_or_wrong_chip_rejected(self):
        p = self.folder / "application.bin"
        p.write_bytes(b"bad image")
        with self.assertRaises(ValueError):
            self.validate()
        data = bytearray(b"\xff" * 32)
        data[0] = 0xE9
        data[12:14] = (9).to_bytes(2, "little")
        p.write_bytes(data)
        self.manifest["segments"][-1]["sha256"] = package.sha(data)
        with self.assertRaises(ValueError):
            self.validate()


if __name__ == "__main__":
    unittest.main()
