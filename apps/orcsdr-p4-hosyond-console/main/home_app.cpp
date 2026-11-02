#include "home_app.hpp"
#include "audio_service.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include "M5Unified.h"
#include "band_profile.hpp"
#include "dashboard_registry.hpp"
#include "esp_log.h"
#include "esp_rtl_sdr.h"
#include "esp_timer.h"
#include "filter_standards.hpp"
#include "freertos/task.h"
#include "hosyond_board.h"
#include "home_dashboard.hpp"
#include "focus_nav.hpp"
#include "home_vfo.hpp"
#include "orientation_app.hpp"
#include "present.hpp"
#include "radio_service.hpp"
#include "screens.hpp"
#include "settings_app.hpp"
#include "screen_snap.hpp"
#include "ui_support.hpp"
#include "wifi_app.hpp"
#include "c6_recovery_app.hpp"
#include "waterfall_style.hpp"

using namespace orcsdr;

namespace {
constexpr char TAG[] = "HOME_APP";
constexpr band_profile::Region kRegion = band_profile::Region::us;
// Let the driver validate each selected dongle profile; do not exclude HF in the UI.
constexpr uint32_t kTuneMinHz = ESP_RTL_SDR_FREQ_MIN_HZ;
constexpr uint32_t kTuneMaxHz = ESP_RTL_SDR_FREQ_MAX_HZ;

gfx::ScaledDisplay* g_display = nullptr;
std::atomic<int> g_tap_x{-1}, g_tap_y{-1};  // pending injected tap, design coordinates (-1 = none)
std::atomic<int> g_rotation{0},g_view_width{0},g_view_height{0};
std::atomic<int> g_screenshot_state{0};  // 0 idle, 1 queued, 2 UI-owned capture/stream
std::atomic<uint32_t> g_request_hz{0};       // frequency requested by another task
std::atomic<int> g_request_volume{-1};       // volume (0-255) requested by another task
uint32_t g_visible_updates = 0;
std::atomic<int> g_request_sound{-1};         // 0 / 1 = set, -1 = none
std::atomic<uint32_t> g_request_span{0};
std::atomic<int> g_request_screen{-2};        // -2 = none, -1 = home, otherwise a dashboards::Id
const ::screens::Screen* g_active = nullptr;     // the dashboard on screen (null = Home); UI task only

// ---- VFO state (what the Tab5's main.cpp keeps for Home) ----
home_vfo::Memory g_vfo;
home_vfo::Demod g_demod = home_vfo::Demod::by_band;
uint32_t g_frequency_hz = 96100000;
uint32_t g_step_hz = 0;
uint32_t g_home_span_hz = 2400000;
uint32_t g_home_frequency_hz = 0;
bool g_in_dashboard = false;
uint32_t g_span_hz = 2400000;  // the Tab5's Home default: the full 2.4 MS/s stream
uint32_t g_bandwidth_override_hz = 0;
uint8_t g_volume = 128;
bool g_sound = true;
uint32_t g_rev = 1, g_tuner_rev = 1, g_audio_rev = 1, g_status_rev = 1;

home::Snapshot g_snapshot;

struct HomeTraceRect {
  const char* name;
  home::LayoutRect rect;
  uint32_t hash;
};
HomeTraceRect g_home_trace_rects[5]{};
constexpr uint32_t kHomeTraceMaxFrames = 3000;
constexpr int64_t kHomeTraceMaxUs = 15000000;
uint32_t g_home_trace_frames = 0;
uint32_t g_home_trace_frames_total = 0;
bool g_home_trace_valid = false;
int64_t g_home_trace_started_us = 0;
uint64_t g_home_trace_hash_us = 0;
uint64_t g_home_trace_samples = 0;

void hash_home_pixel(uint32_t& hash, uint32_t& samples, int x, int y,
                     const home::LayoutRect& rect) {
  if (x < rect.x || x >= rect.x + rect.w || y < rect.y || y >= rect.y + rect.h) return;
  if (!g_display || !g_display->ready()) return;
  const auto* pixels = g_display->buffer();
  const size_t stride = g_display->stride_pixels();
  const int px = std::clamp(g_display->X(x), 0, g_display->target_width() - 1);
  const int py = std::clamp(g_display->Y(y), 0, g_display->target_height() - 1);
  const uint16_t pixel = pixels[size_t(py) * stride + px];
  hash = (hash ^ uint8_t(pixel)) * 16777619u;
  hash = (hash ^ uint8_t(pixel >> 8)) * 16777619u;
  ++samples;
}

void hash_home_patch(uint32_t& hash, uint32_t& samples, int cx, int cy,
                     const home::LayoutRect& rect, int radius = 2) {
  for (int dy = -radius; dy <= radius; ++dy)
    for (int dx = -radius; dx <= radius; ++dx)
      hash_home_pixel(hash, samples, cx + dx, cy + dy, rect);
}

uint32_t home_region_hash(const HomeTraceRect& region, uint32_t& samples) {
  const auto& rect = region.rect;
  if (!g_display || !g_display->ready()) return 0;
  uint32_t hash = 2166136261u;
  constexpr int kGrid = 12;
  for (int gy = 0; gy < kGrid; ++gy) {
    const int y = rect.y + (rect.h > 1 ? gy * (rect.h - 1) / (kGrid - 1) : 0);
    for (int gx = 0; gx < kGrid; ++gx) {
      const int x = rect.x + (rect.w > 1 ? gx * (rect.w - 1) / (kGrid - 1) : 0);
      hash_home_pixel(hash, samples, x, y, rect);
    }
  }
  const int mid_x = rect.x + rect.w / 2;
  const int mid_y = rect.y + rect.h / 2;
  if (std::strcmp(region.name, "contrast") == 0) {
    hash_home_patch(hash, samples, rect.x + 18, mid_y, rect);
    hash_home_patch(hash, samples, mid_x, mid_y, rect);
    hash_home_patch(hash, samples, rect.x + rect.w - 18, mid_y, rect);
  } else if (std::strcmp(region.name, "span") == 0 ||
             std::strcmp(region.name, "tune") == 0 ||
             std::strcmp(region.name, "step") == 0) {
    // Match the two arrow glyph centres and the centered label/value rows in draw_stepper().
    hash_home_patch(hash, samples, rect.x + 21, mid_y, rect);
    hash_home_patch(hash, samples, rect.x + rect.w - 21, mid_y, rect);
    hash_home_patch(hash, samples, mid_x, rect.y + (rect.h >= 60 ? 15 : 12), rect);
    hash_home_patch(hash, samples, mid_x, rect.y + (rect.h >= 60 ? 43 : 34), rect);
  } else if (std::strcmp(region.name, "footer") == 0) {
    constexpr int starts[] = {0, 17, 34, 49, 62, 75};
    constexpr int ends[] = {17, 34, 49, 62, 75, 100};
    for (size_t i = 0; i < std::size(starts); ++i) {
      const int x = rect.x + rect.w * (starts[i] + ends[i]) / 200;
      hash_home_patch(hash, samples, x, mid_y, rect, 1);
    }
    hash_home_patch(hash, samples, rect.x + rect.w - 8, mid_y, rect, 2);
  }
  return hash;
}

void end_home_trace(const char* reason) {
  if (!g_home_trace_valid) return;
  const int64_t elapsed_us = esp_timer_get_time() - g_home_trace_started_us;
  const auto layout = home::layout();
  ESP_LOGI(TAG, "RTL_UI TRACE_HOME stage=end reason=%s frames=%u elapsed_us=%lld hash_total_us=%llu samples=%llu view=%dx%d target=%dx%d",
           reason, unsigned(g_home_trace_frames_total - g_home_trace_frames),
           static_cast<long long>(elapsed_us),
           static_cast<unsigned long long>(g_home_trace_hash_us),
           static_cast<unsigned long long>(g_home_trace_samples), layout.viewport.w,
           layout.viewport.h, g_display->target_width(), g_display->target_height());
  g_home_trace_frames = 0;
  g_home_trace_valid = false;
}

void begin_home_trace() {
  if (!g_display || !g_display->ready()) return;
  const auto layout = home::layout();
  g_home_trace_rects[0] = {"contrast", layout.contrast, 0};
  g_home_trace_rects[1] = {"span", layout.span, 0};
  g_home_trace_rects[2] = {"tune", layout.tune, 0};
  g_home_trace_rects[3] = {"step", layout.step, 0};
  g_home_trace_rects[4] = {"footer", layout.footer, 0};
  g_home_trace_started_us = esp_timer_get_time();
  g_home_trace_samples = 0;
  const int64_t started = esp_timer_get_time();
  for (auto& region : g_home_trace_rects) {
    uint32_t samples = 0;
    region.hash = home_region_hash(region, samples);
    g_home_trace_samples += samples;
  }
  g_home_trace_hash_us = static_cast<uint64_t>(esp_timer_get_time() - started);
  char baseline[160]{};
  size_t used = 0;
  for (const auto& region : g_home_trace_rects) {
    const int written = std::snprintf(baseline + used, sizeof(baseline) - used,
                                      "%s%s:%08lx", used ? "," : "", region.name,
                                      static_cast<unsigned long>(region.hash));
    if (written > 0 && static_cast<size_t>(written) < sizeof(baseline) - used)
      used += static_cast<size_t>(written);
  }
  g_home_trace_frames = g_home_trace_frames_total = kHomeTraceMaxFrames;
  g_home_trace_valid = true;
  ESP_LOGI(TAG, "RTL_UI TRACE_HOME stage=baseline frames_max=%u elapsed_max_us=%lld hashes=%s hash_us=%llu samples=%llu view=%dx%d target=%dx%d",
           unsigned(kHomeTraceMaxFrames), static_cast<long long>(kHomeTraceMaxUs),
           baseline, static_cast<unsigned long long>(g_home_trace_hash_us),
           static_cast<unsigned long long>(g_home_trace_samples), layout.viewport.w,
           layout.viewport.h, g_display->target_width(), g_display->target_height());
}

void trace_home_regions(const char* stage) {
  if (!g_home_trace_frames || !g_home_trace_valid || !g_display) return;
  if (esp_timer_get_time() - g_home_trace_started_us >= kHomeTraceMaxUs) {
    end_home_trace("time_limit");
    return;
  }
  uint32_t hashes[5]{};
  const int64_t started = esp_timer_get_time();
  uint64_t sampled = 0;
  for (size_t i = 0; i < std::size(g_home_trace_rects); ++i) {
    uint32_t samples = 0;
    hashes[i] = home_region_hash(g_home_trace_rects[i], samples);
    sampled += samples;
  }
  const uint64_t elapsed = static_cast<uint64_t>(esp_timer_get_time() - started);
  g_home_trace_hash_us += elapsed;
  g_home_trace_samples += sampled;
  bool changed = false;
  char regions[160]{};
  size_t used = 0;
  for (size_t i = 0; i < std::size(g_home_trace_rects); ++i) {
    auto& region = g_home_trace_rects[i];
    if (hashes[i] == region.hash) continue;
    const int written = std::snprintf(regions + used, sizeof(regions) - used,
                                      "%s%s:%08lx>%08lx", used ? "," : "", region.name,
                                      static_cast<unsigned long>(region.hash),
                                      static_cast<unsigned long>(hashes[i]));
    if (written > 0 && static_cast<size_t>(written) < sizeof(regions) - used)
      used += static_cast<size_t>(written);
    region.hash = hashes[i];
    changed = true;
  }
  if (changed) {
    const auto layout = home::layout();
    ESP_LOGI(TAG, "RTL_UI TRACE_HOME stage=%s frame=%u changed=%s hash_us=%llu total_us=%llu view=%dx%d target=%dx%d",
             stage, unsigned(g_home_trace_frames_total - g_home_trace_frames), regions,
             static_cast<unsigned long long>(elapsed),
             static_cast<unsigned long long>(g_home_trace_hash_us),
             layout.viewport.w, layout.viewport.h, g_display->target_width(), g_display->target_height());
  }
}

void finish_home_trace_frame() {
  if (!g_home_trace_frames || !g_home_trace_valid) return;
  --g_home_trace_frames;
  if (esp_timer_get_time() - g_home_trace_started_us >= kHomeTraceMaxUs)
    end_home_trace("time_limit");
  else if (!g_home_trace_frames)
    end_home_trace("frame_limit");
}

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
  s.wifi_connected = wifi_app::status().connected;
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
    case home::ActionKind::open_device_settings:
      g_request_screen = static_cast<int>(dashboards::Id::settings);
      break;
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
      if (::screens::find(a.dashboard)) {
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
  static float lut[256];
  static bool lut_ready = false;
  if (!lut_ready) {
    for (int i = 0; i < 256; ++i) lut[i] = i * 0.25f + 12.0f;  // the FFT bytes are (dB - 12) * 4
    lut_ready = true;
  }
  for (size_t i = 0; i < kBins; ++i) levels[i] = lut[bins[i]];
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
  const auto* geometry=present_geometry();
  const auto safe=geometry?geometry->safe:orcsdr_rect_t{0,0,g_display->width(),g_display->height()};
  if (!home::set_viewport({int(safe.x),int(safe.y),int(safe.width),int(safe.height)})) {
    ESP_LOGE(TAG,"Home viewport unsupported: %ldx%ld",(long)safe.width,(long)safe.height);
    vTaskDelete(nullptr);return;
  }
  ESP_LOGI(TAG,"RTL_HOME_SELF_CHECK result=%s",home::self_check()?"PASS":"FAIL");
  g_view_width=g_display->width();g_view_height=g_display->height();
  g_rotation=geometry?geometry->rotation:orientation_app::status().current_degrees;
  home::enter(g_snapshot);
  begin_home_trace();

  bool was_down = false, header_gesture = false;
  int last_x = 0, last_y = 0;  // design coordinates
  int64_t last_snapshot_us = 0, last_log_us = 0;
  int tap_frames = 0, tap_x = 0, tap_y = 0;  // injected tap (design coordinates), held ~150 ms
  int last_state = -1, last_level = 0, last_gain_key = -1;
  bool last_wifi = false;
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
      g_active = wanted_screen >= 0 ? ::screens::find(static_cast<dashboards::Id>(wanted_screen)) : nullptr;
      M5.Display.fillScreen(0x0000);
      if (g_active) {
        if (!g_in_dashboard) {  // Home's span and frequency come back when the dashboard closes
          g_home_span_hz = g_span_hz;
          g_home_frequency_hz = g_frequency_hz;
        }
        g_in_dashboard = true;
        g_span_hz = std::min<uint32_t>(g_span_hz, 960000);  // dashboards run at 0.96 MS/s
        ESP_LOGI(TAG, "opening dashboard %s", g_active->name);
        if (dashboards::find(g_active->id)) dashboards::record_open(g_active->id);
        g_active->enter();
      } else {
        if (g_in_dashboard) {
          g_span_hz = g_home_span_hz;
          if (g_home_frequency_hz && g_home_frequency_hz != g_frequency_hz) retune(g_home_frequency_hz);
        }
        g_in_dashboard = false;
        ++g_tuner_rev;
        fill_snapshot();
        home::enter(g_snapshot);
      }
      was_down = false;
      header_gesture = false;
      ui_support::header_reset();
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

    // Board polling returns native pixels; presentation supplies the exact inverse.
    static bool physical_down=false;
    orcsdr_touch_sample_t point{};
    const esp_err_t touch=hosyond_board_touch_poll(&point);
    if (touch==ESP_OK) {
      orcsdr_point_t view;
      const auto* geometry=present_geometry();
      if (geometry && orcsdr_native_to_view(geometry,point.position,&view)) {
        last_x=view.x;last_y=view.y;physical_down=point.pressed;
      } else physical_down=false;
    } else if (touch!=ESP_ERR_NOT_FOUND) physical_down=false;
    bool down=physical_down;
    // Deliver a release frame before accepting the next injected press.
    if (tap_frames == 0 && !was_down && g_tap_x.load() >= 0) {
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
    // The shared header (volume tray, home, mute, settings) gets every touch first, on every screen, exactly as the
    // Tab5's main loop does: a press on the speaker opens a volume slider you drag, and the tray stays live while open.
    if (!(g_active && g_active->id==dashboards::Id::settings && c6_recovery_app::status().busy) && down && (!was_down || ui_support::header_overlay_open()) && ui_support::handle_header_touch(last_x, last_y, !was_down))
      header_gesture = true;
    if (!down && !was_down) header_gesture = false;
    ui_support::header_service();
    {
      // Save volume/mute once they have stopped changing for 1.5 s (flash writes are not made while sliding).
      static uint8_t saved_volume = 255;
      static int saved_sound = -1;
      static int64_t changed_at_us = 0;
      static bool primed = false;
      if (!primed) {
        saved_volume = g_volume;
        saved_sound = g_sound ? 1 : 0;
        primed = true;
      }
      const int64_t t_us = esp_timer_get_time();
      if (g_volume != saved_volume || (g_sound ? 1 : 0) != saved_sound) {
        if (!changed_at_us) changed_at_us = t_us;
        if (t_us - changed_at_us > 1500000) {
          ui_support::put_u8("volume", g_volume);
          ui_support::put_bool("sound", g_sound);
          saved_volume = g_volume;
          saved_sound = g_sound ? 1 : 0;
          changed_at_us = 0;
        }
      } else {
        changed_at_us = 0;
      }
    }
    if (header_gesture) {
      if (!down) header_gesture = false;  // the rest of this gesture, including the lift, belongs to the header
    } else if (g_active) {
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

    // Automatic and manual orientation proposals share this UI-owner path.
    // Poll at the sensor cadence; touch release and C6 flash states are gates.
    static uint32_t last_orientation_tick_ms = 0;
    const uint32_t orientation_now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    if (static_cast<uint32_t>(orientation_now_ms - last_orientation_tick_ms) >= 50) {
      last_orientation_tick_ms = orientation_now_ms;
      const auto update = orcsdr::wifi::c6_update_status();
      const bool update_locked = (g_active && g_active->id==dashboards::Id::settings && settings::geometry_locked()) || c6_recovery_app::status().busy || wifi_app::recovery_access()!=wifi_app::RecoveryAccess::idle ||
                                 update.state == orcsdr::wifi::C6UpdateState::updating ||
                                 update.state == orcsdr::wifi::C6UpdateState::rebooting;
      int32_t degrees = -1;
      if (orientation_app::tick(down || was_down, update_locked, orientation_now_ms, &degrees)) {
        const bool home_active = g_active == nullptr;
        ui_support::header_reset();
        if (!present_geometry()) {
          // Headless rendering has no physical geometry to rotate.
          g_rotation = degrees;
          orientation_app::acknowledge_applied(degrees);
        } else if (present_set_rotation(degrees)) {
          const auto* g = present_geometry();
          g_rotation = degrees;
          orientation_app::acknowledge_applied(degrees);
          if (g && home::set_viewport({int(g->safe.x), int(g->safe.y), int(g->safe.width), int(g->safe.height)})) {
            g_view_width = g->width;
            g_view_height = g->height;
            focus_nav::set_viewport(g->width, g->height);
            focus_nav::clear();
            if (!home_active && g_active->geometry_changed && g_active->geometry_changed()) {
              ESP_LOGI(TAG,"RTL_DISPLAY_SCREEN_RESIZED screen=%s",g_active->name);
            } else if (!home_active) {
              // Screens migrate incrementally; never retain old hit regions.
              g_active->leave();g_active=nullptr;
              fill_snapshot();home::enter(g_snapshot);
              home_app::request_home(); // normal routing restores Home's radio/span state
            } else home::draw();
            if (home_active) begin_home_trace();
            ESP_LOGI(TAG, "RTL_DISPLAY_ROTATION result=PASS degrees=%d view=%ldx%ld",
                     degrees, (long)g->width, (long)g->height);
          } else {
            ESP_LOGE(TAG, "RTL_DISPLAY_ROTATION failed stage=home_layout");
          }
        } else {
          orientation_app::reject_proposal();
          ESP_LOGE(TAG, "RTL_DISPLAY_ROTATION failed stage=present");
        }
        header_gesture = false;
        tap_frames = 0;
      }
    }

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
        const bool wifi_now = wifi_app::status().connected;
        if (wifi_now != last_wifi) {
          last_wifi = wifi_now;
          ++g_status_rev;
          ++g_rev;
        }
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
        trace_home_regions("before_update");
        home::update(g_snapshot);
        trace_home_regions("after_update");
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
    if (radio::take_bins(bins)) {
      if (!g_active) trace_home_regions("before_spectrum");
      feed_spectrum(bins);
      if (!g_active) trace_home_regions("after_spectrum");
    }

    if (!g_active) trace_home_regions("before_present");
    const int rows = present_frame();
    if (!g_active) trace_home_regions("after_present");
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
    int screenshot_queued = 1;
    if (g_screenshot_state.compare_exchange_strong(screenshot_queued, 2, std::memory_order_acq_rel)) {
      if (!g_active) trace_home_regions("before_screenshot");
      screen_snap_stream(g_display);
      if (!g_active) trace_home_regions("after_screenshot");
      g_screenshot_state.store(0, std::memory_order_release);
    }
    finish_home_trace_frame();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
}  // namespace

namespace home_app {
bool request_rotation(int degrees) {
  if (degrees!=0 && degrees!=90 && degrees!=180 && degrees!=270) return false;
  return orientation_app::request_manual(degrees);
}
bool request_screenshot() {
  int idle = 0;
  return g_screenshot_state.compare_exchange_strong(idle, 1, std::memory_order_acq_rel);
}
int rotation() {return g_rotation.load();}
int viewport_width() {return g_view_width.load();}
int viewport_height() {return g_view_height.load();}
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
  const ::screens::Screen* s = ::screens::find_name(name);
  if (!s) return false;
  request_open(s->id);
  return true;
}
const char* screen_name() {
  static char name[16];
  const ::screens::Screen* s = g_active;
  if (!s) return "home";
  std::snprintf(name, sizeof(name), "%s", s->name);
  for (char* c = name; *c; ++c) *c = static_cast<char>(std::tolower(static_cast<unsigned char>(*c)));
  return name;
}

bool start(gfx::ScaledDisplay* display) {
  g_display = display;
  // Volume and mute come back as the user left them.
  g_volume = ui_support::get_u8("volume", g_volume);
  g_sound = ui_support::get_bool("sound", g_sound);
  ::screens::apply_saved_location();
  dashboards::load_recent(nullptr, 0);  // the Tab5's default recent list
  return xTaskCreatePinnedToCore(ui_task, "home_ui", 12288, nullptr, 4, nullptr, 0) == pdPASS;
}
}  // namespace home_app
