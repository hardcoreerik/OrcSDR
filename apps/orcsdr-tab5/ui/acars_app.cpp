#include "acars_app.hpp"

#include <M5Unified.h>
#include <esp_heap_caps.h>

#include <cstdio>
#include <new>

#include "acars_dashboard.hpp"
#include "acars_dashboard_ui.hpp"
#include "acars_receiver.hpp"
#include "screen_controller.hpp"

namespace orcsdr::acars_app {
namespace {

// Model (two bounded stores, ~50 KB) and Controller are allocated once, lazily, outside internal-DRAM BSS --
// the same reason the POCSAG dashboard allocates its snapshot lazily. There is deliberately no static
// fallback object: it would reserve the full size in BSS at link time even if never used.
struct State {
  acars_ui::Model model;
  acars_ui::Controller ui;
};

State* g_state = nullptr;
Hooks g_hooks;

bool ensure_state() {
  if (g_state) return true;
  void* memory = heap_caps_malloc(sizeof(State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!memory) memory = heap_caps_malloc(sizeof(State), MALLOC_CAP_8BIT);
  if (!memory) return false;
  g_state = new (memory) State();
  return true;
}

uint32_t g_last_device_sync_ms = 0;

void sync_device_state() {
  g_last_device_sync_ms = millis();
  g_state->model.set_device_state(M5.Power.getBatteryLevel(),
                                  g_hooks.sound_enabled ? g_hooks.sound_enabled() : true);
}

}  // namespace

void install(const Hooks& hooks) { g_hooks = hooks; }

void open() {
  if (!ensure_state()) return;
  g_state->model.set_store(g_hooks.store);
  g_state->model.load_preferences();
  g_state->ui.reset();
  sync_device_state();
  acars_dashboard::install(&g_state->model, &g_state->ui, g_hooks.header_controls);
  screens::begin_transition(screens::Id::acars, millis());
  acars_dashboard::enter();
  screens::finish_transition();
  // Hooks are installed only by the firmware; without them (host tests) the feed stays "not connected".
  (void)acars_receiver::start(&g_state->model);
}

void close() {
  if (!acars_dashboard::active()) return;
  acars_receiver::stop();                             // gives the radio's sample rate back
  acars_dashboard::leave();
  if (g_state) g_state->model.set_demo(false);        // synthetic data never outlives the screen
}

void tick() {
  if (!g_state || !acars_dashboard::active()) return;
  if (millis() - g_last_device_sync_ms >= 2000u) sync_device_state();   // the power IC is not read every loop
  g_state->model.service();
  acars_receiver::service();
  if (!screens::owns(screens::Id::acars)) return;
  screens::note_visible_update(screens::Id::acars);
  acars_dashboard::update(millis());
}

void draw(bool static_panel) {
  if (!g_state) return;
  if (!static_panel && (!acars_dashboard::active() || !screens::may_draw(screens::Id::acars))) return;
  if (static_panel) {
    acars_dashboard::enter();                         // full redraw after Settings / Visualizer
  } else {
    acars_dashboard::update(millis());
  }
}

void touch(int32_t x, int32_t y) {
  if (!g_state || !acars_dashboard::active()) return;
  const acars_ui::Action action = g_state->ui.touch(x, y, g_state->model);
  if (action.kind == acars_ui::ActionKind::tune_requested) {
    (void)acars_receiver::request_channel(action.value);
    if (g_hooks.request_tune) g_hooks.request_tune(action.value);
  }
  acars_dashboard::update(millis());
}

bool active() { return acars_dashboard::active(); }

void describe(char* out, size_t size) {
  if (!out || size == 0) return;
  if (!g_state) { std::snprintf(out, size, "RTL_ACARS_UI open=0"); return; }
  static const char* const kViews[] = {"live", "aircraft", "channels", "activity", "archive", "setup"};
  static const char* const kReceiver[] = {"disconnected", "unavailable", "paused", "running", "error"};
  static const char* const kTune[] = {"unavailable", "pending", "tuned", "failed"};
  const acars_ui::Snapshot& s = g_state->model.snapshot();
  const size_t view = static_cast<size_t>(g_state->ui.view());
  const size_t receiver = static_cast<size_t>(s.receiver);
  const size_t tune = static_cast<size_t>(s.tune);
  std::snprintf(out, size,
                "RTL_ACARS_UI open=%d view=%s detail=%d demo=%d scenario=%u receiver=%s live_reception=%d tune=%s requested_hz=%u tuned_hz=%u "
                "held=%u total=%u corrected=%u queue_drops=%u aircraft=%u",
                acars_dashboard::active() ? 1 : 0, view < 6 ? kViews[view] : "?", g_state->ui.detail_open() ? 1 : 0,
                g_state->model.demo() ? 1 : 0, static_cast<unsigned>(g_state->model.scenario()), receiver < 5 ? kReceiver[receiver] : "?", s.caps.live_reception ? 1 : 0,
                tune < 4 ? kTune[tune] : "?", static_cast<unsigned>(s.requested_hz), static_cast<unsigned>(s.tuned_hz),
                static_cast<unsigned>(s.messages_held), static_cast<unsigned>(s.messages_total),
                static_cast<unsigned>(s.corrected_total), static_cast<unsigned>(s.queue_drops),
                static_cast<unsigned>(s.aircraft_total));
}

acars_ui::Model* model() { return g_state ? &g_state->model : nullptr; }

}  // namespace orcsdr::acars_app
