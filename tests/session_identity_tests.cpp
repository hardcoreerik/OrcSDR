// Host tests for the session "hello" and "node_snapshot" lines (session_identity.cpp), moved from emit_identity() in
// main.cpp. tests/data/session_identity_golden.txt holds the two lines firmware 9ce92c03 printed on a Tab5 before the
// move (node id and address replaced with placeholders); the module must print the same bytes for the same values.
// Build with g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Iapps/orcsdr-tab5/ui, from the
// repository root: tests/session_identity_tests.cpp apps/orcsdr-tab5/ui/session_identity.cpp
#include <climits>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "session_identity.hpp"

namespace {
int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

std::vector<std::string> g_writes;
void write(const char* text) { g_writes.push_back(text); }
}  // namespace

int main() {
  using namespace orcsdr::session_identity;

  std::ifstream file("tests/data/session_identity_golden.txt", std::ios::binary);
  std::string hello, snapshot_line;
  std::getline(file, hello);
  std::getline(file, snapshot_line);
  if (!hello.empty() && hello.back() == '\r') hello.pop_back();
  if (!snapshot_line.empty() && snapshot_line.back() == '\r') snapshot_line.pop_back();
  CHECK(hello.size() > 400 && snapshot_line.size() > 900);

  // The values the recorded unit reported.
  Snapshot s;
  s.uptime_ms = 67977292;
  s.node_id = "m5tab5_0123456789ab";
  s.chip = "esp32p4";
  s.revision = 103;
  s.cores = 2;
  s.flash_bytes = 16777216;
  s.psram_bytes = 33554432;
  s.free_heap_bytes = 22144220;
  s.display_width = 1280;
  s.display_height = 720;
  s.touch_ready = true;
  s.station_ready = true;
  s.wifi_configured = true;
  s.scan_result = -1;
  s.wifi_connected = true;
  s.ip = "192.0.2.17";
  s.pmic_detected = false;
  s.pmic_type = 0;
  s.battery_level = 93;
  s.battery_mv = 8088;
  s.battery_ma = 2;
  s.vbus_mv = -1;
  s.charging = "unknown";
  s.journal_latest_sequence = 12930;
  s.journal_acknowledged_sequence = 0;
  s.journal_pending = 8;
  s.journal_dropped = 12922;

  emit(s, write);
  CHECK(g_writes.size() == 2);                       // two console writes, as the two Serial.printf calls were
  CHECK(g_writes.size() == 2 && g_writes[0] == hello + "\n");
  CHECK(g_writes.size() == 2 && g_writes[1] == snapshot_line + "\n");
  CHECK(std::string(kHello) == hello + "\n");
  CHECK(std::strchr(kHello, '%') == nullptr);        // it went through printf before: no conversions to lose

  // Every boolean and the workflow fields.
  s.touch_ready = s.station_ready = s.wifi_configured = s.wifi_connected = false;
  s.pmic_detected = true;
  s.workflow_revision = 7;
  s.workflow_runs = 2;
  s.workflow_max_runs = 5;
  char line[kSnapshotLineMax];
  CHECK(format_snapshot(s, line, sizeof(line)) > 0);
  const std::string text = line;
  CHECK(text.find("\"touch_ready\":false}}") != std::string::npos);
  CHECK(text.find("\"station_ready\":false,\"configured\":false,\"scan_result\":-1,\"connected\":false,") !=
        std::string::npos);
  CHECK(text.find("\"pmic_detected\":true,") != std::string::npos);
  CHECK(text.find("\"offline_workflow\":{\"id\":\"status_on_disconnect\",\"config_revision\":7,\"runs\":2,"
                  "\"max_runs\":5},") != std::string::npos);

  // The longest values still fit the buffer (so nothing is cut that the 2048-byte console buffer kept).
  Snapshot big;
  big.uptime_ms = 4294967295ul;
  big.node_id = "0123456789012345678901234567890";       // node_id[32] in main.cpp
  big.chip = "esp32p4";
  big.revision = 65535;
  big.cores = 255;
  big.flash_bytes = big.psram_bytes = big.free_heap_bytes = 4294967295u;
  big.display_width = big.display_height = INT_MIN;
  big.scan_result = INT_MIN;
  big.ip = "255.255.255.255";
  big.pmic_type = INT_MIN;
  big.battery_level = big.battery_ma = -2147483647L - 1;
  big.battery_mv = big.vbus_mv = -32768;
  big.charging = "not_charging";                         // the longest name charging_state() returns is shorter
  big.journal_latest_sequence = big.journal_acknowledged_sequence = big.journal_dropped = 4294967295ul;
  big.journal_pending = 255;
  big.workflow_revision = 4294967295ul;
  big.workflow_runs = big.workflow_max_runs = 65535;
  const int length = format_snapshot(big, line, sizeof(line));
  std::printf("session_identity: longest snapshot line %d bytes (buffer %zu)\n", length, sizeof(line));
  CHECK(length > 0 && length < static_cast<int>(sizeof(line)) && line[length - 1] == '\n');

  if (failures == 0) std::printf("session_identity: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
