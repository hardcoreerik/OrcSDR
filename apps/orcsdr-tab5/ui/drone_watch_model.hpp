#pragma once
// W01: platform-neutral, read-only acquisition presentation.
// A connected Wi-Fi chip or RTL-SDR dongle is NOT evidence of Remote ID capture.
#include <cstdint>

namespace orcsdr::drone_watch {
enum class SourceState : uint8_t {
  unsupported, unavailable, ready, listening, paused, degraded, error
};

struct SourceSnapshot {
  SourceState state = SourceState::unavailable;
  bool capture_qualified = false;  // set ONLY by an independently RF-verified adapter
  bool test_data = false;          // fixtures and replay must never be labeled LIVE
  bool last_packet_valid = false;
  uint32_t last_packet_ms = 0;
  uint32_t packets_received = 0;
  const char* detail = "Remote ID acquisition is not implemented";
};

struct Message {
  const char* headline;
  const char* explanation;
  bool monitoring;
  bool observed;
};

// Pure function: no guess at receiver health, clock, range, aircraft count or position.
Message describe(const SourceSnapshot& source, uint32_t now_ms);
bool valid_packet_age(const SourceSnapshot& source, uint32_t now_ms);
}  // namespace orcsdr::drone_watch
