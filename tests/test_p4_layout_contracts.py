"""Compile production layout contracts for legacy and responsive viewports."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LayoutContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.env = dict(os.environ)
        cls.compiler = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
        cls.msvc = False
        if not cls.compiler and os.name == "nt":
            vc = Path(r"C:/Program Files/Microsoft Visual Studio/18/Community/VC")
            compiler = vc / "Tools/MSVC/14.50.35717/bin/Hostx64/x64/cl.exe"
            if compiler.exists():
                run = subprocess.run(
                    f'cmd.exe /d /s /c ""{vc / "Auxiliary/Build/vcvars64.bat"}" >nul && set"',
                    text=True, capture_output=True, check=True)
                for line in run.stdout.splitlines():
                    if "=" in line:
                        key, value = line.split("=", 1)
                        if key.upper() in {"PATH", "INCLUDE", "LIB", "LIBPATH"}:
                            cls.env[key] = value
                cls.compiler, cls.msvc = str(compiler), True
        if not cls.compiler:
            raise unittest.SkipTest("C++ compiler required")

    def check_contract(self, name):
        source = ROOT / "apps/orcsdr-tab5/tests" / (name + "_layout_test.cpp")
        with tempfile.TemporaryDirectory(prefix="orcsdr-layout-") as tmp:
            exe = Path(tmp) / (name + (".exe" if os.name == "nt" else ""))
            args = ([self.compiler, "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX",
                     str(source), "/Fe:" + str(exe)] if self.msvc else
                    [self.compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                     str(source), "-o", str(exe)])
            run = subprocess.run(args, cwd=tmp, env=self.env, text=True, capture_output=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            run = subprocess.run([str(exe)], text=True, capture_output=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_home(self):
        self.check_contract("home")

    def test_fm(self):
        self.check_contract("fm")

    def test_settings(self):
        self.check_contract("settings")

    def test_settings_storage(self):
        self.check_contract("settings_storage")

    def test_rf_lab(self):
        self.check_contract("rf_lab")

    def test_rf24_dashboard(self):
        self.check_contract("rf24_dashboard")

    def test_rf24_dashboard_recording(self):
        source = ROOT / "tests/rf24_dashboard_recording_fixture.cpp"
        include = ROOT / "tests/ft8_ui_stubs"
        with tempfile.TemporaryDirectory(prefix="orcsdr-rf24-recording-") as tmp:
            exe = Path(tmp) / ("rf24-recording.exe" if os.name == "nt" else "rf24-recording")
            args = ([self.compiler, "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX",
                     "/I" + str(include), str(source), "/Fe:" + str(exe)] if self.msvc else
                    [self.compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                     "-I", str(include), str(source), "-o", str(exe)])
            run = subprocess.run(args, cwd=tmp, env=self.env, text=True, capture_output=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            run = subprocess.run([str(exe)], text=True, capture_output=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_adsb_dashboard(self):
        self.check_contract("adsb_dashboard")



if __name__ == "__main__":
    unittest.main()
