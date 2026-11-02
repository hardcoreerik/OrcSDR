"""Compile real Settings clock rendering/input against a recording display."""
import ctypes as c
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SettingsC6RomRecordingTest(unittest.TestCase):
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
            str(ROOT / "tests/settings_c6_rom_recording_fixture.cpp"),
            str(ui / "clock_settings.cpp"),
            "/link", "/OUT:settingsc6rom.dll"], cwd=temp.name, env=env,
            text=True, capture_output=True)
        if run.returncode:
            raise AssertionError(run.stdout + run.stderr)
        cls.lib = c.CDLL(str(Path(temp.name) / "settingsc6rom.dll"))
        kernel = c.WinDLL("kernel32")
        kernel.FreeLibrary.argtypes = [c.c_void_p]
        cls.addClassCleanup(lambda: kernel.FreeLibrary(cls.lib._handle))
        for name in ("test_rom_workflow_actions_and_locks","test_rom_progress_is_bounded_and_visible","test_hosted_method_unchanged_and_missing_image_rejects"):
            getattr(cls.lib,name).restype=c.c_int

    def test_completed_and_current_hide_update(self):
        self.assertEqual(self.lib.test_completed_and_current_hide_update(),0)

    def test_actions_and_rotation_lock(self):
        self.assertEqual(self.lib.test_rom_workflow_actions_and_locks(),0)

    def test_progress_updates_only_status_region(self):
        self.assertEqual(self.lib.test_rom_progress_is_bounded_and_visible(),0)

    def test_other_profiles_keep_hosted_method(self):
        self.assertEqual(self.lib.test_hosted_method_unchanged_and_missing_image_rejects(),0)


if __name__ == "__main__":
    unittest.main()
