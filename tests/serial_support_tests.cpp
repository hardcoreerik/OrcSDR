// Host test for RTL_SUPPORT REPORT (serial_support.cpp + support_report.hpp).
// Build from the repository root with g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui:
// tests/serial_support_tests.cpp apps/orcsdr-tab5/ui/serial_support.cpp
#include "serial_support.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

using namespace orcsdr::serial_support;
using orcsdr::support_report::Snapshot;

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

std::vector<std::string> g_out;
int g_snapshots = 0;
Snapshot g_next_snapshot{};

void capture_line(const char* line) { g_out.emplace_back(line); }

Snapshot next_snapshot() {
  ++g_snapshots;
  return g_next_snapshot;
}

Hooks make_hooks() {
  Hooks hooks;
  hooks.write_line = capture_line;
  hooks.snapshot = next_snapshot;
  return hooks;
}

bool run(const char* command, bool authenticated = false) {
  g_out.clear();
  g_snapshots = 0;
  return handle_command(command, authenticated, make_hooks());
}

std::string lowercase(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}

void check_common_invariants(const std::vector<std::string>& lines) {
  CHECK(lines.size() == 8);
  if (lines.size() != 8) return;
  CHECK(lines[0] == "ORC_SUPPORT_BEGIN schema=1 read_only=1 private_fields=excluded");
  CHECK(lines[7] == "ORC_SUPPORT_END");
  std::string joined;
  for (const auto& line : lines) {
    CHECK(line.size() < 320);
    CHECK(line.find('\n') == std::string::npos);
    CHECK(line.find('\r') == std::string::npos);
    joined += line;
    joined += '\n';
  }
  const std::string lower = lowercase(joined);
  for (const char* forbidden :
       {"ssid", "password", "token=", "ip_addr", "mac=", "serial=", "latitude", "longitude", "pair_key"}) {
    CHECK(lower.find(forbidden) == std::string::npos);
  }
}

void test_attached_and_auth_independence() {
  g_next_snapshot = Snapshot{};
  g_next_snapshot.firmware = "v0.6.0";
  g_next_snapshot.build = "nightly abc1234";
  g_next_snapshot.driver = "0.9.6";
  g_next_snapshot.profile = "nooelec_smart_v5";
  g_next_snapshot.driver_state = "FAULT";
  g_next_snapshot.last_error = "ESP_RTL_SDR_ERR_FAULT";
  g_next_snapshot.capture_state = "stopped";
  g_next_snapshot.band = "FM";
  g_next_snapshot.usb_speed = "high";
  g_next_snapshot.receiver_ready = true;
  g_next_snapshot.driver_installed = true;
  g_next_snapshot.metrics_ok = true;
  g_next_snapshot.vid = 0x0bda;
  g_next_snapshot.pid = 0x2838;
  g_next_snapshot.frequency_hz = 101100000u;
  g_next_snapshot.target_sps = 960000u;
  g_next_snapshot.effective_sps = 0u;
  g_next_snapshot.bytes_total = 1024ull;
  g_next_snapshot.short_transfers = 3u;
  g_next_snapshot.usb_overruns = 2u;
  g_next_snapshot.consumer_drops = 5u;
  g_next_snapshot.uptime_ms = 123456u;
  g_next_snapshot.reset_reason = 7;
  g_next_snapshot.heap_free = 12345u;
  g_next_snapshot.dma_free = 2048u;
  g_next_snapshot.dma_largest = 1024u;
  g_next_snapshot.psram_free = 23456u;

  CHECK(run("RTL_SUPPORT REPORT", false));
  CHECK(g_snapshots == 1);
  check_common_invariants(g_out);
  if (g_out.size() == 8) {
    CHECK(g_out[1] == "ORC_SUPPORT_BUILD firmware=\"v0.6.0\" build=\"nightly abc1234\" driver=\"0.9.6\"");
    CHECK(g_out[2] == "ORC_SUPPORT_USB receiver_ready=1 vid=0bda pid=2838 speed=\"high\"");
    CHECK(g_out[3] ==
          "ORC_SUPPORT_RECEIVER installed=1 profile=\"nooelec_smart_v5\" state=\"FAULT\" "
          "last_error=\"ESP_RTL_SDR_ERR_FAULT\"");
    CHECK(g_out[4] == "ORC_SUPPORT_RADIO capture=\"stopped\" band=\"FM\" frequency_hz=101100000");
    CHECK(g_out[5] ==
          "ORC_SUPPORT_STREAM metrics_ok=1 target_sps=960000 effective_sps=0 bytes=1024 "
          "short_transfers=3 overruns=2 consumer_drops=5");
    CHECK(g_out[6] ==
          "ORC_SUPPORT_SYSTEM uptime_ms=123456 reset_reason=7 heap_free=12345 dma_free=2048 "
          "dma_largest=1024 psram_free=23456");
  }

  const std::vector<std::string> unauth_out = g_out;
  CHECK(run("RTL_SUPPORT REPORT", true));
  CHECK(g_snapshots == 1);
  CHECK(g_out == unauth_out);
}

void test_disconnected_snapshot() {
  g_next_snapshot = Snapshot{};
  g_next_snapshot.firmware = "v0.6.0";
  g_next_snapshot.build = "dev";
  g_next_snapshot.driver = "0.9.6";
  g_next_snapshot.profile = "none";
  g_next_snapshot.driver_state = "uninstalled";
  g_next_snapshot.last_error = "none";
  g_next_snapshot.capture_state = "disconnected";
  g_next_snapshot.band = "FM";
  g_next_snapshot.usb_speed = "none";
  g_next_snapshot.target_sps = 960000u;

  CHECK(run("RTL_SUPPORT REPORT", false));
  CHECK(g_snapshots == 1);
  check_common_invariants(g_out);
  if (g_out.size() == 8) {
    CHECK(g_out[2] == "ORC_SUPPORT_USB receiver_ready=0 vid=0000 pid=0000 speed=\"none\"");
    CHECK(g_out[3] ==
          "ORC_SUPPORT_RECEIVER installed=0 profile=\"none\" state=\"uninstalled\" last_error=\"none\"");
    CHECK(g_out[5] ==
          "ORC_SUPPORT_STREAM metrics_ok=0 target_sps=960000 effective_sps=0 bytes=0 "
          "short_transfers=0 overruns=0 consumer_drops=0");
  }
}

void test_extremes_bounds_and_null_strings() {
  const std::string long_text(100, 'x');
  g_next_snapshot = Snapshot{};
  g_next_snapshot.firmware = long_text.c_str();
  g_next_snapshot.build = long_text.c_str();
  g_next_snapshot.driver = long_text.c_str();
  g_next_snapshot.profile = long_text.c_str();
  g_next_snapshot.driver_state = long_text.c_str();
  g_next_snapshot.last_error = long_text.c_str();
  g_next_snapshot.capture_state = long_text.c_str();
  g_next_snapshot.band = long_text.c_str();
  g_next_snapshot.usb_speed = long_text.c_str();
  g_next_snapshot.receiver_ready = true;
  g_next_snapshot.driver_installed = true;
  g_next_snapshot.metrics_ok = true;
  g_next_snapshot.vid = std::numeric_limits<uint16_t>::max();
  g_next_snapshot.pid = std::numeric_limits<uint16_t>::max();
  g_next_snapshot.frequency_hz = std::numeric_limits<uint32_t>::max();
  g_next_snapshot.target_sps = std::numeric_limits<uint32_t>::max();
  g_next_snapshot.effective_sps = std::numeric_limits<uint32_t>::max();
  g_next_snapshot.bytes_total = std::numeric_limits<uint64_t>::max();
  g_next_snapshot.short_transfers = std::numeric_limits<uint32_t>::max();
  g_next_snapshot.usb_overruns = std::numeric_limits<uint32_t>::max();
  g_next_snapshot.consumer_drops = std::numeric_limits<uint32_t>::max();
  g_next_snapshot.uptime_ms = std::numeric_limits<uint32_t>::max();
  g_next_snapshot.reset_reason = -7;
  g_next_snapshot.heap_free = std::numeric_limits<uint32_t>::max();
  g_next_snapshot.dma_free = std::numeric_limits<uint32_t>::max();
  g_next_snapshot.dma_largest = std::numeric_limits<uint32_t>::max();
  g_next_snapshot.psram_free = std::numeric_limits<uint32_t>::max();

  CHECK(run("RTL_SUPPORT REPORT", false));
  CHECK(g_snapshots == 1);
  check_common_invariants(g_out);
  if (g_out.size() == 8) {
    CHECK(g_out[1] == std::string("ORC_SUPPORT_BUILD firmware=\"") + std::string(32, 'x') + "\" build=\"" +
                          std::string(40, 'x') + "\" driver=\"" + std::string(24, 'x') + "\"");
    CHECK(g_out[2] == std::string("ORC_SUPPORT_USB receiver_ready=1 vid=ffff pid=ffff speed=\"") +
                          std::string(12, 'x') + "\"");
    CHECK(g_out[3] == std::string("ORC_SUPPORT_RECEIVER installed=1 profile=\"") + std::string(48, 'x') +
                          "\" state=\"" + std::string(24, 'x') + "\" last_error=\"" + std::string(60, 'x') + "\"");
    CHECK(g_out[4] == std::string("ORC_SUPPORT_RADIO capture=\"") + std::string(24, 'x') + "\" band=\"" +
                          std::string(16, 'x') + "\" frequency_hz=4294967295");
    CHECK(g_out[5] ==
          "ORC_SUPPORT_STREAM metrics_ok=1 target_sps=4294967295 effective_sps=4294967295 "
          "bytes=18446744073709551615 short_transfers=4294967295 overruns=4294967295 "
          "consumer_drops=4294967295");
    CHECK(g_out[6] ==
          "ORC_SUPPORT_SYSTEM uptime_ms=4294967295 reset_reason=-7 heap_free=4294967295 "
          "dma_free=4294967295 dma_largest=4294967295 psram_free=4294967295");
  }

  g_next_snapshot = Snapshot{};
  g_next_snapshot.firmware = nullptr;
  g_next_snapshot.build = nullptr;
  g_next_snapshot.driver = nullptr;
  g_next_snapshot.profile = nullptr;
  g_next_snapshot.driver_state = nullptr;
  g_next_snapshot.last_error = nullptr;
  g_next_snapshot.capture_state = nullptr;
  g_next_snapshot.band = nullptr;
  g_next_snapshot.usb_speed = nullptr;
  CHECK(run("RTL_SUPPORT REPORT", false));
  CHECK(g_snapshots == 1);
  check_common_invariants(g_out);
  if (g_out.size() == 8) {
    CHECK(g_out[1] == "ORC_SUPPORT_BUILD firmware=\"\" build=\"\" driver=\"\"");
    CHECK(g_out[2] == "ORC_SUPPORT_USB receiver_ready=0 vid=0000 pid=0000 speed=\"\"");
    CHECK(g_out[3] == "ORC_SUPPORT_RECEIVER installed=0 profile=\"\" state=\"\" last_error=\"\"");
    CHECK(g_out[4] == "ORC_SUPPORT_RADIO capture=\"\" band=\"\" frequency_hz=0");
  }
}

void test_no_match_and_null_hooks() {
  for (const char* cmd : {"RTL_SUPPORT", "RTL_SUPPORT ", "RTL_SUPPORT REPORT ", "RTL_SUPPORT REPORT extra",
                          "rtl_support report", ""}) {
    CHECK(!run(cmd, false));
    CHECK(g_snapshots == 0 && g_out.empty());
  }

  g_out.clear();
  g_snapshots = 0;
  Hooks missing_snapshot{capture_line, nullptr};
  CHECK(handle_command("RTL_SUPPORT REPORT", false, missing_snapshot));
  CHECK(g_out.empty() && g_snapshots == 0);

  Hooks missing_writer{nullptr, next_snapshot};
  CHECK(handle_command("RTL_SUPPORT REPORT", false, missing_writer));
  CHECK(g_out.empty() && g_snapshots == 0);
}

}  // namespace

int main() {
  test_attached_and_auth_independence();
  test_disconnected_snapshot();
  test_extremes_bounds_and_null_strings();
  test_no_match_and_null_hooks();
  if (failures == 0) std::printf("serial_support tests passed\n");
  return failures == 0 ? 0 : 1;
}
