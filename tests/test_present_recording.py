"""Compile the real presenter and geometry mapper against a recording board/display."""
import ctypes as c
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PresentRecordingTest(unittest.TestCase):
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
        run = subprocess.run([
            str(cl), "/nologo", "/std:c++17", "/EHsc", "/MD", "/LD",
            "/I" + str(ROOT / "tests/present_host_stubs"),
            "/I" + str(ROOT / "apps/orcsdr-p4-hosyond-console/main"),
            "/I" + str(ROOT / "ports/p4/components/orcsdr_gfx_compat/include"),
            "/I" + str(ROOT / "components/orcsdr_display_geometry/include"),
            str(ROOT / "apps/orcsdr-p4-hosyond-console/main/present.cpp"),
            str(ROOT / "tests/present_recording_fixture.cpp"),
            str(ROOT / "components/orcsdr_display_geometry/orcsdr_display_geometry.c"),
            "/link", "/OUT:presentrecording.dll"], cwd=temp.name, env=env,
            text=True, capture_output=True)
        if run.returncode:
            raise AssertionError(run.stdout + run.stderr)
        cls.lib = c.CDLL(str(Path(temp.name) / "presentrecording.dll"))
        kernel = c.WinDLL("kernel32")
        kernel.FreeLibrary.argtypes = [c.c_void_p]
        cls.addClassCleanup(lambda: kernel.FreeLibrary(cls.lib._handle))
        for name in ("test_disjoint_incremental_ranges_and_mirror",
                     "test_rotation_maps_native_pixels",
                     "test_headless_drains_ranges_without_flip"):
            getattr(cls.lib, name).restype = c.c_int

    def test_disjoint_ranges_flip_once_mirror_buffers_and_preserve_untouched_rows(self):
        self.assertEqual(self.lib.test_disjoint_incremental_ranges_and_mirror(), 0)

    def test_all_rotations_map_view_pixels_to_native_framebuffers(self):
        self.assertEqual(self.lib.test_rotation_maps_native_pixels(), 0)

    def test_headless_drains_every_range_without_flipping(self):
        self.assertEqual(self.lib.test_headless_drains_ranges_without_flip(), 0)


if __name__ == "__main__":
    unittest.main()
