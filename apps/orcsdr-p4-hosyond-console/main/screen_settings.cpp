// SETTINGS (apps/orcsdr-tab5/ui/settings_app.cpp, unchanged) on the P4 console. Display brightness, volume and
// the receiver defaults work; Wi-Fi, SD card, catalog and companion sections show their unavailable state
// because this board has none of them wired yet.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include "esp_rtl_sdr.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "home_app.hpp"
#include "hosyond_board.h"
#include "nvs_store.hpp"
#include "radio_service.hpp"
#include "receiver_location.hpp"
#include "screens.hpp"
#include "settings_app.hpp"
#include "wifi_app.hpp"

namespace {
namespace st = orcsdr::settings;
st::State g_state;
bool g_loaded = false;

// ZIP 97477 (Springfield, Oregon) is the default receiver location until the owner sets another one.
constexpr int32_t kDefaultLatE7 = 440720000, kDefaultLonE7 = -1229600000;

void load_location() {
  bool set = true;
  int32_t lat = kDefaultLatE7, lon = kDefaultLonE7;
  char label[40] = "ZIP 97477 Springfield, OR";
  orcsdr::NvsStore s;
  if (s.begin("orcsdr_set", true)) {
    if (s.get_bool("loc_set", false)) {
      lat = s.get_i32("loc_lat", lat);
      lon = s.get_i32("loc_lon", lon);
      const std::string saved = s.get_string("loc_label", label);
      std::snprintf(label, sizeof(label), "%.39s", saved.c_str());
    }
    s.end();
  }
  orcsdr::receiver_location::set(set, lat, lon, label, "");
}

void save_location(const st::State& v) {
  orcsdr::NvsStore s;
  if (!s.begin("orcsdr_set", false)) return;
  s.put_bool("loc_set", true);
  s.put_i32("loc_lat", v.latitude_e7);
  s.put_i32("loc_lon", v.longitude_e7);
  s.put_string("loc_label", v.location_label);
  s.end();
  orcsdr::receiver_location::set(v.location_configured, v.latitude_e7, v.longitude_e7, v.location_label, "");
}

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
  {
    const wifi_app::Status w = wifi_app::status();
    g_state.wifi_power_enabled = w.power;
    g_state.wifi_start_at_boot = w.start_at_boot;
    g_state.wifi_ready = w.started;
    g_state.wifi_scanning = wifi_app::scanning();
    g_state.wifi_connected = w.connected;
    g_state.wifi_connecting = w.connecting;
    g_state.wifi_hosted_transport_ready = w.started || (w.c6_version[0] != 0);
    g_state.wifi_hosted_update_required = false;
    std::snprintf(g_state.wifi_ssid, sizeof(g_state.wifi_ssid), "%.32s", w.ssid);
    std::snprintf(g_state.wifi_ip, sizeof(g_state.wifi_ip), "%.15s", w.ip);
    std::snprintf(g_state.wifi_hosted_c6_version, sizeof(g_state.wifi_hosted_c6_version), "%.15s", w.c6_version);
    std::snprintf(g_state.wifi_message, sizeof(g_state.wifi_message), "%.47s",
                  w.started ? (w.connected ? "Connected" : w.connecting ? "Connecting..." : "Ready") : (w.failure_stage[0] ? w.failure_stage : "Starting the C6"));
    g_state.wifi_rssi = w.rssi;
    orcsdr::wifi::ScanResult found[6];
    const int n = wifi_app::scan_results(found, 6);
    g_state.network_count = static_cast<uint8_t>(n);
    for (int i = 0; i < n; ++i) {
      std::snprintf(g_state.networks[i].ssid, sizeof(g_state.networks[i].ssid), "%.32s", found[i].ssid);
      g_state.networks[i].rssi = found[i].rssi;
      g_state.networks[i].secure = found[i].secure;
      g_state.networks[i].saved = w.saved && std::strcmp(found[i].ssid, w.saved_ssid) == 0;
    }
    g_state.saved_network_count = w.saved ? 1 : 0;
    std::snprintf(g_state.profiles[0].ssid, sizeof(g_state.profiles[0].ssid), "%.32s", w.saved_ssid);
    g_state.profiles[0].connected = w.connected && std::strcmp(w.ssid, w.saved_ssid) == 0;
  }
  {
    const orcsdr::wifi::C6UpdateStatus up = orcsdr::wifi::c6_update_status();
    g_state.wifi_c6_image_embedded = up.image_embedded;
    g_state.wifi_c6_update_percent = up.progress_percent;
    std::snprintf(g_state.wifi_hosted_host_version, sizeof(g_state.wifi_hosted_host_version), "3.0.6");
    std::snprintf(g_state.wifi_c6_update_state, sizeof(g_state.wifi_c6_update_state), "%.19s", orcsdr::wifi::c6_update_state_name(up.state));
    std::snprintf(g_state.wifi_c6_update_stage, sizeof(g_state.wifi_c6_update_stage), "%.23s", up.stage);
    g_state.wifi_hosted_update_required = up.state == orcsdr::wifi::C6UpdateState::optional || up.state == orcsdr::wifi::C6UpdateState::ready;
  }
  {
    const auto loc = orcsdr::receiver_location::snapshot();
    g_state.location_configured = loc.configured;
    g_state.latitude_e7 = loc.latitude_e7;
    g_state.longitude_e7 = loc.longitude_e7;
    std::snprintf(g_state.location_label, sizeof(g_state.location_label), "%.39s", loc.label);
  }
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
    case st::ActionKind::c6_update_confirm: (void)orcsdr::wifi::begin_c6_update(); break;
    case st::ActionKind::wifi_power_changed: wifi_app::set_power(a.value != 0); break;
    case st::ActionKind::wifi_start_at_boot_changed: wifi_app::set_start_at_boot(a.value != 0); break;
    case st::ActionKind::scan_wifi: (void)wifi_app::request_scan(); break;
    case st::ActionKind::connect_wifi: {
      char ssid[33]{}, password[64]{};
      if (st::take_wifi_credentials(ssid, sizeof(ssid), password, sizeof(password))) (void)wifi_app::connect(ssid, password, true);
      std::memset(password, 0, sizeof(password));
      break;
    }
    case st::ActionKind::connect_saved_wifi: {
      const wifi_app::Status w = wifi_app::status();
      if (w.saved) {
        // Saved credentials are re-read from NVS by the Wi-Fi task; a reconnect request reuses them.
        wifi_app::disconnect();
      }
      break;
    }
    case st::ActionKind::forget_wifi:
      wifi_app::disconnect();
      wifi_app::forget();
      break;
    case st::ActionKind::location_changed: save_location(st::state()); break;
    default: break;  // catalog, companion, clock: not available on this target yet
  }
}
}  // namespace

namespace screens {
void apply_saved_location() { load_location(); }
const Screen& settings_screen() {
  static const Screen s{orcsdr::dashboards::Id::settings, "SETTINGS", enter, leave, tick, tap, nullptr, nullptr};
  return s;
}
}  // namespace screens
