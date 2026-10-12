#pragma once

#include <cstddef>
#include <cstdint>

// Serial diagnostics for the LoRa receive path: detector and counter status,
// region/slot selection, decoder switches and the recent-packet list. The module
// owns the command parsing and formatting; everything it needs from the
// application comes through Hooks, so it never reaches into application globals.

namespace orcsdr::lora_diag {

struct DetectorState {
  bool enabled = false;
  bool armed = false;
  bool preamble_mode = false;
  bool capture_active = false;
  bool capture_ready = false;
  bool decode_busy = false;
  float level_dbfs = 0;
  float noise_dbfs = 0;
  float trigger_dbfs = 0;
  float margin_db = 0;
  float hysteresis_db = 0;
  float ceiling_dbfs = 0;
  uint32_t sustained_ms = 0;
  uint32_t relearns = 0;
  uint32_t events = 0;
  uint32_t drops = 0;
};

struct Counters {
  uint32_t events = 0;
  uint32_t crc_ok = 0;
  uint32_t encrypted = 0;
  uint32_t messages = 0;
  uint32_t failures = 0;
  uint32_t drops = 0;
  uint32_t relearns = 0;
  uint32_t last_decode_ms = 0;
};

struct RecentPacket {
  uint32_t age_ms = 0;
  uint32_t sender = 0;
  uint32_t packet_id = 0;
  uint16_t port = 0;
  bool encrypted = false;
  int16_t snr_tenths = INT16_MAX;  // INT16_MAX = unknown
  char text[112]{};
};

struct ChannelSelection {
  char region[24]{};
  uint16_t slot = 0;
  uint32_t frequency_hz = 0;
};

// Supplied by the application. Any hook may be null; the command then reports
// "unavailable" instead of crashing.
struct Hooks {
  void (*write_line)(const char* text) = nullptr;  // required: prints one formatted line
  void (*detector_state)(DetectorState* out) = nullptr;
  void (*counters)(Counters* out) = nullptr;
  void (*reset_counters)() = nullptr;
  void (*request_relearn)() = nullptr;
  void (*set_preamble_mode)(bool preamble) = nullptr;
  bool (*select_channel)(size_t region_index, uint16_t slot, ChannelSelection* out) = nullptr;
  size_t (*recent_packets)(RecentPacket* out, size_t capacity) = nullptr;
};

constexpr size_t kMaxRecentPackets = 8;

// Handles RTL_LORA_DETECTOR, RTL_LORA_STATS, RTL_LORA_PLAN SET, RTL_LORA_DECODER
// and RTL_LORA_RECENT. Returns true when the command was one of those.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::lora_diag
