import re
import tempfile
import unittest
from pathlib import Path

from tools.check_documentation_truth import run_checks


PIN = "b175dfea6782faa97e512d4a2408767c75977527"


class DocumentationTruthTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.write("apps/orcsdr-tab5/main/idf_component.yml", f"  esp_rtl_sdr:\n    version: {PIN}\n")
        self.write("apps/orcsdr-tab5/dependencies.lock", f"  esp-rtl-sdr:\n    version: {PIN}\n")
        self.write("apps/orcsdr-tab5/ui/main.cpp", "one\ntwo\nthree\n")
        self.write(
            "apps/orcsdr-tab5/ui/screen_controller.hpp",
            "enum class Id : uint8_t { none, home, fm };\n",
        )
        self.write(
            "apps/orcsdr-tab5/ui/dashboard_registry.hpp",
            "enum class Id : uint8_t { home, fm, count };\n",
        )
        self.write(".github/workflows/core.yml", "name: Core\n")
        current = f"Current esp-rtl-sdr pin: `{PIN}` (0.8.0-rc2).\n"
        self.write("PROJECT_STATUS.md", current)
        self.write("docs/API_ESP_RTL_SDR.md", current)
        self.write(
            "architecture.md",
            current
            + "main.cpp measurement (Git-normalized): 14 bytes (~0.0 KiB), 3 lines.\n"
            + "ScreenController IDs: `none`, `home`, `fm`.\n"
            + "Dashboard IDs: `home`, `fm`.\n",
        )
        self.write("README.md", "[Status](PROJECT_STATUS.md)\n")

    def tearDown(self):
        self.temp.cleanup()

    def write(self, relative, text):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def report(self):
        return run_checks(self.root)

    def test_correct_current_state_passes(self):
        self.assertEqual([], self.report().errors)

    def test_stale_current_driver_pin_fails(self):
        self.write("PROJECT_STATUS.md", "Current esp-rtl-sdr driver is v0.7.9.\n")
        self.assertTrue(any(item.code == "driver-pin" for item in self.report().errors))

    def test_historical_old_driver_version_does_not_fail(self):
        self.write("docs/history/old.md", "Historical Evidence: this snapshot used v0.7.9.\n")
        self.assertEqual([], self.report().errors)

    def test_unqualified_no_ci_claim_fails_when_workflows_exist(self):
        self.write("PROJECT_STATUS.md", "OrcSDR has no CI.\n")
        self.assertTrue(any(item.code == "no-ci" for item in self.report().errors))

    def test_precise_native_firmware_ci_gap_passes(self):
        self.write(
            "PROJECT_STATUS.md",
            f"Current esp-rtl-sdr pin: `{PIN}`. Native Tab5 firmware is not currently compiled in CI.\n",
        )
        self.assertEqual([], self.report().errors)

    def test_broken_local_markdown_link_fails(self):
        self.write("README.md", "[Missing](docs/missing.md)\n")
        self.assertTrue(any(item.code == "local-link" for item in self.report().errors))

    def test_valid_local_markdown_link_passes(self):
        self.write("README.md", "[Status](PROJECT_STATUS.md)\n")
        self.assertEqual([], self.report().errors)

    def test_small_main_cpp_measurement_drift_passes(self):
        self.write("apps/orcsdr-tab5/ui/main.cpp", "x" * 99 + "\n")
        text = (self.root / "architecture.md").read_text(encoding="utf-8")
        text = re.sub(
            r"14 bytes \(~0\.0 KiB\), 3 lines",
            "96 bytes (~0.1 KiB), 1 lines",
            text,
        )
        self.write("architecture.md", text)
        self.assertFalse(any(item.code == "main-measurement" for item in self.report().errors))

    def test_excessive_main_cpp_measurement_drift_fails(self):
        text = (self.root / "architecture.md").read_text(encoding="utf-8")
        self.write("architecture.md", text.replace("14 bytes", "7 bytes"))
        self.assertTrue(any(item.code == "main-measurement" for item in self.report().errors))

    def test_prompt_residue_warns_without_failing(self):
        self.write("README.md", "Your job is to implement the following.\n")
        report = self.report()
        self.assertEqual([], report.errors)
        self.assertTrue(any(item.code == "prompt-residue" for item in report.warnings))

    def test_history_prompt_is_not_current_prose(self):
        self.write("docs/history/prompt.md", "Historical Evidence\n\nYou are Codex. Your job is to implement the following.\n")
        report = self.report()
        self.assertEqual([], report.errors)
        self.assertFalse(any(item.code == "prompt-residue" for item in report.warnings))


if __name__ == "__main__":
    unittest.main()
