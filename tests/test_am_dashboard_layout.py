"""Compile and run the focused AM responsive layout contract."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AmDashboardLayoutTest(unittest.TestCase):
    def test_listen_data_update_is_widget_bounded(self):
        source = (ROOT / "apps/orcsdr-tab5/ui/am_dashboard.cpp").read_text(encoding="utf-8")
        update = source.split("void update(const Snapshot& snapshot) {", 1)[1].split(
            "scope::FrameStats", 1)[0]
        self.assertNotIn("fillScreen", update)
        self.assertIn("draw_dynamic();", update)
        self.assertIn("tab_rect(g_layout, i)", source)

    def test_dynamic_clears_preserve_static_titles_and_spectrum_is_clipped(self):
        source = (ROOT / "apps/orcsdr-tab5/ui/am_dashboard.cpp").read_text(encoding="utf-8")
        listen = source.split("void draw_listen_dynamic() {", 1)[1].split(
            "void draw_finder_static()", 1)[0]
        spectrum = source.split("void draw_spectrum_dynamic() {", 1)[1].split(
            "void draw_settings_static()", 1)[0]
        self.assertIn("fillRect(g_layout.frequency_value.x", listen)
        self.assertNotIn("fillRect(g_layout.frequency_label.x", listen)
        self.assertIn("fillRect(g_layout.spectrum_readout.x", spectrum)
        self.assertNotIn("fillRect(g_layout.spectrum_title.x", spectrum)
        self.assertIn("ViewportClip clip(g_layout.viewport);", spectrum)

    def test_entry_restores_saved_am_station_when_home_frequency_is_outside_band(self):
        adapter = (ROOT / "apps/orcsdr-p4-hosyond-console/main/screen_am.cpp").read_text(
            encoding="utf-8")
        self.assertIn("st.frequency_hz < am_band.min_hz || st.frequency_hz > am_band.max_hz",
                      adapter)
        self.assertIn("am::saved_frequency() >= am_band.min_hz", adapter)
        self.assertIn("am::saved_frequency() <= am_band.max_hz", adapter)

    def test_spectrum_scroll_rect_lifecycle_has_no_frame_time_reset(self):
        source = (ROOT / "apps/orcsdr-tab5/ui/am_dashboard.cpp").read_text(encoding="utf-8")
        view = source.split("void draw_view() {", 1)[1].split("void draw_keypad()", 1)[0]
        spectrum_static = source.split("void draw_spectrum_static() {", 1)[1].split(
            "void draw_spectrum_dynamic()", 1)[0]
        spectrum_frame = source.split("void draw_spectrum(const float* levels", 1)[1].split(
            "uint32_t spectrum_fps()", 1)[0]
        update = source.split("void update(const Snapshot& snapshot) {", 1)[1].split(
            "scope::FrameStats", 1)[0]
        self.assertLess(view.index("clearScrollRect()"), view.index("draw_spectrum_static()"))
        self.assertIn("setScrollRect(water.x + 1", spectrum_static)
        self.assertIn("scope::scroll_waterfall(", spectrum_frame)
        self.assertNotIn("clearScrollRect", spectrum_frame)
        self.assertNotIn("setScrollRect", spectrum_frame)
        self.assertNotIn("clearScrollRect", update)
        scope = (ROOT / "apps/orcsdr-tab5/ui/scope_canvas.hpp").read_text(encoding="utf-8")
        scroll = scope.split("inline void scroll_waterfall(", 1)[1].split("// Waterfall palette", 1)[0]
        self.assertIn("M5.Display.scroll(0, -rows)", scroll)
        self.assertNotIn("clearScrollRect", scroll)

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
        source = ROOT / "apps/orcsdr-tab5/tests/am_dashboard_layout_test.cpp"
        with tempfile.TemporaryDirectory(prefix="am-layout-") as temp:
            exe = Path(temp) / ("am_dashboard_layout_test.exe" if os.name == "nt" else
                                "am_dashboard_layout_test")
            command = ([compiler, "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX",
                        str(source), "/Fe:" + str(exe)] if msvc else
                       [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        str(source), "-o", str(exe)])
            result = subprocess.run(command, cwd=temp, env=env, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
