"""Compile and run focused POCSAG dashboard geometry tests."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PocsagDashboardLayoutTest(unittest.TestCase):
    def test_native_layout_geometry(self):
        self.compile_run(ROOT / "apps/orcsdr-tab5/tests/pocsag_dashboard_layout_test.cpp")

    def test_production_renderer_input_and_bounded_updates(self):
        self.compile_run(ROOT / "tests/pocsag_dashboard_recording_fixture.cpp", recording=True)

    def compile_run(self, src, recording=False):
        compiler = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
        env = dict(os.environ)
        msvc = False
        if not compiler and os.name == "nt":
            vc = Path(r"C:/Program Files/Microsoft Visual Studio/18/Community/VC")
            candidate = vc / "Tools/MSVC/14.50.35717/bin/Hostx64/x64/cl.exe"
            if candidate.exists():
                setup = subprocess.run(
                    f'cmd.exe /d /s /c ""{vc / "Auxiliary/Build/vcvars64.bat"}" >nul && set"',
                    text=True, capture_output=True, check=True)
                for line in setup.stdout.splitlines():
                    if "=" in line:
                        key, value = line.split("=", 1)
                        if key.upper() in {"PATH", "INCLUDE", "LIB", "LIBPATH"}:
                            env[key] = value
                compiler, msvc = str(candidate), True
        if not compiler:
            self.skipTest("C++ compiler required")
        with tempfile.TemporaryDirectory(prefix="pocsag-layout-") as temp:
            exe = Path(temp) / "pocsag_dashboard_layout_test"
            command = ([compiler, "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX",
                        str(src), "/Fe:" + str(exe)] if msvc else
                       [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        str(src), "-o", str(exe)])
            if recording:
                command.insert(1, ("/I" if msvc else "-I") + str(ROOT / "tests/ft8_ui_stubs"))
                command.insert(2,str(ROOT / "apps/orcsdr-tab5/ui/focus_nav.cpp"))
            result = subprocess.run(command, cwd=temp, env=env, text=True,
                                    capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
