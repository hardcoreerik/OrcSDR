#!/usr/bin/env python3
"""Host regression test for OrcSDR's firmware-native support report.

No ESP-IDF, hardware, credentials or serial port needed.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HEADER_DIR = ROOT / "apps" / "orcsdr-tab5" / "ui"
MAIN_CPP = HEADER_DIR / "main.cpp"
SERIAL_SUPPORT_CPP = HEADER_DIR / "serial_support.cpp"
SERIAL_HELP_CPP = HEADER_DIR / "serial_help.cpp"

serial_support_src = SERIAL_SUPPORT_CPP.read_text(encoding="utf-8")
assert 'if (strcmp(command, "RTL_SUPPORT REPORT") == 0)' in serial_support_src
assert "orcsdr::support_report::emit(" in serial_support_src

main_src = MAIN_CPP.read_text(encoding="utf-8")
assert "orcsdr::serial_support::handle_command(" in main_src
assert "collect_rtl_support_snapshot" in main_src

help_src = SERIAL_HELP_CPP.read_text(encoding="utf-8")
assert "RTL_SUPPORT REPORT            - read-only" in help_src

cxx = os.environ.get("CXX") or shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
assert cxx, "A C++17 compiler is required to validate the pure formatter"
harness = r"""
#include <iostream>
#include <string>
#include <vector>
#include "serial_support.hpp"
static std::vector<std::string> lines;
static void line_out(const char* line) { lines.emplace_back(line); }
static orcsdr::support_report::Snapshot make_snapshot() {
  orcsdr::support_report::Snapshot s{};
  s.firmware = "v0.6.0";
  s.build = "nightly abc123";
  s.driver = "0.9.6";
  s.profile = "nooelec_smart_v5";
  s.driver_state = "FAULT";
  s.last_error = "ESP_RTL_SDR_ERR_FAULT";
  s.capture_state = "stopped";
  s.band = "FM";
  s.usb_speed = "high";
  s.receiver_ready = true;
  s.driver_installed = true;
  s.metrics_ok = true;
  s.vid = 0x0bda;
  s.pid = 0x2838;
  s.frequency_hz = 101100000;
  s.target_sps = 960000;
  s.effective_sps = 0;
  s.bytes_total = 1024;
  s.short_transfers = 3;
  s.usb_overruns = 2;
  s.consumer_drops = 5;
  s.uptime_ms = 123456;
  s.reset_reason = 7;
  s.heap_free = 12345;
  s.dma_free = 2048;
  s.dma_largest = 1024;
  s.psram_free = 23456;
  return s;
}
int main() {
  const orcsdr::serial_support::Hooks hooks{line_out, make_snapshot};
  if (!orcsdr::serial_support::handle_command("RTL_SUPPORT REPORT", false, hooks)) return 2;
  for (const auto& line : lines) std::cout << line << '\n';
  return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="orc-support-") as directory:
    cpp = Path(directory) / "report_test.cpp"
    exe = Path(directory) / ("report_test.exe" if os.name == "nt" else "report_test")
    cpp.write_text(harness, encoding="utf-8")
    subprocess.run(
        [
            cxx,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(HEADER_DIR),
            str(cpp),
            str(SERIAL_SUPPORT_CPP),
            "-o",
            str(exe),
        ],
        check=True,
    )
    result = subprocess.run([str(exe)], capture_output=True, text=True, check=True)
    lines = result.stdout.splitlines()

assert len(lines) == 8, lines
assert lines[0].startswith("ORC_SUPPORT_BEGIN schema=1 read_only=1"), lines
assert lines[-1] == "ORC_SUPPORT_END", lines
assert 'firmware="v0.6.0"' in lines[1]
assert 'build="nightly abc123"' in lines[1]
assert 'vid=0bda pid=2838' in lines[2]
assert 'profile="nooelec_smart_v5"' in lines[3]
assert 'last_error="ESP_RTL_SDR_ERR_FAULT"' in lines[3]
assert 'capture="stopped"' in lines[4]
assert "frequency_hz=101100000" in lines[4]
assert "metrics_ok=1 target_sps=960000 effective_sps=0" in lines[5]
assert "short_transfers=3 overruns=2 consumer_drops=5" in lines[5]
assert "uptime_ms=123456 reset_reason=7" in lines[6]
assert all(len(line) < 320 for line in lines)
for forbidden in ("SSID", "password", "token=", "ip_addr", "mac=", "serial=",
                  "latitude", "longitude", "pair_key"):
    assert forbidden.lower() not in result.stdout.lower(), forbidden
print("PASS: support-report command hooked, schema, metrics, boundaries, and privacy")
