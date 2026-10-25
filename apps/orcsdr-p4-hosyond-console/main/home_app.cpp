#include "home_app.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "M5Unified.h"
#include "band_profile.hpp"
#include "dashboard_registry.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "filter_standards.hpp"
#include "freertos/task.h"
#include "ft5x06_touch.h"
#include "home_dashboard.hpp"
#include "home_vfo.hpp"
#include "present.hpp"
#include "radio_service.hpp"
#include "waterfall_style.hpp"

using namespace orcsdr;

namespace {
constexpr char TAG[] = "HOME_APP";
constexpr band_profile::Region kRegion = band_profile::Region::us;
constexpr uint32_t kTuneMinHz = 24000000, kTuneMaxHz = 1766000000;  // the dongle's range (Tab5 default)

gfx::ScaledDisplay* g_display = nullptr;

// ---- VFO state (what the Tab5's main.cpp keeps for Home) ----
home_vfo::Memory g_vfo;
home_vfo::Demod g_demod = home_vfo::Demod::by_band;
uint32_t g_frequency_hz = 96100000;
uint32_t g_step_hz = 0;
uint32_t g_span_hz = 960000;
uint32_t g_bandwidth_override_hz = 0;
uint8_t g_volume = 128;
bool g_sound = true;
uint32_t g_rev = 1, g_tuner_rev = 1, g_audio_rev = 1, g_status_rev = 1;

home::Snapshot g_snapshot;

const band_profile::Profile& current_profile() { return band_profile::resolve(kRegion, g_frequency_hz); }

home_vfo::Demod active_demod(const band_profile::Profile& p) {
  return g_demod != home_vfo::Demod::by_band ? g_demod : home_vfo::demod_for_mode(p.mode);
}

void retune(uint32_t hz) {
  hz = std::clamp(hz, kTuneMinHz, kTuneMaxHz);
  g_frequency_hz = hz;
  const band_profile::Profile& p = current_profile();
  home_vfo::Demod mode;
  if (g_vfo.enter_band(p, &mode)) g_demod = mode;  // a different band applies its own mode
  g_step_hz = g_vfo.step_hz(p);
  g_bandwidth_override_hz = 0;
  radio::tune_to(hz);
  ++g_tuner_rev;
  ++g_audio_rev;
  ++g_rev;
}

void fill_snapshot() {
  const band_profile::Profile& p = current_profile();
  if (g_step_hz == 0) g_step_hz = g_vfo.step_hz(p);
  const auto radio_state = radio::snapshot();
  home::Snapshot& s = g_snapshot;
  s.revision = g_rev;
  s.tuner_revision = g_tuner_rev;
  s.audio_revision = g_audio_rev;
  s.status_revision = g_status_rev;
  s.frequency_hz = radio_state.applied_hz ? radio_state.applied_hz : g_frequency_hz;
  s.requested_frequency_hz = g_frequency_hz;
  s.tune_min_hz = kTuneMinHz;
  s.tune_max_hz = kTuneMaxHz;
  s.span_hz = g_span_hz;
  s.step_hz = g_step_hz;
  s.step_adjustable = band_profile::has_step_control(p);
  const home_vfo::Demod active = active_demod(p);
  const filter_standards::Kind kind = home_vfo::filter_kind(active, g_frequency_hz);
  s.filter_kind = p.mode == band_profile::Mode::none ? p.filter : kind;
  s.filter_bandwidth_hz = g_bandwidth_override_hz ? g_bandwidth_override_hz : filter_standards::standards(s.filter_kind).standard_hz;
  s.effective_sps = radio_state.effective_sps;
  s.battery_percent = -1;  // no battery on this board
  s.volume = g_volume;
  s.relative_dbfs = radio::level_dbfs();
  s.wifi_connected = false;
  s.usb_connected = radio_state.receiver_state >= 2;
  s.driver_ready = radio_state.receiver_state == 2;
  s.receiving = radio_state.receiver_state == 2;
  s.sound_enabled = g_sound;
  s.gain_available = false;  // receiver gain control is not wired on this target yet
  s.gain_auto_available = false;
  std::snprintf(s.receiver, sizeof(s.receiver), "RTL-SDR");
  std::snprintf(s.mode, sizeof(s.mode), "%s", p.mode == band_profile::Mode::none ? "IQ" : home_vfo::demod_name(active));
  s.mode_choice = g_vfo.pin(p);
  s.mode_active = static_cast<uint8_t>(active);
  s.mode_suggested = static_cast<uint8_t>(home_vfo::demod_for_mode(p.mode));
  s.mode_selectable = p.mode != band_profile::Mode::none;
  std::snprintf(s.band, sizeof(s.band), "%s", p.name);
  s.clock[0] = 0;  // no clock source yet: the UI shows its own "--:--" and "UPTIME"
  s.date[0] = 0;
}

void handle_action(const home::Action& a) {
  const band_profile::Profile& p = current_profile();
  switch (a.kind) {
    case home::ActionKind::tune_frequency:
      retune(a.value);
      break;
    case home::ActionKind::step_up:
    case home::ActionKind::step_down: {
      const int dir = a.kind == home::ActionKind::step_up ? 1 : -1;
      retune(band_profile::step_frequency(p, g_frequency_hz, g_step_hz ? g_step_hz : g_vfo.step_hz(p), dir, kTuneMinHz,
                                           kTuneMaxHz));
      break;
    }
    case home::ActionKind::step_size_up:
    case home::ActionKind::step_size_down: {
      const uint32_t next = band_profile::cycle_step_hz(p, g_step_hz, a.kind == home::ActionKind::step_size_up);
      g_vfo.remember_step(p, next);
      g_step_hz = next;
      ++g_audio_rev;
      ++g_rev;
      break;
    }
    case home::ActionKind::span_up:
    case home::ActionKind::span_down: {
      // The IQ stream is 0.96 MS/s, so the widest span is the stream itself; narrower spans zoom in.
      const uint32_t next = home::step_span(g_span_hz, a.kind == home::ActionKind::span_up ? 1 : -1);
      g_span_hz = std::min(next, radio::sample_rate());
      ++g_tuner_rev;
      ++g_rev;
      break;
    }
    case home::ActionKind::sound_toggle:
      g_sound = !g_sound;
      ++g_audio_rev;
      ++g_rev;
      break;
    case home::ActionKind::volume_up:
    case home::ActionKind::volume_down:
      g_volume = static_cast<uint8_t>(std::clamp(static_cast<int>(g_volume) + (a.kind == home::ActionKind::volume_up ? 16 : -16), 0, 255));
      ++g_audio_rev;
      ++g_rev;
      break;
    case home::ActionKind::mode_set: {
      const auto choice = static_cast<home_vfo::Demod>(std::min<uint32_t>(a.value, 5));
      g_vfo.set_pin(p, choice);
      g_demod = choice;
      g_vfo.note_band(p);
      ++g_tuner_rev;
      ++g_rev;
      break;
    }
    case home::ActionKind::filter_set:
      g_bandwidth_override_hz = a.value;
      ++g_tuner_rev;
      ++g_rev;
      break;
    case home::ActionKind::open_dashboard:
      ESP_LOGI(TAG, "dashboard %u is not available on this target yet", unsigned(a.dashboard));
      break;
    case home::ActionKind::none:
      break;
    default:
      ESP_LOGI(TAG, "action %u has no receiver effect on this target yet", unsigned(a.kind));
      break;
  }
}

// Visible window of the 256 IQ-FFT bins for the current span (zoom around the tuned frequency).
void feed_spectrum(const uint8_t* bins) {
  constexpr size_t kBins = 256;
  float levels[kBins];
  for (size_t i = 0; i < kBins; ++i) levels[i] = bins[i] * 0.25f + 12.0f;  // the FFT bytes are (dB - 12) * 4
  const uint32_t rate = radio::sample_rate();
  const size_t visible = std::clamp<size_t>(static_cast<size_t>(static_cast<uint64_t>(kBins) * g_span_hz / rate), 16, kBins);
  home::draw_spectrum(levels, (kBins - visible) / 2, visible, 12.0f);
}

void ui_task(void*) {
  home::Snapshot initial;
  fill_snapshot();
  home::enter(g_snapshot);
  const float inv_sx = 1.0f / g_display->scale_x(), inv_sy = 1.0f / g_display->scale_y();
  bool was_down = false;
  int last_x = 0, last_y = 0;
  int64_t last_snapshot_us = 0, last_log_us = 0;
  uint8_t bins[256];
  for (;;) {
    // Touch: panel pixels -> the Tab5's 1280x720 design grid, which is what its gesture code expects.
    ft5x06_point_t pt;
    const bool down = ft5x06_get(&pt);
    if (down) {
      last_x = static_cast<int>(pt.x * inv_sx);
      last_y = static_cast<int>(pt.y * inv_sy);
    }
    if (down || was_down) {
      const home::Action action = home::handle_touch(last_x, last_y, down);
      if (action.kind != home::ActionKind::none) handle_action(action);
    }
    was_down = down;

    const int64_t now = esp_timer_get_time();
    if (now - last_snapshot_us > 100000) {
      last_snapshot_us = now;
      const uint32_t before = g_status_rev;
      static int last_state = -1;
      const int state = radio::snapshot().receiver_state;
      if (state != last_state) {
        last_state = state;
        ++g_status_rev;
        ++g_rev;
      }
      static int last_level = 0;
      const int level = static_cast<int>(std::lround(radio::level_dbfs()));
      if (level != last_level) {
        last_level = level;
        ++g_rev;
      }
      (void)before;
      static uint32_t last_sps = 0, last_uptime = 0;
      const auto rs = radio::snapshot();
      if (rs.effective_sps / 1000u != last_sps / 1000u || rs.applied_hz != g_snapshot.frequency_hz) {
        last_sps = rs.effective_sps;
        ++g_tuner_rev;
        ++g_rev;
      }
      (void)last_uptime;
      fill_snapshot();
      home::update(g_snapshot);
    }
    if (radio::take_bins(bins)) feed_spectrum(bins);

    const int rows = present_frame();
    if (rows && now - last_log_us > 5000000) {
      last_log_us = now;
      char plot[200];
      home::plot_status(plot, sizeof(plot));
      ESP_LOGI(TAG, "present rows=%d in %u us | %s", rows, present_last_us(), plot);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
}  // namespace

namespace home_app {
bool start(gfx::ScaledDisplay* display) {
  g_display = display;
  dashboards::load_recent(nullptr, 0);  // the Tab5's default recent list
  return xTaskCreatePinnedToCore(ui_task, "home_ui", 12288, nullptr, 4, nullptr, 0) == pdPASS;
}
}  // namespace home_app
