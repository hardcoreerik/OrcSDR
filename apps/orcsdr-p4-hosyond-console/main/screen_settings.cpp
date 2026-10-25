// SETTINGS (apps/orcsdr-tab5/ui/settings_app.cpp, unchanged) on the P4 console. Display brightness, volume and
// the receiver defaults work; Wi-Fi, SD card, catalog and companion sections show their unavailable state
// because this board has none of them wired yet.
#include <algorithm>
#include <cstring>
#include "esp_rtl_sdr.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "home_app.hpp"
#include "hosyond_board.h"
#include "nvs_store.hpp"
#include "radio_service.hpp"
#include "screens.hpp"
#include "settings_app.hpp"

namespace {
namespace st = orcsdr::settings;
st::State g_state;
bool g_loaded = false;

void load() {
  g_state.brightness = 255;
  g_state.rotation = 1;
  g_state.volume = 128;
  g_state.sound_default = true;
  g_state.auto_start_reception = true;
  g_state.graphics_default = true;
  orcsdr::NvsStore s;
  if (s.begin("orcsdr_set", true)) {
    g_state.brightness = s.get_u8("bright", g_state.brightness);
    g_state.screen_timeout_sec = s.get_u16("timeout", 0);
    g_state.sound_default = s.get_bool("sound", true);
    g_state.auto_start_reception = s.get_bool("autostart", true);
    g_state.graphics_default = s.get_bool("graphics", true);
    s.end();
  }
  g_loaded = true;
}

void save() {
  orcsdr::NvsStore s;
  if (!s.begin("orcsdr_set", false)) return;
  s.put_u8("bright", g_state.brightness);
  s.put_u16("timeout", g_state.screen_timeout_sec);
  s.put_bool("sound", g_state.sound_default);
  s.put_bool("autostart", g_state.auto_start_reception);
  s.put_bool("graphics", g_state.graphics_default);
  s.end();
}

const st::State& build() {
  const home_app::Status h = home_app::status();
  g_state.volume = h.volume;
  g_state.fm_frequency_hz = h.frequency_hz;
  std::snprintf(g_state.default_band, sizeof(g_state.default_band), "%.15s", h.band);
  g_state.rtl_usb_safe_mode = esp_rtl_sdr_usb_safe_mode_active();
  g_state.battery_level = -1;
  g_state.uptime_seconds = static_cast<uint32_t>(esp_timer_get_time() / 1000000);
  std::snprintf(g_state.build_identity, sizeof(g_state.build_identity), "P4 console %s", __DATE__);
  g_state.wifi_ready = false;
  g_state.sd_ready = false;
  g_state.companion_supported = false;
  return g_state;
}

void enter() {
  if (!g_loaded) load();
  st::enter(build(), st::Section::display_audio);
}
void leave() { st::leave(); }
void tick() { st::update(build()); }

void tap(int x, int y) {
  const st::Action a = st::handle_touch(x, y);
  switch (a.kind) {
    case st::ActionKind::close: home_app::request_home(); break;
    case st::ActionKind::brightness_changed:
      g_state.brightness = static_cast<uint8_t>(std::clamp<int32_t>(a.value, 8, 255));
      (void)hosyond_board_set_backlight(g_state.brightness);
      save();
      break;
    case st::ActionKind::timeout_changed:
      g_state.screen_timeout_sec = static_cast<uint16_t>(std::max<int32_t>(0, a.value));
      save();
      break;
    case st::ActionKind::volume_changed: home_app::request_volume(static_cast<uint8_t>(std::clamp<int32_t>(a.value, 0, 255))); break;
    case st::ActionKind::sound_changed:
      g_state.sound_default = a.value != 0;
      home_app::request_sound(g_state.sound_default);
      save();
      break;
    case st::ActionKind::auto_start_changed:
      g_state.auto_start_reception = a.value != 0;
      save();
      break;
    case st::ActionKind::graphics_changed:
      g_state.graphics_default = a.value != 0;
      save();
      break;
    case st::ActionKind::rtl_usb_safe_mode_reset:
      (void)esp_rtl_sdr_usb_fault_guard_reset();
      vTaskDelay(pdMS_TO_TICKS(200));
      esp_restart();
      break;
    default: break;  // Wi-Fi, location, catalog, companion, clock: not available on this target yet
  }
}
}  // namespace

namespace screens {
const Screen& settings_screen() {
  static const Screen s{orcsdr::dashboards::Id::settings, "SETTINGS", enter, leave, tick, tap, nullptr, nullptr};
  return s;
}
}  // namespace screens
