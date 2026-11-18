// AIS (maritime VHF) on the P4 console: the shared ais_app / ais_radio / pipeline (apps/orcsdr-tab5/ui, unchanged) decode the receiver's
// 240 kS/s stream centred on 162.000 MHz and the compact dashboard (ais_compact_dashboard.cpp) shows TARGETS, MESSAGES, SIGNAL and SETUP.
// Receive only. Not on this target yet: the map page, the protocol and events pages, the SD journal, NMEA output, long range.
#include "ais_app.hpp"
#include "ais_compact_dashboard.hpp"
#include "ais_radio.hpp"
#include "audio_service.hpp"
#include "esp_timer.h"
#include "home_app.hpp"
#include "iq_tap.hpp"
#include "present.hpp"
#include "radio_service.hpp"
#include "receiver_location.hpp"
#include "screens.hpp"
#include "sd_card.hpp"
#include "orcsdr_storage.hpp"
#include "ui_support.hpp"
#include "M5Unified.h"

namespace {
namespace ais = orcsdr::ais_ui;

bool resize_viewport() {
  const auto* g = present_geometry();
  return ais::set_viewport(g ? ais::compact::Rect{int(g->safe.x), int(g->safe.y), int(g->safe.width), int(g->safe.height)}
                             : ais::compact::Rect{0, 0, 1280, 720});
}

void consume(const uint8_t* iq, size_t bytes, void*) { orcsdr::ais_radio::offer_iq(iq, bytes, orcsdr::radio::sample_rate()); }

void install_hooks() {
  orcsdr::ais_radio::Hooks radio = {};
  radio.device_ready = [] { return orcsdr::radio::snapshot().receiver_state == 2; };
  radio.tune = [](uint32_t center_hz, uint32_t sample_rate) {
    if (orcsdr::radio::snapshot().receiver_state != 2) return false;
    orcsdr::radio::set_sample_rate_override(sample_rate);
    home_app::request_frequency(center_hz);
    return true;
  };
  radio.release = [] { orcsdr::radio::set_sample_rate_override(0); };
  radio.tuned_center = [] { return home_app::status().frequency_hz; };
  radio.now_ms = [] { return static_cast<uint64_t>(esp_timer_get_time() / 1000); };
  radio.micros = [] { return static_cast<uint32_t>(esp_timer_get_time()); };
  radio.lost_samples = [] { return static_cast<uint64_t>(iq_tap::dropped()) * 8192u; };   // whole blocks the consumer could not take
  orcsdr::ais_radio::install(radio);

  orcsdr::ais_app::Hooks app = {};
  app.header_controls = ui_support::draw_header;
  app.battery_percent = [] { return static_cast<int>(M5.Power.getBatteryLevel()); };
  app.receiver_location = [](int32_t* lat_e7, int32_t* lon_e7) {
    const auto place = orcsdr::receiver_location::snapshot();
    if (!place.configured) return false;
    *lat_e7 = place.latitude_e7;
    *lon_e7 = place.longitude_e7;
    return true;
  };
  app.sd_available = [] { return orcsdr::storage::mounted(); };
  app.now_ms = ui_support::now_ms;
  app.overlay_open = ui_support::header_overlay_open;
  app.go_home = [] { home_app::request_home(); };
  orcsdr::ais_app::install(app);
}

void enter() {
  if (!resize_viewport()) {
    home_app::request_home();
    return;
  }
  audio_service::set_active(false);   // AIS is data: no audio
  ui_support::header_reset();
  ui_support::claim_screen(orcsdr::screens::Id::ais);
  install_hooks();
  iq_tap::start("ais", consume, nullptr, 256 * 1024, 1, 3, 16384, true);   // PSRAM stack, as for FT8
  orcsdr::ais_app::open();
}

void leave() {
  orcsdr::ais_app::close();
  iq_tap::stop();
  orcsdr::radio::set_sample_rate_override(0);
}

void tick() {
  orcsdr::ais_app::tick();
  ui_support::header_service();
}

void tap(int x, int y) {
  if (ui_support::handle_header_touch(x, y, true)) return;
  orcsdr::ais_app::touch(x, y);
}
}  // namespace

namespace screens {
const Screen& ais_screen() {
  static const Screen s{orcsdr::dashboards::Id::ais, "AIS", enter, leave, tick, tap, nullptr, nullptr, nullptr, []() {
    if (!resize_viewport()) return false;
    orcsdr::ais_app::draw(true);
    return true;
  }};
  return s;
}
}  // namespace screens
