"""Configure-only checks: no firmware compilation, device access or managed edits."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HOSTED = ROOT / "apps/orcsdr-p4-hosyond-console/managed_components/espressif__esp_hosted"
ADAPTER = ROOT / "ports/p4/hosted/board_reset.cmake"
SOURCES = {
    "eh_host_port_idf": HOSTED / "port/os/idf/src/eh_host_port_power.c",
    "eh_host_mcu_transport": HOSTED / "host/mcu/eh_host_mcu_transport/src/eh_host_bus_sdio.c",
}


class BoardResetConfigureTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.env = dict(os.environ)
        if os.name == "nt":
            batch = Path(r"C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat")
            run = subprocess.run(f'cmd.exe /d /s /c ""{batch}" >nul && set"', capture_output=True, text=True, check=True)
            for line in run.stdout.splitlines():
                if "=" in line:
                    key, value = line.split("=", 1)
                    if key.upper() in {"PATH", "INCLUDE", "LIB", "LIBPATH"}:
                        cls.env[key] = value
    def configure(self, folder, enabled=True, drift=False):
        declarations = []
        for target, original in SOURCES.items():
            source = folder / original.name
            shutil.copyfile(original, source)
            if drift and target == "eh_host_port_idf":
                source.write_bytes(source.read_bytes() + b"\n/* drift */\n")
            declarations.append(f'add_library({target} STATIC)\n'
                                f'set_property(TARGET {target} PROPERTY SOURCES "{source.as_posix()}")')
        (folder / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.19)\nproject(reset_check C)\n'
            'add_library(idf::orcsdr_board_hosyond INTERFACE IMPORTED)\n'
            f'set(CONFIG_ORCSDR_BOARD_LILYGO_T_DISPLAY_P4_AMOLED {"ON" if enabled else "OFF"})\n'
            'set(CONFIG_ESP_HOSTED_HOST_RESET_GPIO -1)\n' + '\n'.join(declarations) +
            f'\ninclude("{ADAPTER.as_posix()}")\n'
            'foreach(t eh_host_port_idf eh_host_mcu_transport)\n'
            'get_target_property(s ${t} SOURCES)\n'
            'file(WRITE "${CMAKE_BINARY_DIR}/${t}.sources" "${s}")\n'
            'endforeach()\n', encoding="utf-8")
        return subprocess.run(["cmake", "-G", "Ninja", "-S", str(folder), "-B", str(folder / "build")],
                              capture_output=True, text=True, env=self.env)

    def test_lilygo_uses_real_reset_without_managed_edits(self):
        before = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in SOURCES.values()}
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            result = self.configure(folder)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            generated = folder / "build/orcsdr_hosted_board_reset"
            power = (generated / "eh_host_port_power.c").read_text()
            sdio = (generated / "eh_host_bus_sdio.c").read_text()
            self.assertIn("return orcsdr_hosted_board_reset();", power)
            self.assertNotIn("skipping slave reset", power)
            self.assertNotIn("assert(reset_pin.pin != -1)", sdio)
            self.assertIn("!orcsdr_hosted_board_reset_available()", sdio)
            self.assertEqual(sdio.count("if (transport_gpio_reset(bus_handle, reset_pin) != ESP_OK) return -1;"), 2)
            for target, original in SOURCES.items():
                sources = (folder / f"build/{target}.sources").read_text().split(";")
                self.assertIn((generated / original.name).as_posix(), sources)
                self.assertNotIn((folder / original.name).as_posix(), sources)
        self.assertEqual(before, {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in SOURCES.values()})

    def test_other_profiles_keep_upstream_sources(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            result = self.configure(folder, enabled=False)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertFalse((folder / "build/orcsdr_hosted_board_reset").exists())
            for target, original in SOURCES.items():
                self.assertEqual((folder / f"build/{target}.sources").read_text(),
                                 (folder / original.name).as_posix())

    def test_upstream_drift_stops_configuration(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = self.configure(Path(tmp), drift=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("ESP-Hosted reset source changed", result.stderr)


if __name__ == "__main__":
    unittest.main()
