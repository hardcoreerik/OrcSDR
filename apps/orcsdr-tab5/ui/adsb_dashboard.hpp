#pragma once

#include <cstdint>
#include <cstddef>

namespace orcsdr::adsb {

struct Settings {
  bool location_configured = false;
  int32_t latitude_e7 = 0;
  int32_t longitude_e7 = 0;
  uint16_t radar_range_nm = 25;
  uint32_t atc_frequency_hz = 0;
  char atc_label[32]{};
};

constexpr size_t kVisibleAircraft = 6;
constexpr uint8_t kDocumentationViewCount = 5;

struct Aircraft {
  uint32_t icao = 0;
  char callsign[9]{};
  char registration[9]{};
  char type[49]{};
  char owner[51]{};
  int altitude_ft = 0;
  int speed_kts = 0;
  int heading_deg = 0;
  int vertical_rate_fpm = 0;
  float latitude = 0;
  float longitude = 0;
  float signal_dbfs = 0;
  bool has_callsign = false;
  bool has_altitude = false;
  bool has_speed = false;
  bool has_heading = false;
  bool has_vertical_rate = false;
  bool has_position = false;
};

struct Snapshot {
  Aircraft aircraft[kVisibleAircraft]{};
  uint32_t revision = 0;
  uint32_t total_messages = 0;
  float message_rate = 0;
  float strongest_signal_dbfs = 0;
  uint32_t effective_sps = 0;
  uint32_t usb_overruns = 0;
  uint32_t consumer_drops = 0;
  bool faa_aircraft_installed = false;
  bool faa_aviation_installed = false;
  bool sound_enabled = true;
  uint8_t visible_count = 0;
  uint8_t aircraft_count = 0;
};

enum class Action : uint8_t {
  none,
  settings_changed,
  open_data_settings,
  atc_listen,
  atc_resume,
  exit
};

void enter(const Settings& settings);
void leave();
void draw();
void update();
void set_live_snapshot(const Snapshot& snapshot);
void set_atc_listening(bool listening, uint32_t frequency_hz);
uint32_t atc_frequency_hz();
Action handle_touch(int32_t x, int32_t y);
const Settings& settings();
bool active();
void show_documentation_view(uint8_t view, const Settings& settings,
                             bool demo = true);
uint8_t view();
bool self_check();

}  // namespace orcsdr::adsb
