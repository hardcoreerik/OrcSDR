#pragma once

#include "weather_model.hpp"
#include "weather_report_format.hpp"
#include "weather_service.hpp"

#include <cstddef>
#include <cstdint>

namespace orcsdr::weather {

enum class Tab : uint8_t { now, forecast, map, rf_weather, reports };

enum class ActionKind : uint8_t {
  none,
  exit_home,
  open_settings,
  sound_toggle,
  noaa_channel_previous,
  noaa_channel_next,
  noaa_listen,
  noaa_scan,
  noaa_stop,
  save_snapshot,
};

struct Action {
  ActionKind kind = ActionKind::none;
};

struct Snapshot {
  uint32_t now_ms = 0;
  int32_t battery_percent = -1;
  bool sound_enabled = true;
  bool receiver_ready = false;
  bool sd_ready = false;
  bool map_available = false;
  bool location_configured = false;
  int32_t latitude_e7 = 0;
  int32_t longitude_e7 = 0;
  bool noaa_catalog_installed = false;
  bool noaa_catalog_busy = false;
  report::OnlinePolicy online_policy = report::OnlinePolicy::disabled;
  ServiceState rf{};
  float signal_dbfs = -90.0f;
  Observation observations[8]{};
  uint8_t observation_count = 0;
  uint8_t report_count = 0;
  bool cached_forecast_available = false;
  uint32_t cached_forecast_age_seconds = 0;
  char location_label[40]{};
  char report_status[64]{};
};

void enter(const Snapshot& snapshot);
void leave();
void draw();
void update(const Snapshot& snapshot);
Action handle_touch(int32_t x, int32_t y);
void select_tab(Tab tab);
Tab tab();
bool active();
bool self_check();

}  // namespace orcsdr::weather
