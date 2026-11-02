"""Compile real Settings clock rendering/input against a recording display."""
import ctypes as c
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SettingsConnectivityRecordingTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(temp.cleanup)
        tools = Path(r"C:/Program Files/Microsoft Visual Studio/18/Community/VC")
        batch = tools / "Auxiliary/Build/vcvars64.bat"
        result = subprocess.run(f'cmd.exe /d /s /c ""{batch}" >nul && set"',
                                text=True, capture_output=True)
        if result.returncode:
            raise AssertionError("MSVC developer environment activation failed")
        env = dict(os.environ)
        for line in result.stdout.splitlines():
            if "=" in line:
                key, value = line.split("=", 1)
                if key.upper() in {"INCLUDE", "LIB", "LIBPATH", "PATH"}:
                    env[key] = value
        cl = tools / "Tools/MSVC/14.50.35717/bin/Hostx64/x64/cl.exe"
        ui = ROOT / "apps/orcsdr-tab5/ui"
        run = subprocess.run([
            str(cl), "/nologo", "/std:c++17", "/EHsc", "/MD", "/LD",
            "/I" + str(ROOT / "tests/settings_clock_stubs"),
            "/I" + str(ui),
            str(ROOT / "tests/settings_connectivity_recording_fixture.cpp"),
            str(ui / "clock_settings.cpp"),
            str(ui / "text_editor.cpp"),str(ui / "keyboard_input.cpp"),
            "/link", "/OUT:settingsconnectivity.dll"], cwd=temp.name, env=env,
            text=True, capture_output=True)
        if run.returncode:
            raise AssertionError(run.stdout + run.stderr)
        cls.lib = c.CDLL(str(Path(temp.name) / "settingsconnectivity.dll"))
        kernel = c.WinDLL("kernel32")
        kernel.FreeLibrary.argtypes = [c.c_void_p]
        cls.addClassCleanup(lambda: kernel.FreeLibrary(cls.lib._handle))
        for name in ("test_connectivity_geometry", "test_connectivity_actions_and_redraw", "test_connectivity_real_editor", "test_all_settings_sections_fit", "test_sparse_catalog_identity_and_coordinate"):
            getattr(cls.lib,name).restype=c.c_int

    def test_real_hidden_ssid_password_and_cancel(self):
        self.assertEqual(self.lib.test_connectivity_real_editor(),0)

    def test_every_section_and_page_within_safe_area(self):
        self.assertEqual(self.lib.test_all_settings_sections_fit(),0)

    def test_catalog_identity_and_coordinate_validation(self):
        self.assertEqual(self.lib.test_sparse_catalog_identity_and_coordinate(),0)

    def test_readable_wrapping_and_full_ssids(self):
        self.assertEqual(self.lib.test_readable_wrapping_and_full_ssids(),0)

    def test_storage_status_is_real_and_bounded(self):
        self.assertEqual(self.lib.test_storage_status_is_real_and_bounded(),0)

    def test_geometry_and_minimum_targets(self):
        self.assertEqual(self.lib.test_connectivity_geometry(),0)

    def test_real_touch_and_bounded_updates(self):
        self.assertEqual(self.lib.test_connectivity_actions_and_redraw(),0)


if __name__ == "__main__":
    unittest.main()
