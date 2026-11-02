"""Compile and exercise the fixed-capacity dirty-row interval tracker."""
import ctypes as c
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DirtyRowsTest(unittest.TestCase):
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
        include = ROOT / "ports/p4/components/orcsdr_gfx_compat/include"
        run = subprocess.run([
            str(cl), "/nologo", "/std:c++17", "/EHsc", "/MD", "/LD",
            "/I" + str(include), str(ROOT / "tests/dirty_rows_host_test.cpp"),
            "/link", "/OUT:dirtyrows.dll"], cwd=temp.name, env=env,
            text=True, capture_output=True)
        if run.returncode:
            raise AssertionError(run.stdout + run.stderr)
        cls.lib = c.CDLL(str(Path(temp.name) / "dirtyrows.dll"))
        kernel = c.WinDLL("kernel32")
        kernel.FreeLibrary.argtypes = [c.c_void_p]
        cls.addClassCleanup(lambda: kernel.FreeLibrary(cls.lib._handle))
        for name in ("test_disjoint_plot_and_footer_ranges",
                     "test_overlap_adjacency_clipping_and_empty",
                     "test_clear_and_orientation_reset",
                     "test_capacity_overflow_preserves_coverage",
                     "test_randomized_bitmap_coverage"):
            getattr(cls.lib, name).restype = c.c_int

    def test_disjoint_plot_and_footer_ranges_drain_separately(self):
        self.assertEqual(self.lib.test_disjoint_plot_and_footer_ranges(), 0)

    def test_overlap_adjacency_clipping_and_empty_intervals(self):
        self.assertEqual(self.lib.test_overlap_adjacency_clipping_and_empty(), 0)

    def test_clear_and_orientation_reset_drop_old_ranges(self):
        self.assertEqual(self.lib.test_clear_and_orientation_reset(), 0)

    def test_capacity_overflow_preserves_changed_rows(self):
        self.assertEqual(self.lib.test_capacity_overflow_preserves_coverage(), 0)

    def test_deterministic_randomized_requests_are_all_covered(self):
        self.assertEqual(self.lib.test_randomized_bitmap_coverage(), 0)


if __name__ == "__main__":
    unittest.main()
