"""Compile real Settings clock rendering/input against a recording display."""
import ctypes as c
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SettingsClockRecordingTest(unittest.TestCase):
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
            str(ROOT / "tests/settings_clock_recording_fixture.cpp"),
            str(ui / "clock_settings.cpp"),
            "/link", "/OUT:settingsclock.dll"], cwd=temp.name, env=env,
            text=True, capture_output=True)
        if run.returncode:
            raise AssertionError(run.stdout + run.stderr)
        cls.lib = c.CDLL(str(Path(temp.name) / "settingsclock.dll"))
        kernel = c.WinDLL("kernel32")
        kernel.FreeLibrary.argtypes = [c.c_void_p]
        cls.addClassCleanup(lambda: kernel.FreeLibrary(cls.lib._handle))
        for name in ("test_compact_minus_keeps_clock_editor",
                     "test_month_clamp_repaints_only_affected_fields",
                     "test_cancel_discards_clock_draft",
                     "test_offset_only_save_emits_offset_action",
                     "test_invalid_dirty_clock_cannot_save", "test_titles_stay_below_navigation"):
            getattr(cls.lib, name).restype = c.c_int

    def test_titles_do_not_bleed_into_navigation_gap(self):
        self.assertEqual(self.lib.test_titles_stay_below_navigation(),0)

    def test_compact_minus_left_of_legacy_rail_keeps_editor_active(self):
        self.assertEqual(self.lib.test_compact_minus_keeps_clock_editor(), 0)

    def test_month_clamp_repaints_month_and_day_with_bounded_fills(self):
        self.assertEqual(self.lib.test_month_clamp_repaints_only_affected_fields(), 0)

    def test_cancel_discards_unsaved_calendar_draft(self):
        self.assertEqual(self.lib.test_cancel_discards_clock_draft(), 0)

    def test_offset_only_save_emits_offset_action(self):
        self.assertEqual(self.lib.test_offset_only_save_emits_offset_action(), 0)

    def test_invalid_dirty_clock_remains_open_without_save_action(self):
        self.assertEqual(self.lib.test_invalid_dirty_clock_cannot_save(), 0)


if __name__ == "__main__":
    unittest.main()
