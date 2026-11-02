"""Compile and run the shared text editor against a recording display."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TextEditorLayoutTest(unittest.TestCase):
    def test_recorded_layout_touch_and_hardware_edit(self):
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

        ui = ROOT / "apps/orcsdr-tab5/ui"
        with tempfile.TemporaryDirectory(prefix="text-editor-layout-") as temp:
            exe = Path(temp) / "text_editor_render_tests.exe"
            command = [
                str(compiler), "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX",
                "/I" + str(ROOT / "tests/text_editor_stubs"),
                "/I" + str(ui),
                str(ROOT / "tests/text_editor_render_tests.cpp"),
                str(ui / "text_editor.cpp"),
                str(ui / "keyboard_input.cpp"),
                "/Fe:" + str(exe),
            ]
            compiled = subprocess.run(command, cwd=temp, env=environment,
                                      text=True, capture_output=True)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            run = subprocess.run([str(exe)], cwd=temp, text=True, capture_output=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
