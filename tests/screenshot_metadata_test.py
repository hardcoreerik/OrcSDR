import base64
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from datetime import datetime

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("orcsdr_screenshot_metadata", ROOT / "tools/screenshot.py")
shot = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shot)


class Port:
    def __init__(self, data):
        self.data = data

    def reset_input_buffer(self):
        pass

    @property
    def in_waiting(self):
        return len(self.data)

    def read(self, count=1):
        value, self.data = self.data[:count], self.data[count:]
        return value


class Device:
    def __init__(self, *, elf=True, valid_hash=True, corrupt=False):
        png = b"\x89PNG\r\n\x1a\nexample"
        digest = hashlib.sha256(png).hexdigest() if valid_hash else "0" * 64
        elf_field = " elf_sha256=" + "a" * 64 if elf else ""
        payload = "%%%" if corrupt else base64.b64encode(png).decode()
        wire = (
            f"ORC_SNAP_BEGIN width=320 height=240 bytes={len(png)} sha256={digest}{elf_field}\n"
            f"ORC_SNAP_DATA 0 {payload}\nORC_SNAP_END\n"
        ).encode()
        self.s = Port(wire)
        self.commands = []

    def authenticate(self, key):
        pass

    def send(self, command):
        self.commands.append(command)


class ScreenshotMetadataTests(unittest.TestCase):
    def capture(self, tmp, device, *, command="SCREENSHOT"):
        target = Path(tmp) / "capture.png"
        size = shot.screenshot(device, "unused", target, tries=1, command=command)
        return target, size

    def test_new_header_saves_verified_metadata(self):
        with tempfile.TemporaryDirectory() as tmp:
            target, size = self.capture(tmp, Device())
            metadata = json.loads(Path(str(target) + ".json").read_text(encoding="utf-8"))
            self.assertEqual(size, target.stat().st_size)
            self.assertEqual((metadata["width"], metadata["height"]), (320, 240))
            self.assertEqual(metadata["bytes"], size)
            self.assertEqual(metadata["png_sha256"], hashlib.sha256(target.read_bytes()).hexdigest())
            self.assertEqual(metadata["elf_sha256"], "a" * 64)
            self.assertEqual(metadata["method"], "SCREENSHOT")
            self.assertTrue(metadata["capture_utc"].endswith("Z"))
            datetime.fromisoformat(metadata["capture_utc"].replace("Z", "+00:00"))

    def test_legacy_header_and_alias(self):
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / "legacy.png"
            self.assertEqual(shot.snap_png(Device(elf=False), "unused", target, tries=1,
                                           command="FT8 SNAP"), 15)
            metadata = json.loads(Path(str(target) + ".json").read_text(encoding="utf-8"))
            self.assertIsNone(metadata["elf_sha256"])
            self.assertEqual(metadata["method"], "FT8 SNAP")

    def test_corrupt_or_hash_mismatch_writes_no_artifacts(self):
        for device in (Device(corrupt=True), Device(valid_hash=False)):
            with self.subTest(device=device), tempfile.TemporaryDirectory() as tmp:
                target = Path(tmp) / "failed.png"
                with self.assertRaises(SystemExit):
                    shot.screenshot(device, "unused", target, tries=1)
                self.assertFalse(target.exists())
                self.assertFalse(Path(str(target) + ".json").exists())


if __name__ == "__main__":
    unittest.main()
