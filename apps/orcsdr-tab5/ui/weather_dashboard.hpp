#pragma once

#include "weather_model.hpp"
#include "weather_noaa.hpp"

#include <cstddef>
#include <cstdint>

namespace orcsdr::weather {

enum class Tab : uint8_t { now, forecast, map, rf_weather, reports, count };
enum class ActionKind : uint8_t {
  none,
  exit_home,
  open_settings,
  volume_set,
  mute_toggle,
  listen_noaa,
  scan_noaa,
  stop_rf,
  save_snapshot,
  cycle_online_policy,
};

struct Action {
  ActionKind kind = ActionKind::none;
  int32_t value = 0;
};

struct Snapshot {
  Tab tab = Tab::now;
  bool rtl_ready = false;
  bool rf_running = false;
  bool rf_owned_by_weather = false;
  bool sd_ready = false;
  bool map_ready = false;
  bool location_configured = false;
  bool rtc_valid = false;
  bool sound_enabled = true;
  uint8_t volume = 128;
  int32_t battery_percent = -1;
  int32_t latitude_e7 = 0;
  int32_t longitude_e7 = 0;
  char location_label[40]{};
  char local_time[24]{};
  OnlinePolicy online_policy = OnlinePolicy::disabled;
  uint32_t current_noaa_hz = 162400000u;
  float relative_dbfs = -120.0f;
  uint32_t rf_age_seconds = 0;
  NoaaScanResult noaa_scan{};
  size_t report_count = 0;
  char last_report_id[40]{};
  char status[64]{};
};

void enter(const Snapshot& snapshot);
void leave();
void draw();
void update(const Snapshot& snapshot);
Action handle_touch(int32_t x, int32_t y, uint32_t now_ms);
void select_tab(Tab tab);
Tab tab();
bool active();
bool self_check();

}  // namespace orcsdr::weather
