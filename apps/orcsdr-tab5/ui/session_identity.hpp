#pragma once
// The two JSON lines a host gets when a session starts (and after workflow / pressure commands): the fixed "hello"
// line and the "node_snapshot" line. The text is moved unchanged from emit_identity() in main.cpp, which now only
// fills Snapshot from the device. Host-tested in tests/session_identity_tests.cpp.
#include <cstddef>
#include <cstdint>

namespace orcsdr::session_identity {

constexpr size_t kSnapshotLineMax = 1280;   // the longest possible snapshot line is checked in the host test

// Written as is, as one console write.
extern const char kHello[];

struct Snapshot {
  unsigned long uptime_ms = 0;          // millis(); the message id suffix
  const char* node_id = "";
  const char* chip = "";                // CONFIG_IDF_TARGET
  unsigned revision = 0;
  unsigned cores = 0;
  unsigned flash_bytes = 0;
  unsigned psram_bytes = 0;
  unsigned free_heap_bytes = 0;
  int display_width = 0;
  int display_height = 0;
  bool touch_ready = false;
  bool station_ready = false;
  bool wifi_configured = false;
  int scan_result = 0;
  bool wifi_connected = false;
  const char* ip = "0.0.0.0";
  bool pmic_detected = false;
  int pmic_type = 0;
  long battery_level = 0;
  int battery_mv = 0;
  long battery_ma = 0;
  int vbus_mv = 0;
  const char* charging = "";
  unsigned long journal_latest_sequence = 0;
  unsigned long journal_acknowledged_sequence = 0;
  unsigned journal_pending = 0;
  unsigned long journal_dropped = 0;
  unsigned long workflow_revision = 0;
  unsigned workflow_runs = 0;
  unsigned workflow_max_runs = 0;
};

// The node_snapshot line with its '\n'; returns snprintf's result.
int format_snapshot(const Snapshot& snapshot, char* out, size_t size);

// kHello, then the snapshot line: two calls of `write`.
void emit(const Snapshot& snapshot, void (*write)(const char* text));

}  // namespace orcsdr::session_identity
