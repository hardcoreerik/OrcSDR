"""Host-only tests: no ESP-IDF installation or serial device required."""
import importlib.util
import os
import pathlib
import subprocess
import sys
import tempfile
import types
import unittest
from unittest import mock

SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "tools" / "lilygo.py"
spec = importlib.util.spec_from_file_location("lilygo_cli", SCRIPT)
cli = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cli)


class LilygoCliTests(unittest.TestCase):
    def test_idf_script_comes_from_active_installation(self):
        with tempfile.TemporaryDirectory() as temp:
            script = pathlib.Path(temp) / "tools" / "idf.py"
            script.parent.mkdir()
            script.touch()
            with mock.patch.dict(os.environ, {"IDF_PATH": temp}):
                self.assertEqual(cli.idf_available(), str(script))

    def test_multiple_ports_require_explicit_selection(self):
        ports = [types.SimpleNamespace(device=p, vid=0x303A, pid=0x1001,
                                       description="ESP USB") for p in ("COM17", "COM32")]
        listing = types.SimpleNamespace(comports=lambda: ports)
        with mock.patch.dict(sys.modules, {"serial.tools": types.SimpleNamespace(list_ports=listing)}):
            with self.assertRaises(cli.PreflightError):
                cli.select_port(None)
        self.assertEqual(cli.select_port("COM32"), "COM32")

    def test_backup_failure_prevents_flash(self):
        with mock.patch.object(cli, "select_port", return_value="COM32"), \
             mock.patch.object(cli, "probe_chip", return_value=(1, "ESP32-P4")), \
             mock.patch.object(cli, "build"), \
             mock.patch.object(cli, "backup", side_effect=cli.PreflightError("incomplete")), \
             mock.patch.object(cli.subprocess, "run") as run, \
             mock.patch.object(sys, "argv", ["lilygo.py", "flash", "--experimental"]):
            self.assertEqual(cli.main(), 2)
            run.assert_not_called()

    def test_incomplete_backup_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp, \
             mock.patch.object(cli, "PROJECT", pathlib.Path(temp)), \
             mock.patch.object(cli.subprocess, "run"):
            with self.assertRaises(cli.PreflightError):
                cli.backup("COM32", "Detected flash size: 16MB")

    def test_project_paths_come_from_script_not_working_directory(self):
        self.assertEqual(cli.PROJECT, SCRIPT.parents[1])
        self.assertEqual(cli.BOARD_DEFAULTS, cli.PROJECT / "sdkconfig.defaults")
        self.assertTrue(cli.SHARED_DEFAULTS.name == "sdkconfig.defaults")

    def test_version_specific_build_directories(self):
        self.assertEqual(cli.build_dir(1).name, "build-lilygo-rev1")
        self.assertEqual(cli.build_dir(3).name, "build-lilygo-rev3")

    def test_revision_probe_and_no_target_guess(self):
        response = subprocess.CompletedProcess([], 0,
            "esptool.py v4.12\nChip is ESP32-P4 (revision v3.2)\n"
            "Detected flash size: 16MB\n")
        with mock.patch.object(cli.subprocess, "run", return_value=response):
            major, output = cli.probe_chip("/dev/ttyACM0")
        self.assertEqual(major, 3)
        self.assertIn("16MB", output)

    def test_unrecognized_revision_fails_closed(self):
        response = subprocess.CompletedProcess([], 0,
            "Chip is ESP32-P4 but revision unavailable\n")
        with mock.patch.object(cli.subprocess, "run", return_value=response):
            with self.assertRaises(cli.PreflightError):
                cli.probe_chip("COM42")

    def test_flash_requires_experimental_acknowledgement(self):
        with mock.patch.object(cli, "select_port", return_value="COM42"), \
             mock.patch.object(cli, "probe_chip", return_value=(1, "ESP32-P4")), \
             mock.patch.object(cli, "build") as build, \
             mock.patch.object(sys, "argv", ["lilygo.py", "flash"]):
            self.assertEqual(cli.main(), 2)
            build.assert_not_called()

    def test_build_uses_board_overlay_and_silicon_overlay_last(self):
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            app = root / "apps" / "orcsdr-lilygo-p4"
            old = root / "apps" / "orcsdr-p4-hosyond-console"
            app.mkdir(parents=True)
            old.mkdir()
            (old / "sdkconfig.defaults").write_text("CONFIG_IDF_TARGET=\"esp32p4\"\n")
            (app / "sdkconfig.defaults").write_text("CONFIG_ORCSDR_BOARD_LILYGO_T_DISPLAY_P4_AMOLED=y\n")
            (app / "sdkconfig.silicon-v3").write_text("CONFIG_ESP32P4_REV_MIN_300=y\n")
            seen = []

            def fake_run(command, **_kwargs):
                seen.append(command)
                build = app / "build-lilygo-rev3"
                build.mkdir(exist_ok=True)
                (build / "sdkconfig").write_text(
                    "CONFIG_IDF_TARGET=\"esp32p4\"\n"
                    "CONFIG_ORCSDR_BOARD_LILYGO_T_DISPLAY_P4_AMOLED=y\n"
                    "CONFIG_ESP32P4_REV_MIN_300=y\n")
                return subprocess.CompletedProcess(command, 0)

            with mock.patch.multiple(cli, PROJECT=app,
                    SHARED_DEFAULTS=old / "sdkconfig.defaults",
                    BOARD_DEFAULTS=app / "sdkconfig.defaults",
                    V3_DEFAULTS=app / "sdkconfig.silicon-v3"), \
                 mock.patch.object(cli, "idf_available", return_value="idf.py"), \
                 mock.patch.object(cli.subprocess, "run", side_effect=fake_run):
                self.assertEqual(cli.build(3).name, "build-lilygo-rev3")
            self.assertEqual(len(seen), 1)
            self.assertEqual(seen[0][:2], [sys.executable, "idf.py"])
            defaults = next(x for x in seen[0] if x.startswith("SDKCONFIG_DEFAULTS="))
            self.assertTrue(defaults.endswith(str(app / "sdkconfig.silicon-v3")))
            self.assertIn(str(app / "sdkconfig.defaults"), defaults)

    def test_backup_refuses_unknown_flash_size(self):
        with self.assertRaises(cli.PreflightError):
            cli.backup("COM42", "ESP32-P4 revision v1.3")


if __name__ == "__main__":
    unittest.main()
