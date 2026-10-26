#include "ui_support.hpp"
#include <ctime>
#include "M5Unified.h"
#include "dashboard_audio_control.hpp"
#include "esp_timer.h"
#include "home_app.hpp"
#include "nvs_store.hpp"
#include "radio_service.hpp"

namespace ui_support {
namespace {
orcsdr::audio_header::Control g_control;

orcsdr::NvsStore* open_store(orcsdr::NvsStore& s, bool read_only) {
  return s.begin("orcsdr_ui", read_only) ? &s : nullptr;
}
}  // namespace

uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

bool sound_enabled() { return home_app::status().sound; }

void draw_header() {
  const home_app::Status st = home_app::status();
  orcsdr::audio_header::draw(g_control, st.volume, st.sound, -1);
  orcsdr::audio_header::draw_home_button();
  orcsdr::audio_header::draw_settings_button();
  orcsdr::audio_header::draw_mute_button(st.sound);
  orcsdr::audio_header::draw_visualizer_button(orcsdr::radio::snapshot().receiver_state == 2);
}

bool header_overlay_open() { return g_control.expanded; }
void header_reset() { orcsdr::audio_header::reset(g_control); }

bool handle_header_touch(int x, int y) {
  const home_app::Status st = home_app::status();
  const auto action = orcsdr::audio_header::handle_touch(g_control, x, y, now_ms(), st.volume);
  if (action != orcsdr::audio_header::Action::none) {
    if (action == orcsdr::audio_header::Action::opened) {
      orcsdr::audio_header::capture_volume_background();
    } else if (action == orcsdr::audio_header::Action::volume_set) {
      home_app::request_volume(g_control.volume);
      home_app::request_sound(g_control.volume > 0);
    } else if (action == orcsdr::audio_header::Action::mute_toggle) {
      home_app::request_sound(!st.sound);
    } else if (action == orcsdr::audio_header::Action::closed) {
      orcsdr::audio_header::restore_volume_background();
      return true;
    }
    draw_header();
    return true;
  }
  if (orcsdr::audio_header::visualizer_hit(x, y)) return true;  // the RF visualizer is not available on this target
  if (orcsdr::audio_header::home_hit(x, y)) {
    home_app::request_home();
    return true;
  }
  if (orcsdr::audio_header::settings_hit(x, y)) {
    home_app::request_open(orcsdr::dashboards::Id::settings);
    return true;
  }
  return false;
}

void header_service() {
  if (orcsdr::audio_header::service_timeout(g_control, now_ms())) orcsdr::audio_header::restore_volume_background();
}

bool get_bool(const char* key, bool fallback) {
  orcsdr::NvsStore s;
  if (!open_store(s, true)) return fallback;
  const bool v = s.get_bool(key, fallback);
  s.end();
  return v;
}
void put_bool(const char* key, bool value) {
  orcsdr::NvsStore s;
  if (!open_store(s, false)) return;
  s.put_bool(key, value);
  s.end();
}
uint8_t get_u8(const char* key, uint8_t fallback) {
  orcsdr::NvsStore s;
  if (!open_store(s, true)) return fallback;
  const uint8_t v = s.get_u8(key, fallback);
  s.end();
  return v;
}
void put_u8(const char* key, uint8_t value) {
  orcsdr::NvsStore s;
  if (!open_store(s, false)) return;
  s.put_u8(key, value);
  s.end();
}
uint32_t get_u32(const char* key, uint32_t fallback) {
  orcsdr::NvsStore s;
  if (!open_store(s, true)) return fallback;
  const uint32_t v = s.get_u32(key, fallback);
  s.end();
  return v;
}
void put_u32(const char* key, uint32_t value) {
  orcsdr::NvsStore s;
  if (!open_store(s, false)) return;
  s.put_u32(key, value);
  s.end();
}

uint32_t utc_epoch() {
  const time_t t = time(nullptr);
  return t > 1700000000 ? static_cast<uint32_t>(t) : 0u;  // before NTP the clock reads near 1970
}

void claim_screen(orcsdr::screens::Id id) {
  orcsdr::screens::begin_transition(id, now_ms(), false);
  orcsdr::screens::finish_transition();
}

}  // namespace ui_support
