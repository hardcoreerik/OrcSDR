#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr::settings {

enum class Section : uint8_t {
  connectivity,
  location_adsb,
  data_maps,
  display_audio,
  radio_defaults,
  storage,
  companion,
  system,
  count
};

struct WifiNetwork {
  char ssid[33]{};
  int16_t rssi = 0;
  bool secure = false;
  bool saved = false;
};

struct WifiProfileView {
  char ssid[33]{};
  bool connected = false;
};

struct State {
  bool wifi_ready = false;
  bool wifi_scanning = false;
  bool wifi_connected = false;
  bool wifi_connecting = false;
  char wifi_ssid[33]{};
  char wifi_ip[16]{};
  char wifi_message[48]{};
  int16_t wifi_rssi = 0;
  WifiNetwork networks[6]{};
  uint8_t network_count = 0;
  WifiProfileView profiles[4]{};
  uint8_t saved_network_count = 0;

  bool location_configured = false;
  int32_t latitude_e7 = 0;
  int32_t longitude_e7 = 0;
  uint16_t radar_range_nm = 25;
  char location_label[40]{};
  char map_pack[40]{};

  uint8_t brightness = 180;
  uint16_t screen_timeout_sec = 0;
  uint8_t volume = 128;
  bool sound_default = true;
  bool auto_start_reception = true;
  bool graphics_default = true;
  char default_band[16]{};
  uint32_t fm_frequency_hz = 0;

  bool sd_ready = false;
  uint64_t sd_total_bytes = 0;
  uint64_t sd_free_bytes = 0;
  bool companion_supported = false;
  uint8_t paired_phone_count = 0;
  int32_t battery_level = -1;
  int16_t battery_mv = -1;
  int32_t battery_current_ma = 0;
  int16_t vbus_mv = -1;
  char charging_state[16]{};
  char build_identity[40]{};
  uint32_t uptime_seconds = 0;
};

enum class ActionKind : uint8_t {
  none,
  close,
  scan_wifi,
  connect_wifi,
  connect_saved_wifi,
  forget_wifi,
  move_wifi_up,
  move_wifi_down,
  location_changed,
  range_changed,
  brightness_changed,
  timeout_changed,
  volume_changed,
  sound_changed,
  auto_start_changed,
  graphics_changed
};

struct Action {
  ActionKind kind = ActionKind::none;
  int32_t value = 0;
};

void enter(const State& state, Section section = Section::connectivity);
void draw();
void update(const State& state);
Action handle_touch(int32_t x, int32_t y);
bool active();
const State& state();
bool take_wifi_credentials(char* ssid, size_t ssid_size,
                           char* password, size_t password_size);
bool self_check();

}  // namespace orcsdr::settings
