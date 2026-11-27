#include "digital_voice_dashboard_surface.hpp"
#include "screen_controller.hpp"
#include "dashboard_info_button.hpp"

namespace orcsdr::digital_voice_ui {
namespace {
constexpr screens::Id kOwner = screens::Id::digital_voice;
}
bool Surface::draw(lgfx::LovyanGFX& display, const Snapshot& snapshot,
                   bool overlay_open, HeaderHook shared_header) {
  if (!screens::owns(kOwner) || overlay_open) return false;
  if (!dashboard_.active() && shared_header) shared_header();
  return dashboard_.present(display, snapshot, state_, true, false);
}
bool Surface::update(lgfx::LovyanGFX& display, const Snapshot& snapshot, bool overlay_open) {
  if (!screens::owns(kOwner) || overlay_open) return false;
  return dashboard_.present(display, snapshot, state_, true, false);
}
Action Surface::touch(int32_t x, int32_t y, const Snapshot& snapshot, bool overlay_open) {
  if (!screens::owns(kOwner) || overlay_open) return {};
  if (info_button::hit(x, y)) {                 // the standard (i) in the header: UI only
    apply_ui_action(state_, Action{ActionKind::toggle_info, 0}, snapshot);
    return {};
  }
  if (y < 104) return {};
  const Action action = dashboard_.handle_touch(x, y, snapshot, state_);
  if (apply_ui_action(state_, action, snapshot)) return {}; // UI-only, repaint next tick
  return action; // receiver-owned scan/hold/skip/save; caller validates again
}
} // namespace orcsdr::digital_voice_ui
