"""Compile the focused responsive System and clock editor geometry contracts."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SettingsSystemClockLayoutTest(unittest.TestCase):
    def test_system_and_clock_rectangles(self):
        compiler = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
        environment = dict(os.environ)
        msvc = False
        if not compiler and os.name == "nt":
            vc = Path(r"C:/Program Files/Microsoft Visual Studio/18/Community/VC")
            batch = vc / "Auxiliary/Build/vcvars64.bat"
            candidate = vc / "Tools/MSVC/14.50.35717/bin/Hostx64/x64/cl.exe"
            if candidate.exists() and batch.exists():
                setup = subprocess.run(
                    f'cmd.exe /d /s /c ""{batch}" >nul && set"',
                    text=True, capture_output=True, check=True)
                for line in setup.stdout.splitlines():
                    if "=" in line:
                        key, value = line.split("=", 1)
                        if key.upper() in {"PATH", "INCLUDE", "LIB", "LIBPATH"}:
                            environment[key] = value
                compiler, msvc = str(candidate), True
        if not compiler:
            self.skipTest("A C++ host compiler is unavailable")

        source = ROOT / "apps/orcsdr-tab5/tests/settings_system_clock_layout_test.cpp"
        with tempfile.TemporaryDirectory(prefix="settings-system-clock-layout-") as temp:
            exe = Path(temp) / ("settings_system_clock_layout_test.exe" if os.name == "nt"
                                else "settings_system_clock_layout_test")
            command = ([compiler, "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX",
                        str(source), "/Fe:" + str(exe)] if msvc else
                       [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        str(source), "-o", str(exe)])
            compiled = subprocess.run(command, cwd=temp, env=environment,
                                      text=True, capture_output=True)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            run = subprocess.run([str(exe)], cwd=temp, text=True, capture_output=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
