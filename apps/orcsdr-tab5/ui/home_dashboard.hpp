#pragma once

#include <cstddef>
#include <cstdint>

#include "dashboard_registry.hpp"
#include "filter_standards.hpp"

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
  bool step_adjustable = true;  // false on channelized or fixed bands: the STEP SIZE control shows FIXED
  uint32_t filter_bandwidth_hz = 0;
  filter_standards::Kind filter_kind = filter_standards::Kind::fixed;
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
  // Receiver gain, all capability-gated by main.cpp. gain_auto means OrcSDR
  // SMART gain when gain_smart (FM/AM), otherwise the tuner's own AGC.
  bool gain_available = false;
  bool gain_auto_available = false;
  bool gain_smart = false;
  bool gain_auto = false;
  bool rtl_agc_available = false;
  bool rtl_agc = false;
  bool bias_available = false;
  bool bias_on = false;
  int16_t gain_tenth_db = 0;
  uint8_t gain_step_count = 0;
  int16_t gain_steps_tenth_db[32]{};
  char wifi_ip[16]{};
  char receiver[12] = "RTL-SDR";
  char mode[12]{};
  char clock[12]{};
  char date[20]{};
};

enum class ActionKind : uint8_t {
  none,
  open_dashboard,
  open_browser,
  close_browser,
  browser_previous,
  browser_next,
  tune_frequency,
  span_down,
  span_up,
  step_down,
  step_up,
  step_size_down,
  step_size_up,
  sound_toggle,
  volume_down,
  volume_up,
  open_device_settings,
  waterfall_contrast_down,
  waterfall_contrast_up,
  waterfall_palette_next,
  waterfall_speed_next,
  gain_open,
  gain_close,
  filter_open,
  filter_close,
  filter_set,        // value = bandwidth in Hz
  filter_standard,   // the standard width for the band on screen
  filter_edges,      // show/hide the two filter lines on the spectrum
  gain_auto,
  gain_tenth_db,
  rtl_agc,
  mode_next,         // cycle AUTO, NFM, AM, WFM, USB, LSB for the band on screen
  keypad_open,       // tap on the frequency readout
};

struct Action {
  ActionKind kind = ActionKind::none;
  dashboards::Id dashboard = dashboards::Id::count;
  uint32_t value = 0;
};

void enter(const Snapshot& snapshot);
void leave();
void draw();
void update(const Snapshot& snapshot);
void draw_spectrum(const float* levels, size_t first_bin, size_t visible_bins,
                   float floor, bool audio_stressed = false);
Action handle_touch(int32_t x, int32_t y, bool pressed);
uint32_t step_span(uint32_t span_hz, int direction);
bool active();
// A gain or filter popup is showing; close_popup() dismisses it.
bool popup_open();
// Scroll position of the "Last used" list; the keyboard focus map depends on it.
int32_t list_scroll_px();
void close_popup();
bool browser_active();
// The direct-tuning numpad is showing (the shared keyboard handler routes keys to it).
bool keypad_open();
bool self_check();

}  // namespace orcsdr::home
