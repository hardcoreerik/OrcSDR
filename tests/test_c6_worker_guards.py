"""Compile and exercise the real C6 recovery API with hardware-free stubs."""
import ctypes as c
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class C6RecoveryGuardTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(temp.cleanup)
        tools = Path(r"C:/Program Files/Microsoft Visual Studio/18/Community/VC")
        batch = tools / "Auxiliary/Build/vcvars64.bat"
        result = subprocess.run(f'cmd.exe /d /s /c ""{batch}" >nul && set"',
                                text=True, capture_output=True)
        if result.returncode:
            raise AssertionError("MSVC developer environment activation failed")
        env = dict(os.environ)
        for line in result.stdout.splitlines():
            if "=" in line:
                key, value = line.split("=", 1)
                if key.upper() in {"INCLUDE", "LIB", "LIBPATH", "PATH"}:
                    env[key] = value
        cl = tools / "Tools/MSVC/14.50.35717/bin/Hostx64/x64/cl.exe"
        run = subprocess.run([
            str(cl), "/nologo", "/std:c++17", "/EHsc", "/MD", "/LD",
            "/I" + str(ROOT / "tests/c6_worker_stubs"),
            "/I" + str(ROOT / "ports/p4/components/orcsdr_c6_rom_update/include"),
            "/I" + str(ROOT / "apps/orcsdr-tab5/ui"),
            "/I" + str(ROOT / "apps/orcsdr-p4-hosyond-console/main"),
            str(ROOT / "tests/c6_worker_guard_fixture.cpp"),
            "/link", "/OUT:c6guards.dll"], cwd=temp.name, env=env,
            text=True, capture_output=True)
        if run.returncode:
            raise AssertionError(run.stdout + run.stderr)
        cls.lib = c.CDLL(str(Path(temp.name) / "c6guards.dll"))
        kernel = c.WinDLL("kernel32")
        kernel.FreeLibrary.argtypes = [c.c_void_p]
        cls.addClassCleanup(lambda: kernel.FreeLibrary(cls.lib._handle))
        for name in ("check_prerequisites", "check_task_failure", "check_release_guard",
                     "check_duplicate_cancel", "check_cancel_after_write"):
            getattr(cls.lib, name).restype = c.c_int

    def test_begin_requires_package_qualified_profile_and_idle_hosted_ota(self):
        self.assertEqual(self.lib.check_prerequisites(), 0)

    def test_failed_task_creation_releases_busy_guard(self):
        self.assertEqual(self.lib.check_task_failure(), 0)

    def test_release_confirmation_requires_active_release_phase(self):
        self.assertEqual(self.lib.check_release_guard(), 0)

    def test_duplicate_begin_and_cancel_are_guarded(self):
        self.assertEqual(self.lib.check_duplicate_cancel(), 0)

    def test_cancel_is_refused_after_writing_begins(self):
        self.assertEqual(self.lib.check_cancel_after_write(), 0)


if __name__ == "__main__":
    unittest.main()
