"""Compile and run focused CB responsive geometry and source contracts."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class CbDashboardLayoutTest(unittest.TestCase):
    def test_native_layout_geometry(self):
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
        src = ROOT / "apps/orcsdr-tab5/tests/cb_dashboard_layout_test.cpp"
        with tempfile.TemporaryDirectory(prefix="cb-layout-") as temp:
            exe = Path(temp) / "cb_dashboard_layout_test"
            command = ([compiler,"/nologo","/std:c++17","/EHsc","/W4","/WX",
                        str(src),"/Fe:"+str(exe)] if msvc else
                       [compiler,"-std=c++17","-Wall","-Wextra","-Werror",
                        str(src),"-o",str(exe)])
            result = subprocess.run(command, cwd=temp, env=env, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
    def test_listen_updates_do_not_clear_or_redraw_the_screen(self):
        source = (ROOT / "apps/orcsdr-tab5/ui/cb_dashboard.cpp").read_text(encoding="utf-8")
        update = source.split("void update(const Snapshot& snapshot) {", 1)[1].split(
            "void draw_spectrum", 1)[0]
        self.assertNotIn("fillScreen", update)
        self.assertNotIn("draw();", update)
if __name__ == "__main__":
    unittest.main()
