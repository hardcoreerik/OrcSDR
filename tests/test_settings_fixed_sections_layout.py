"""Compile the shared Radio Defaults and Companion layout contracts."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class FixedSettingsSectionsLayoutTest(unittest.TestCase):
    def test_responsive_and_legacy_geometry(self):
        tools = Path(r"C:/Program Files/Microsoft Visual Studio/18/Community/VC")
        batch = tools / "Auxiliary/Build/vcvars64.bat"
        compiler = tools / "Tools/MSVC/14.50.35717/bin/Hostx64/x64/cl.exe"
        if not batch.exists() or not compiler.exists():
            self.skipTest("MSVC host compiler is unavailable")

        environment = dict(os.environ)
        setup = subprocess.run(
            f'cmd.exe /d /s /c ""{batch}" >nul && set"',
            text=True, capture_output=True, check=True)
        for line in setup.stdout.splitlines():
            if "=" in line:
                key, value = line.split("=", 1)
                if key.upper() in {"PATH", "INCLUDE", "LIB", "LIBPATH"}:
                    environment[key] = value

        source = ROOT / "apps/orcsdr-tab5/tests/settings_fixed_sections_layout_test.cpp"
        with tempfile.TemporaryDirectory(prefix="settings-sections-layout-") as temp:
            exe = Path(temp) / "settings_fixed_sections_layout_test.exe"
            command = [str(compiler), "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX",
                       str(source), "/Fe:" + str(exe)]
            compiled = subprocess.run(command, cwd=temp, env=environment,
                                      text=True, capture_output=True)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            run = subprocess.run([str(exe)], cwd=temp, text=True, capture_output=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
