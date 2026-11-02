"""Run the real recovery worker with deterministic, non-hardware dependencies."""
import ctypes as c
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class C6WorkerCleanupTest(unittest.TestCase):
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
            "/I" + str(ROOT / "tests/c6_worker_cleanup_stubs"),
            "/I" + str(ROOT / "tests/c6_worker_stubs"),
            "/I" + str(ROOT / "ports/p4/components/orcsdr_c6_rom_update/include"),
            "/I" + str(ROOT / "apps/orcsdr-tab5/ui"),
            "/I" + str(ROOT / "apps/orcsdr-p4-hosyond-console/main"),
            str(ROOT / "tests/c6_worker_cleanup_fixture.cpp"),
            "/link", "/OUT:c6cleanup.dll"], cwd=temp.name, env=env,
            text=True, capture_output=True)
        if run.returncode:
            raise AssertionError(run.stdout + run.stderr)
        cls.lib = c.CDLL(str(Path(temp.name) / "c6cleanup.dll"))
        kernel = c.WinDLL("kernel32")
        kernel.FreeLibrary.argtypes = [c.c_void_p]
        cls.addClassCleanup(lambda: kernel.FreeLibrary(cls.lib._handle))
        cls.lib.run_prepare_failure_cleanup.restype = c.c_int
        cls.lib.run_disconnect_ack_race.restype = c.c_int
        cls.lib.run_install_success.restype = c.c_int
        cls.lib.run_stale_generation_rejected.restype = c.c_int
        cls.lib.run_reset_failure_cleanup.restype = c.c_int

    def test_prepare_failure_waits_for_release_and_retains_ownership_until_idle(self):
        self.assertEqual(self.lib.run_prepare_failure_cleanup(), 0)

    def test_disconnect_probe_ack_after_confirmation_timeout_is_not_lost(self):
        self.assertEqual(self.lib.run_disconnect_ack_race(), 0)

    def test_mock_install_requires_new_link_generation_and_exact_version(self):
        self.assertEqual(self.lib.run_install_success(), 0)

    def test_exact_version_with_stale_generation_is_rejected_after_deadline(self):
        self.assertEqual(self.lib.run_stale_generation_rejected(), 0)

    def test_reset_failure_overrides_prior_error_and_releases_guards(self):
        self.assertEqual(self.lib.run_reset_failure_cleanup(), 0)


if __name__ == "__main__":
    unittest.main()
