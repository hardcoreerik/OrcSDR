#include "home_app.hpp"
#include "audio_service.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
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
#include "screens.hpp"
#include "waterfall_style.hpp"

using namespace orcsdr;

namespace {
constexpr char TAG[] = "HOME_APP";
constexpr band_profile::Region kRegion = band_profile::Region::us;
constexpr uint32_t kTuneMinHz = 24000000, kTuneMaxHz = 1766000000;  // the dongle's range (Tab5 default)

gfx::ScaledDisplay* g_display = nullptr;
std::atomic<int> g_tap_x{-1}, g_tap_y{-1};  // pending injected tap, design coordinates (-1 = none)
std::atomic<uint32_t> g_request_hz{0};       // frequency requested by another task
std::atomic<int> g_request_volume{-1};       // volume (0-255) requested by another task
uint32_t g_visible_updates = 0;
std::atomic<int> g_request_sound{-1};         // 0 / 1 = set, -1 = none
std::atomic<uint32_t> g_request_span{0};
std::atomic<int> g_request_screen{-2};        // -2 = none, -1 = home, otherwise a dashboards::Id
const screens::Screen* g_active = nullptr;     // the dashboard on screen (null = Home); UI task only

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
  {
    const radio::Gain g = radio::gain();
    s.gain_available = g.available && radio_state.receiver_state == 2;
    s.gain_auto_available = g.auto_available;
    s.gain_smart = false;  // automatic gain is the tuner's own AGC
    s.gain_auto = g.auto_mode;
    s.rtl_agc_available = g.rtl_agc_available;
    s.rtl_agc = g.rtl_agc;
    s.bias_available = g.bias_available;
    s.bias_on = g.bias;
    s.gain_tenth_db = static_cast<int16_t>(g.tenth_db);
    s.gain_step_count = g.step_count;
    for (int i = 0; i < g.step_count; ++i) s.gain_steps_tenth_db[i] = static_cast<int16_t>(g.steps[i]);
  }
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
      g_span_hz = std::min<uint32_t>(next, 2400000u);  // above 0.96 MHz the stream switches to 2.4 MS/s
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
    case home::ActionKind::gain_auto:
      radio::set_gain_auto();
      ++g_status_rev;
      ++g_rev;
      break;
    case home::ActionKind::gain_tenth_db:
      radio::set_gain_manual(static_cast<int>(a.value));
      ++g_status_rev;
      ++g_rev;
      break;
    case home::ActionKind::rtl_agc:
      radio::set_rtl_agc(a.value != 0);
      ++g_status_rev;
      ++g_rev;
      break;
    case home::ActionKind::filter_set:
      g_bandwidth_override_hz = a.value;
      ++g_tuner_rev;
      ++g_rev;
      break;
    case home::ActionKind::open_dashboard:
      if (screens::find(a.dashboard)) {
        g_request_screen = static_cast<int>(a.dashboard);
      } else if (a.dashboard == dashboards::Id::weather) {
        retune(162400000);  // these use the Tab5's generic receiver screen, which is Home here
      } else if (a.dashboard == dashboards::Id::marine) {
        retune(156800000);
      } else if (a.dashboard == dashboards::Id::satellite) {
        retune(137500000);
      } else if (a.dashboard == dashboards::Id::utilities) {
        ESP_LOGI(TAG, "utilities: staying on Home");
      } else {
        ESP_LOGI(TAG, "dashboard %u is not available on this target yet", unsigned(a.dashboard));
      }
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
  const size_t first = (kBins - visible) / 2;
  if (g_active) {
    if (g_active->spectrum) g_active->spectrum(levels, first, visible, 12.0f);
    return;
  }
  home::draw_spectrum(levels, first, visible, 12.0f);
}

void ui_task(void*) {
  fill_snapshot();
  home::enter(g_snapshot);
  const float inv_sx = 1.0f / g_display->scale_x(), inv_sy = 1.0f / g_display->scale_y();
  bool was_down = false;
  int last_x = 0, last_y = 0;  // design coordinates
  int64_t last_snapshot_us = 0, last_log_us = 0;
  int tap_frames = 0, tap_x = 0, tap_y = 0;  // injected tap (design coordinates), held ~150 ms
  int last_state = -1, last_level = 0, last_gain_key = -1;
  uint32_t last_sps = 0, last_applied_hz = 0;
  uint8_t bins[256];
  for (;;) {
    // Requests from other tasks (the serial interpreter).
    const uint32_t wanted_hz = g_request_hz.exchange(0);
    if (wanted_hz) retune(wanted_hz);
    const int wanted_sound = g_request_sound.exchange(-1);
    if (wanted_sound >= 0) {
      g_sound = wanted_sound != 0;
      ++g_audio_rev;
      ++g_rev;
    }
    const uint32_t wanted_span = g_request_span.exchange(0);
    if (wanted_span) {
      g_span_hz = std::clamp<uint32_t>(wanted_span, 30000, g_active ? 960000u : 2400000u);
      ++g_tuner_rev;
      ++g_rev;
    }
    const int wanted_screen = g_request_screen.exchange(-2);
    if (wanted_screen != -2) {
      if (g_active) g_active->leave();
      else home::leave();
      audio_service::set_gate(true);
      audio_service::set_active(true);  // dashboards without audio switch it off in their enter()
      g_active = wanted_screen >= 0 ? screens::find(static_cast<dashboards::Id>(wanted_screen)) : nullptr;
      M5.Display.fillScreen(0x0000);
      if (g_active) {
        g_span_hz = std::min<uint32_t>(g_span_hz, 960000);  // dashboards run at 0.96 MS/s
        ESP_LOGI(TAG, "opening dashboard %s", g_active->name);
        if (dashboards::find(g_active->id)) dashboards::record_open(g_active->id);
        g_active->enter();
      } else {
        fill_snapshot();
        home::enter(g_snapshot);
      }
      was_down = false;
      tap_frames = 0;
    }
    // 2.4 MS/s is only for Home's wide spans (the Tab5 does the same); dashboards run at 0.96.
    radio::set_sample_rate(!g_active && g_span_hz > 960000 ? 2400000u : 960000u);
    const int wanted_volume = g_request_volume.exchange(-1);
    if (wanted_volume >= 0) {
      g_volume = static_cast<uint8_t>(wanted_volume);
      ++g_audio_rev;
      ++g_rev;
    }

    // Touch: panel pixels -> the Tab5's 1280x720 design grid, which is what its gesture code expects.
    ft5x06_point_t pt;
    bool down = ft5x06_get(&pt);
    if (down) {
      last_x = static_cast<int>(pt.x * inv_sx);
      last_y = static_cast<int>(pt.y * inv_sy);
    }
    if (tap_frames == 0 && g_tap_x.load() >= 0) {
      tap_x = g_tap_x.exchange(-1);
      tap_y = g_tap_y.exchange(-1);
      tap_frames = 15;
    }
    if (tap_frames > 0) {
      --tap_frames;
      down = true;
      last_x = tap_x;
      last_y = tap_y;
    }
    if (g_active) {
      if (g_active->touch) {
        if (down || was_down) g_active->touch(last_x, last_y, down);
      } else {
        if (down && g_active->drag) g_active->drag(last_x, last_y);
        if (!down && was_down) g_active->tap(last_x, last_y);
      }
    } else if (down || was_down) {
      const home::Action action = home::handle_touch(last_x, last_y, down);
      if (action.kind != home::ActionKind::none) handle_action(action);
    }
    was_down = down;

    const int64_t now = esp_timer_get_time();
    if (now - last_snapshot_us > 100000) {
      last_snapshot_us = now;
      const auto rs = radio::snapshot();
      if (rs.receiver_state != last_state) {
        last_state = rs.receiver_state;
        ++g_status_rev;
        ++g_rev;
      }
      {
        const radio::Gain g = radio::gain();
        const int gain_key = g.tenth_db * 8 + (g.auto_mode ? 1 : 0) + (g.rtl_agc ? 2 : 0) + (g.bias ? 4 : 0) +
                             (g.available ? 100000 : 0) + g.step_count * 1000;
        if (gain_key != last_gain_key) {
          last_gain_key = gain_key;
          ++g_status_rev;
          ++g_rev;
        }
      }
      const int level = static_cast<int>(std::lround(radio::level_dbfs()));
      if (level != last_level) {
        last_level = level;
        ++g_rev;
      }
      if ((rs.effective_sps > last_sps + 3000u || last_sps > rs.effective_sps + 3000u) || rs.applied_hz != last_applied_hz) {
        last_sps = rs.effective_sps;
        last_applied_hz = rs.applied_hz;
        ++g_tuner_rev;
        ++g_rev;
      }
      if (g_active) {
        g_active->tick();
      } else {
        fill_snapshot();
        home::update(g_snapshot);
      }
      {
        // Receiver audio follows Home: WFM/NFM/AM are demodulated; USB/LSB and IQ-only bands are silent for now.
        const auto d = active_demod(current_profile());
        if (!g_active) {  // a dashboard sets its own demodulator in its tick
          audio_service::set_mode(d == home_vfo::Demod::am    ? orcsdr::dsp::Mode::am
                                  : d == home_vfo::Demod::nfm ? orcsdr::dsp::Mode::nfm
                                  : d == home_vfo::Demod::usb ? orcsdr::dsp::Mode::usb
                                  : d == home_vfo::Demod::lsb ? orcsdr::dsp::Mode::lsb
                                                              : orcsdr::dsp::Mode::wfm,
                                  true);
        }
        audio_service::set_volume(g_volume);
        audio_service::set_sound(g_sound);
      }
    }
    if (radio::take_bins(bins)) feed_spectrum(bins);

    const int rows = present_frame();
    if (rows > 300) ESP_LOGW(TAG, "large repaint: %d rows, snapshot rev %u/%u/%u/%u", rows, unsigned(g_rev), unsigned(g_tuner_rev), unsigned(g_status_rev), unsigned(g_audio_rev));
    if (rows) {
      ++g_visible_updates;
      if (now - last_log_us > 5000000) {
        last_log_us = now;
        char plot[200];
        home::plot_status(plot, sizeof(plot));
        ESP_LOGI(TAG, "present rows=%d in %u us | %s", rows, present_last_us(), plot);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
}  // namespace

namespace home_app {
Status status() {
  Status s;
  s.frequency_hz = g_frequency_hz;
  s.applied_hz = g_snapshot.frequency_hz == g_frequency_hz ? g_snapshot.frequency_hz : 0;
  s.step_hz = g_step_hz;
  s.span_hz = g_span_hz;
  s.volume = g_volume;
  s.sound = g_sound;
  std::snprintf(s.band, sizeof(s.band), "%s", g_snapshot.band);
  std::snprintf(s.mode, sizeof(s.mode), "%s", g_snapshot.mode);
  s.visible_updates = g_visible_updates;
  return s;
}

void request_frequency(uint32_t hz) { g_request_hz = hz; }
void request_volume(uint8_t volume) { g_request_volume = volume; }
void request_sound(bool enabled) { g_request_sound = enabled ? 1 : 0; }
void request_span(uint32_t span_hz) { g_request_span = span_hz; }
void request_open(dashboards::Id id) { g_request_screen = static_cast<int>(id); }
void request_home() { g_request_screen = -1; }

void inject_tap(int design_x, int design_y) {
  g_tap_y = design_y;
  g_tap_x = design_x;
}

bool open_screen(const char* name) {
  if (std::strcmp(name, "HOME") == 0) {
    request_home();
    return true;
  }
  const screens::Screen* s = screens::find_name(name);
  if (!s) return false;
  request_open(s->id);
  return true;
}
const char* screen_name() {
  static char name[16];
  const screens::Screen* s = g_active;
  if (!s) return "home";
  std::snprintf(name, sizeof(name), "%s", s->name);
  for (char* c = name; *c; ++c) *c = static_cast<char>(std::tolower(static_cast<unsigned char>(*c)));
  return name;
}

bool start(gfx::ScaledDisplay* display) {
  g_display = display;
  dashboards::load_recent(nullptr, 0);  // the Tab5's default recent list
  return xTaskCreatePinnedToCore(ui_task, "home_ui", 12288, nullptr, 4, nullptr, 0) == pdPASS;
}
}  // namespace home_app
