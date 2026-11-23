#include "digital_voice_app.hpp"

#include <M5Unified.h>
#include <esp_heap_caps.h>

#include <cstdio>
#include <new>

#include "digital_voice_dashboard_surface.hpp"
#include "screen_controller.hpp"

namespace orcsdr::digital_voice_app {
namespace {
using namespace digital_voice_ui;

// Surface + Snapshot live in PSRAM for the lifetime of a visit: the Tab5 internal boot pool is nearly full.
struct State {
  Surface surface;
  Snapshot snapshot;
};
State* g_state = nullptr;
Hooks g_hooks{};
uint32_t g_open_failed = 0;
uint32_t g_unapplied_actions = 0;   // radio actions returned by the dashboard that nothing owns yet
uint32_t g_repaints = 0;

uint32_t now() { return g_hooks.now_ms ? g_hooks.now_ms() : 0; }
bool overlay() { return g_hooks.overlay_open && g_hooks.overlay_open(); }

bool ensure_state() {
  if (g_state) return true;
  void* mem = heap_caps_calloc(1, sizeof(State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!mem) return false;
  g_state = new (mem) State();
  return true;
}
void free_state() {
  if (!g_state) return;
  g_state->~State();
  heap_caps_free(g_state);
  g_state = nullptr;
}

// Only facts the firmware can prove. No scanner exists yet, so can_scan stays false and the UI says so.
void refresh(Snapshot& s) {
  const Receiver r = (g_hooks.receiver_ready && g_hooks.receiver_ready()) ? Receiver::ready : Receiver::disconnected;
  if (s.receiver != r) {
    s.receiver = r;
    ++s.revision;
  }
}

void leave_to_home() {
  close();
  if (g_hooks.go_home) g_hooks.go_home();
}

// Full-frame draw once the transition has finished (Surface refuses to paint while a transition is running).
bool full_draw() {
  g_state->surface.enter();
  M5.Display.fillScreen(TFT_BLACK);
  return g_state->surface.draw(M5.Display, g_state->snapshot, overlay(), g_hooks.header_controls);
}
}  // namespace

void install(const Hooks& hooks) { g_hooks = hooks; }

void open() {
  if (!ensure_state()) {
    ++g_open_failed;
    if (g_hooks.go_home) g_hooks.go_home();
    return;
  }
  new (&g_state->snapshot) Snapshot();
  refresh(g_state->snapshot);
  g_state->snapshot.revision = 1;
  screens::begin_transition(screens::Id::digital_voice, now());
  screens::finish_transition();
  if (!full_draw()) {
    ++g_open_failed;
    leave_to_home();
  }
}

void close() {
  if (!g_state) return;
  g_state->surface.leave();
  free_state();
}

void tick() {
  if (!g_state) return;
  refresh(g_state->snapshot);
  if (g_state->surface.update(M5.Display, g_state->snapshot, overlay())) {
    ++g_repaints;
    screens::note_visible_update(screens::Id::digital_voice);
  }
}

void draw(bool static_panel) {
  if (!g_state) return;
  if (static_panel) {
    screens::begin_transition(screens::Id::digital_voice, now());
    screens::finish_transition();
    if (!full_draw()) leave_to_home();
    return;
  }
  tick();
}

void touch(int32_t x, int32_t y) {
  if (!g_state) return;
  const Action action = g_state->surface.touch(x, y, g_state->snapshot, overlay());
  if (action.kind != ActionKind::none) ++g_unapplied_actions;   // scan/hold/skip/save: no owner yet, never faked
}

bool active() { return g_state != nullptr && g_state->surface.active(); }

void describe(char* out, size_t size) {
  if (!out || size == 0) return;
  if (!g_state) {
    std::snprintf(out, size, "RTL_DV_UI open=0 open_failed=%lu", static_cast<unsigned long>(g_open_failed));
    return;
  }
  const UiState& ui = g_state->surface.state();
  std::snprintf(out, size,
                "RTL_DV_UI open=1 view=%u density=%u receiver=%s scan=%s can_scan=%u rev=%lu repaints=%lu "
                "unapplied_actions=%lu open_failed=%lu",
                static_cast<unsigned>(ui.view), static_cast<unsigned>(ui.density),
                receiver_name(g_state->snapshot.receiver), scan_phase_name(g_state->snapshot.scan.phase),
                g_state->snapshot.can_scan ? 1u : 0u, static_cast<unsigned long>(g_state->snapshot.revision),
                static_cast<unsigned long>(g_repaints), static_cast<unsigned long>(g_unapplied_actions),
                static_cast<unsigned long>(g_open_failed));
}

}  // namespace orcsdr::digital_voice_app
