"""Compile and run the focused P25 layout contract."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class P25DashboardLayoutTest(unittest.TestCase):
    def test_native_layout_geometry(self):
        self.compile_run("layout")

    def test_production_renderer_input_and_bounded_updates(self):
        self.compile_run("recording")

    def compile_run(self, test):
        compiler = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
        msvc = False
        env = dict(os.environ)
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
        with tempfile.TemporaryDirectory(prefix="p25-layout-") as temp:
            exe = Path(temp) / ("p25_dashboard_" + test)
            sources = ([ROOT / "apps/orcsdr-tab5/tests/p25_dashboard_layout_test.cpp"] if test == "layout" else
                       [ROOT / "tests/p25_dashboard_recording_fixture.cpp",
                        ROOT / "apps/orcsdr-tab5/ui/p25_dashboard.cpp"])
            command = ([compiler, "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX"] if msvc else
                       [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror"])
            if test == "recording":
                command.extend([("/I" if msvc else "-I") + str(ROOT),
                                ("/I" if msvc else "-I") + str(ROOT / "tests/p25_ui_stubs")])
            command.extend(map(str, sources))
            command.append(("/Fe:" if msvc else "-o") + str(exe))
            result = subprocess.run(command, cwd=temp, env=env, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
