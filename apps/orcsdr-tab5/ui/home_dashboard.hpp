#pragma once

#include <cstddef>
#include <cstdint>

#include "dashboard_registry.hpp"

namespace orcsdr::home {

struct Snapshot {
  uint32_t revision = 0;
  uint32_t tuner_revision = 0;
  uint32_t audio_revision = 0;
  uint32_t status_revision = 0;
  uint32_t frequency_hz = 0;
  uint32_t requested_frequency_hz = 0;
  uint32_t span_hz = 960000;
  uint32_t step_hz = 12500;
  uint32_t filter_bandwidth_hz = 0;
  uint32_t effective_sps = 0;
  int32_t battery_percent = -1;
  int32_t vbus_mv = 0;
  uint8_t volume = 0;
  float relative_dbfs = -90.0f;
  bool wifi_connected = false;
  bool usb_connected = false;
  bool driver_ready = false;
  bool receiving = false;
  bool sound_enabled = true;
  char wifi_ip[16]{};
  char mode[12]{};
  char clock[12]{};
  char date[20]{};
};

enum class ActionKind : uint8_t {
  none,
  open_dashboard,
  open_browser,
  close_browser,
  tune_frequency,
  span_down,
  span_up,
  step_down,
  step_up,
  sound_toggle,
  volume_down,
  volume_up,
  waterfall_contrast_down,
  waterfall_contrast_up,
};

struct Action {
  ActionKind kind = ActionKind::none;
  dashboards::Id dashboard = dashboards::Id::count;
  uint32_t value = 0;
};

void enter(const Snapshot& snapshot);
void leave();
void update(const Snapshot& snapshot);
void draw_spectrum(const float* levels, size_t first_bin, size_t visible_bins,
                   float floor, bool audio_stressed = false);
Action handle_touch(int32_t x, int32_t y, bool pressed);
bool active();
bool browser_active();
bool self_check();

}  // namespace orcsdr::home
