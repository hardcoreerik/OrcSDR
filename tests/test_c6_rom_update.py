import pathlib
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class RomUpdatePolicyTests(unittest.TestCase):
    def test_c_policy_sequence_and_bounds(self):
        compiler = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
        component = ROOT / "ports/p4/components/orcsdr_c6_rom_update"
        with tempfile.TemporaryDirectory(prefix="c6-rom-update-") as tmp:
            exe = pathlib.Path(tmp) / ("test_c6_rom_update.exe" if os.name == "nt" else "test_c6_rom_update")
            sources = [str(component / "src/orcsdr_c6_rom_update.c"),
                       str(component / "src/loader_write_policy.c"),
                       str(ROOT / "tests/test_c6_rom_update.c")]
            if not compiler and os.name == "nt":
                vc = pathlib.Path(r"C:/Program Files/Microsoft Visual Studio/18/Community/VC")
                activation = subprocess.run(
                    f'cmd.exe /d /s /c ""{vc / "Auxiliary/Build/vcvars64.bat"}" >nul && set"',
                    text=True, capture_output=True, check=True)
                env = dict(os.environ)
                for line in activation.stdout.splitlines():
                    if "=" in line:
                        key, value = line.split("=", 1)
                        if key.upper() in {"INCLUDE", "LIB", "LIBPATH", "PATH"}:
                            env[key] = value
                subprocess.run([str(vc / "Tools/MSVC/14.50.35717/bin/Hostx64/x64/cl.exe"),
                    "/nologo", "/std:c11", "/W4", "/I" + str(component / "include"),
                    "/I" + str(component / "src"), *sources, "/Fe:" + str(exe)],
                    cwd=tmp, env=env, check=True, capture_output=True, text=True)
            elif compiler:
                subprocess.run([
                compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(component / "include"),
                "-I", str(component / "src"),
                *sources, "-o", str(exe),
            ], check=True)
            else:
                self.skipTest("a C compiler is required for the host policy test")
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
