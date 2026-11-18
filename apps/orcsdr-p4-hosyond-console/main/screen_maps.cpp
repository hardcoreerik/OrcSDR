// MAPS on the P4 console: the compact screen of the map packs on the card (maps_compact_dashboard.cpp) over the shared map service.
// Reached from Settings > Data & Maps > LOCAL MAPS. Not on this target yet: the location picker and the download page.
#include "home_app.hpp"
#include "map_service.hpp"
#include "maps_compact_dashboard.hpp"
#include "orcsdr_storage.hpp"
#include "present.hpp"
#include "receiver_location.hpp"
#include "screens.hpp"
#include "ui_support.hpp"
#include "wifi_app.hpp"
#include <cstdio>

namespace {
namespace mu = orcsdr::maps_ui;

bool resize_viewport() {
  const auto* g = present_geometry();
  return mu::set_viewport(g ? mu::compact::Rect{int(g->safe.x), int(g->safe.y), int(g->safe.width), int(g->safe.height)}
                           : mu::compact::Rect{0, 0, 1280, 720});
}

bool g_picker_requested = false, g_wizard_requested = false;

mu::WizardHooks wizard_hooks() {
  mu::WizardHooks h;
  h.wifi_line = [](char* out, size_t size) {
    const wifi_app::Status s = wifi_app::status();
    if (s.connected) std::snprintf(out, size, "%s  %s", s.ssid, s.ip);
    else std::snprintf(out, size, "%s", !s.started ? "Wi-Fi co-processor starting" : s.connecting ? "connecting..." : s.power ? "no network joined" : "Wi-Fi is off");
    return s.connected;
  };
  h.location_line = [](char* out, size_t size) {
    const auto here = orcsdr::receiver_location::snapshot();
    if (here.configured) std::snprintf(out, size, "%.4f, %.4f", here.latitude_e7 / 1e7, here.longitude_e7 / 1e7);
    else out[0] = 0;
    return here.configured;
  };
  h.open_wifi = [] { home_app::request_open(orcsdr::dashboards::Id::settings); };
  h.save_record = [](uint32_t record) { ui_support::put_u32("maps_setup", record); return true; };
  h.finished = [] { home_app::request_open(orcsdr::dashboards::Id::settings); };
  return h;
}

void enter() {
  mu::set_header_controls(ui_support::draw_header);
  if (!resize_viewport()) {
    home_app::request_home();
    return;
  }
  if (orcsdr::storage::mounted()) (void)orcsdr::maps::begin(&orcsdr::storage::filesystem());
  ui_support::header_reset();
  ui_support::claim_screen(orcsdr::screens::Id::maps);
  const auto here = orcsdr::receiver_location::snapshot();
  mu::set_start_location(here.latitude_e7, here.longitude_e7);
  if (!mu::enter()) { home_app::request_home(); return; }
  mu::set_wizard_hooks(wizard_hooks());
  if (g_wizard_requested) { g_wizard_requested = false; (void)mu::open_wizard(); }
  else if (g_picker_requested) { g_picker_requested = false; (void)mu::open_picker(); }
}

void leave() { mu::leave(); }

void tick() {
  if (!ui_support::header_overlay_open()) mu::update();
  ui_support::header_service();
}

void tap(int x, int y) {
  if (ui_support::handle_header_touch(x, y, true)) return;
  if (mu::touch(x, y) == mu::Action::use_location) {
    int32_t lat = 0, lon = 0;
    if (mu::selection(&lat, &lon)) screens::settings_set_location(lat, lon);
    if (!mu::finish_picker()) home_app::request_open(orcsdr::dashboards::Id::settings);
  }
}
}  // namespace

namespace screens {
void maps_request_picker() { g_picker_requested = true; }
void maps_request_wizard() { g_wizard_requested = true; }
// Not a Home dashboard: it has no registry entry, so it carries the registry's `count` as its id and is opened by name or from Settings.
const Screen& maps_screen() {
  static const Screen s{orcsdr::dashboards::Id::count, "MAPS", enter, leave, tick, tap, nullptr, nullptr, nullptr, []() {
    if (!resize_viewport()) return false;
    mu::draw();
    return true;
  }};
  return s;
}
}  // namespace screens
