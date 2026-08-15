#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr::lora {

constexpr size_t kNodeCapacity = 16;
constexpr size_t kEventCapacity = 16;

enum class View : uint8_t { overview, nodes, traffic, map, rf_health, count };

struct Node {
  uint32_t id = 0;
  uint32_t seen_ms = 0;
  int32_t latitude_e7 = INT32_MAX;
  int32_t longitude_e7 = INT32_MAX;
  int16_t signal_tenths = INT16_MAX;
  int16_t snr_tenths = INT16_MAX;
  int16_t altitude_m = INT16_MIN;
  uint8_t battery_percent = UINT8_MAX;
  uint8_t hops = UINT8_MAX;
  bool favorite = false;
  char name[32]{};
  char role[16]{};
};

struct Event {
  uint32_t sender = 0;
  uint32_t destination = 0;
  uint32_t packet_id = 0;
  uint32_t received_ms = 0;
  int32_t latitude_e7 = INT32_MAX;
  int32_t longitude_e7 = INT32_MAX;
  int16_t signal_tenths = INT16_MAX;
  int16_t snr_tenths = INT16_MAX;
  uint16_t port = 0;
  bool encrypted = false;
  bool verified = false;
  char text[112]{};
};

struct Snapshot {
  uint32_t revision = 0;
  uint32_t frequency_hz = 0;
  uint32_t span_hz = 0;
  uint32_t effective_sps = 0;
  uint32_t target_sps = 960000;
  uint32_t usb_overruns = 0;
  uint32_t consumer_drops = 0;
  uint32_t decoded_frames = 0;
  uint32_t crc_ok = 0;
  uint32_t encrypted_frames = 0;
  uint32_t log_drops = 0;
  uint32_t uptime_seconds = 0;
  uint32_t survey_progress = 0;
  uint8_t sf = 11;
  uint32_t bandwidth_hz = 250000;
  int32_t battery_percent = -1;
  float noise_dbfs = -90.0f;
  float trigger_dbfs = -75.0f;
  float relative_dbfs = -90.0f;
  bool running = false;
  bool driver_ready = false;
  bool wifi_connected = false;
  bool sd_logging = false;
  bool survey_active = false;
  bool native_decoder_ready = false;
  bool key_loaded = false;
  char profile[24]{};
  char region[24]{};
  Node nodes[kNodeCapacity]{};
  Event events[kEventCapacity]{};
  uint8_t node_count = 0;
  uint8_t event_count = 0;
  uint8_t selected_node = 0;
};

enum class ActionKind : uint8_t {
  none,
  select_view,
  select_node,
  toggle_favorite,
  filter_next,
  scan_toggle,
  record_iq_toggle,
  logging_toggle,
  clear_events,
  export_log,
  center_map,
  follow_node,
  mark_point,
  save_snapshot,
  open_channels,
  open_settings,
  exit_home,
};

struct Action {
  ActionKind kind = ActionKind::none;
  uint32_t value = 0;
};

void enter(const Snapshot& snapshot);
void leave();
void draw();
void update(const Snapshot& snapshot);
void draw_spectrum(const float* levels, size_t first_bin, size_t visible_bins, float floor);
Action handle_touch(int32_t x, int32_t y);
bool active();
bool spectrum_active();
View view();
void show_documentation_view(View view, const Snapshot& snapshot);
void toggle_filter();
void toggle_follow_node();
bool self_check();

}  // namespace orcsdr::lora
