#pragma once

#include "cb_scanner.hpp"

#include <cstddef>
#include <cstdint>

namespace orcsdr::cb {

enum class Mode : uint8_t { am, usb, lsb };
enum class Tab : uint8_t { listen, spectrum, activity, channels, setup };

struct ChannelView {
  float snr_db = 0.0f;
  uint32_t last_active_ms = 0;
  uint16_t hits = 0;
  bool active = false;
  bool locked = false;
  bool skipped = false;
  bool seen = false;
};

struct Snapshot {
  uint32_t now_ms = 0;
  uint32_t frequency_hz = kChannelsHz[kHighwayChannel];
  uint8_t channel = kHighwayChannel;
  Mode mode = Mode::am;
  int32_t clarifier_hz = 0;
  int32_t squelch_dbfs = -75;
  bool squelch_open = false;
  float signal_dbfs = -90.0f;
  bool running = false;
  bool sound_enabled = true;
  int32_t battery_percent = -1;
  State scan_state = State::off;
  uint32_t hang_remaining_ms = 0;
  uint32_t stops = 0;
  Settings scan{};
  float floor_db = kNoLevel;
  uint8_t active_count = 0;
  ChannelView channels[kChannelCount]{};
  Hit log[Monitor::kLogCapacity]{};
  uint8_t log_count = 0;
};

enum class ActionKind : uint8_t {
  none,
  tune_channel,
  channel_down,
  channel_up,
  mode_cycle,
  clarifier_down,
  clarifier_up,
  squelch_down,
  squelch_up,
  scan_toggle,
  hold_toggle,
  skip,
  lockout_current,
  lockout_toggle,
  lockout_clear,
  lockout_ssb_only,
  priority_cycle,
  threshold_down,
  threshold_up,
  hang_down,
  hang_up,
  max_hold_cycle,
  auto_sideband_toggle,
  log_clear,
  open_settings,
  exit_home,
};

struct Action {
  ActionKind kind = ActionKind::none;
  int32_t value = 0;
};

const char* mode_name(Mode mode);
// Relative S-units from the uncalibrated receiver level (S9 = -36 dBFS).
int s_units(float signal_dbfs, int* over_s9_db);

void enter(const Snapshot& snapshot);
void leave();
void draw();
void update(const Snapshot& snapshot);
// Full fft-shifted spectrum; the view always spans the whole 40-channel band.
void draw_spectrum(const float* bins, size_t bin_count, uint32_t sample_rate_sps,
                   uint32_t center_hz);
Action handle_touch(int32_t x, int32_t y);
bool active();
bool spectrum_active();
Tab tab();
bool dashboard_self_check();

}  // namespace orcsdr::cb
