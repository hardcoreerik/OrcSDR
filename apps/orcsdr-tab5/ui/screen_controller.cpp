#include "screen_controller.hpp"

#include <atomic>
#include <initializer_list>

namespace orcsdr::screens {
namespace {
Status g_status{};
std::atomic<bool> g_transitioning{false};
}

void begin_transition(Id next, uint32_t now_ms, bool remember_return) {
  g_transitioning.store(true, std::memory_order_release);
  if (next == Id::settings && remember_return && g_status.active != Id::settings &&
      g_status.active != Id::none)
    g_status.return_to = g_status.active;
  g_status.active = next;
  g_status.last_transition_ms = now_ms;
  ++g_status.transitions;
}

void finish_transition() { g_transitioning.store(false, std::memory_order_release); }

bool transitioning() { return g_transitioning.load(std::memory_order_acquire); }

Id close_settings(uint32_t now_ms) {
  const Id target = g_status.return_to == Id::none ? Id::home : g_status.return_to;
  begin_transition(target, now_ms, false);
  return target;
}

bool owns(Id id) { return !transitioning() && g_status.active == id; }

bool is_active(Id id) { return g_status.active == id; }

bool may_draw(Id id) {
  if (transitioning() || g_status.active != id) {
    ++g_status.rejected_draws;
    return false;
  }
  return true;
}

void note_visible_update(Id id) {
  if (may_draw(id)) ++g_status.visible_updates;
}

const Status& status() { return g_status; }

const char* name(Id id) {
  switch (id) {
    case Id::home: return "home";
    case Id::fm: return "fm";
    case Id::p25: return "p25";
    case Id::adsb: return "adsb";
    case Id::lora: return "lora";
    case Id::radio: return "radio";
    case Id::settings: return "settings";
    case Id::documentation: return "documentation";
    default: return "none";
  }
}

bool self_check() {
  const Status saved = g_status;
  const bool saved_transitioning = g_transitioning;
  g_status = {};
  begin_transition(Id::home, 10, false);
  const bool entering_blocks_draw = !may_draw(Id::home);
  finish_transition();
  const bool home_owns = owns(Id::home) && may_draw(Id::home);
  bool settings_return = true;
  uint32_t now = 20;
  for (const Id screen : {Id::home, Id::fm, Id::p25, Id::adsb, Id::lora}) {
    begin_transition(screen, now++, false);
    finish_transition();
    begin_transition(Id::settings, now++, true);
    finish_transition();
    settings_return = settings_return && close_settings(now++) == screen;
    finish_transition();
    settings_return = settings_return && owns(screen);
  }
  begin_transition(Id::documentation, now++, false);
  finish_transition();
  const bool documentation_owns = owns(Id::documentation);
  begin_transition(Id::home, now++, false);
  finish_transition();
  const bool documentation_restores = owns(Id::home);
  const bool restored = g_status.transitions == 18;
  g_status = saved;
  g_transitioning = saved_transitioning;
  return entering_blocks_draw && home_owns && settings_return && documentation_owns &&
         documentation_restores && restored;
}

}  // namespace orcsdr::screens
