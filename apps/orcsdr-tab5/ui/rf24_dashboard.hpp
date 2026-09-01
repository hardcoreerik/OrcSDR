#pragma once

#include <cstdint>

namespace orcsdr::rf24 {

constexpr uint8_t kAccessPointCapacity = 16;

enum class Page : uint8_t { overview, channels, devices, csi, settings, count };

struct AccessPoint {
  char ssid[33]{};
  uint8_t bssid[6]{};
  int16_t rssi = 0;
  uint8_t channel = 0;
  bool secure = false;
};

struct Snapshot {
  bool ready = false;
  bool scanning = false;
  bool sound_enabled = false;
  bool visualizer_available = false;
  char message[48]{};
  AccessPoint access_points[kAccessPointCapacity]{};
  uint8_t access_point_count = 0;
  uint32_t revision = 0;
};

enum class ActionKind : uint8_t {
  none,
  close,
  rescan,
  open_settings,
  toggle_mute,
  open_visualizer,
};
struct Action { ActionKind kind = ActionKind::none; };

void enter(const Snapshot& snapshot);
void update(const Snapshot& snapshot);
void update_header(const Snapshot& snapshot);
void leave();
bool active();
Action handle_touch(int32_t x, int32_t y, const Snapshot& snapshot);
bool self_check();

}  // namespace orcsdr::rf24
