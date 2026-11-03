"""Compile focused Shortwave geometry and real renderer/input recording checks."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def compiler_setup():
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
    return compiler, env, msvc


class ShortwaveDashboardTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler, cls.env, cls.msvc = compiler_setup()
        if not cls.compiler:
            raise unittest.SkipTest("C++ compiler required")

    def compile_run(self, source, recording=False):
        ui = ROOT / "apps/orcsdr-tab5/ui"
        stub = ROOT / "tests/fixtures/shortwave_dashboard_stubs"
        sources = [source]
        if recording:
            sources += [ui / "focus_nav.cpp", ui / "receiver_tuning_controls.cpp",
                        ui / "shortwave_model.cpp", ui / "shortwave_dashboard_state.cpp",
                        ui / "waterfall_style.cpp"]
        with tempfile.TemporaryDirectory(prefix="shortwave-layout-") as temp:
            exe = Path(temp) / "shortwave_dashboard_test.exe"
            if self.msvc:
                command = [self.compiler, "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX",
                           "/D_CRT_SECURE_NO_WARNINGS",
                           "/I" + str(stub), "/I" + str(ui)] + [str(s) for s in sources] + ["/Fe:" + str(exe)]
            else:
                command = [self.compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                           "-I" + str(stub), "-I" + str(ui)] + [str(s) for s in sources] + ["-o", str(exe)]
            run = subprocess.run(command, cwd=temp, env=self.env, text=True, capture_output=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            run = subprocess.run([str(exe)], cwd=temp, env=self.env, text=True, capture_output=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_geometry(self):
        self.compile_run(ROOT / "apps/orcsdr-tab5/tests/shortwave_dashboard_layout_test.cpp")

    def test_production_renderer_input_and_bounded_updates(self):
        self.compile_run(ROOT / "tests/fixtures/shortwave_dashboard_recording_fixture.cpp", recording=True)


if __name__ == "__main__":
    unittest.main()
