"""Compile and run the focused ACARS responsive-layout contract."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AcarsResponsiveLayoutTest(unittest.TestCase):
    def test_legacy_portrait_landscape_and_touch_geometry(self):
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

        test_source = ROOT / "apps/orcsdr-tab5/tests/acars_responsive_layout_test.cpp"
        layout_source = ROOT / "apps/orcsdr-tab5/ui/acars_dashboard_layout.cpp"
        ui_source = ROOT / "apps/orcsdr-tab5/ui/acars_dashboard_ui.cpp"
        model_source = ROOT / "apps/orcsdr-tab5/ui/acars_dashboard_model.cpp"
        demo_source = ROOT / "apps/orcsdr-tab5/ui/acars_demo_data.cpp"
        core_source = ROOT / "apps/orcsdr-tab5/ui/acars_core.cpp"
        include = ROOT / "apps/orcsdr-tab5/ui"
        with tempfile.TemporaryDirectory(prefix="acars-layout-") as temp:
            exe = Path(temp) / ("acars_responsive_layout_test.exe" if os.name == "nt" else
                                "acars_responsive_layout_test")
            command = ([compiler, "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX",
                        str(test_source), str(layout_source), str(ui_source), str(model_source),
                        str(demo_source), str(core_source), "/I" + str(include), "/Fe:" + str(exe)]
                       if msvc else
                       [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-I", str(include), str(test_source), str(layout_source), str(ui_source),
                        str(model_source), str(demo_source), str(core_source), "-o", str(exe)])
            result = subprocess.run(command, cwd=temp, env=env, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
