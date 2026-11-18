// MAPS on the P4 console: the compact screen of the map packs on the card (maps_compact_dashboard.cpp) over the shared map service.
// Reached from Settings > Data & Maps > LOCAL MAPS. Not on this target yet: the location picker and the download page.
#include "home_app.hpp"
#include "map_service.hpp"
#include "maps_compact_dashboard.hpp"
#include "orcsdr_storage.hpp"
#include "present.hpp"
#include "screens.hpp"
#include "ui_support.hpp"

namespace {
namespace mu = orcsdr::maps_ui;

bool resize_viewport() {
  const auto* g = present_geometry();
  return mu::set_viewport(g ? mu::compact::Rect{int(g->safe.x), int(g->safe.y), int(g->safe.width), int(g->safe.height)}
                           : mu::compact::Rect{0, 0, 1280, 720});
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
  if (!mu::enter()) home_app::request_home();
}

void leave() { mu::leave(); }

void tick() {
  if (!ui_support::header_overlay_open()) mu::update();
  ui_support::header_service();
}

void tap(int x, int y) {
  if (ui_support::handle_header_touch(x, y, true)) return;
  (void)mu::touch(x, y);
}
}  // namespace

namespace screens {
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
