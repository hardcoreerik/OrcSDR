#include "ais_app.hpp"

#include <cstdio>
#include <cstdlib>
#include <new>

#include "ais_dashboard.hpp"
#include "ais_radio.hpp"
#include "screen_controller.hpp"

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace orcsdr::ais_app {
namespace {

constexpr uint32_t kRefreshMs = 250;   // link state and device fields; the decoder publishes every 500 ms

// The UI-side snapshot (~17 KB) is allocated on open and freed on close; no static of that size exists.
struct State {
  ais_ui::Snapshot snapshot;
  uint32_t feed_sequence = 0;
  uint32_t last_refresh_ms = 0;
  uint32_t updates = 0;
  ais_radio::Link shown_link = ais_radio::Link::stopped;
};

State* g_state = nullptr;
Hooks g_hooks;
uint32_t g_open_failed = 0;
// The radio starts after the first screen has drawn and the map frame has settled (or kStartWaitMaxMs has passed). On a cold
// boot the first basemap render slows the DSP task enough to drop USB blocks (measured on the Tab5), so the two must not overlap.
constexpr uint32_t kStartWaitMaxMs = 8000;
bool g_start_pending = false;
uint32_t g_start_wait_since_ms = 0, g_last_repaint_ms = 0;
uint32_t g_t_open_ms = 0, g_t_enter_start_ms = 0, g_t_enter_end_ms = 0;   // phases of the last open, in ms since boot

uint32_t now_ms() { return g_hooks.now_ms ? g_hooks.now_ms() : 0; }

bool ensure_state() {
  if (g_state) return true;
#if defined(ESP_PLATFORM)
  void* memory = heap_caps_malloc(sizeof(State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);   // never the internal pool
#else
  void* memory = std::malloc(sizeof(State));
#endif
  if (!memory) return false;
  g_state = new (memory) State();
  return true;
}

void free_state() {
  if (!g_state) return;
  g_state->~State();
#if defined(ESP_PLATFORM)
  heap_caps_free(g_state);
#else
  std::free(g_state);
#endif
  g_state = nullptr;
}

// Fields the app owns. Returns true when something the dashboard shows changed.
bool refresh_device_fields() {
  ais_ui::Snapshot& s = g_state->snapshot;
  const ais_radio::Link link = ais_radio::status().link;
  ais_ui::ReceiveState receiver = ais_ui::ReceiveState::ready;
  bool settled = false;
  if (g_start_pending) {                          // not started yet: say so, never "disconnected"
    s.receiver = receiver;
    s.receiver_settled = false;
    s.center_hz = ais_radio::kCenterHz;
    s.sample_rate_sps = 0;
    g_state->shown_link = link;
    return false;
  }
  switch (link) {
    case ais_radio::Link::no_receiver:
    case ais_radio::Link::stopped: receiver = ais_ui::ReceiveState::disconnected; break;
    case ais_radio::Link::no_memory:
    case ais_radio::Link::wrong_stream: receiver = ais_ui::ReceiveState::unavailable; break;
    case ais_radio::Link::receiving: settled = true; break;
    case ais_radio::Link::tuning:
    case ais_radio::Link::stalled: break;
  }
  int32_t lat = 0, lon = 0;
  const bool located = g_hooks.receiver_location && g_hooks.receiver_location(&lat, &lon);
  const int battery = g_hooks.battery_percent ? g_hooks.battery_percent() : -1;
  const bool sd = g_hooks.sd_available && g_hooks.sd_available();
  const bool remote = g_hooks.remote_source && g_hooks.remote_source();
  const bool test = ais_radio::status().test_replay;
  const bool changed = s.test_replay != test || s.receiver != receiver || s.receiver_settled != settled || s.coordinates_valid != located ||
                       s.receiver_lat_e7 != lat || s.receiver_lon_e7 != lon || s.battery_percent != battery ||
                       s.sd_available != sd || s.rtl_tcp != remote;
  s.receiver = receiver;
  s.receiver_settled = settled;
  s.coordinates_valid = located;
  s.receiver_lat_e7 = located ? lat : 0;
  s.receiver_lon_e7 = located ? lon : 0;
  s.battery_percent = battery;
  s.sd_available = sd;
  s.rtl_tcp = remote;
  s.test_replay = test;
  // None of these three exists in this build (issue 249); the Setup page says so instead of showing a state.
  s.long_range_available = s.nmea_available = s.journal_available = false;
  s.center_hz = ais_radio::kCenterHz;
  if (!settled) s.sample_rate_sps = 0;            // no rate is claimed while nothing is being decoded
  g_state->shown_link = link;
  return changed;
}

}  // namespace

void install(const Hooks& hooks) { g_hooks = hooks; }

void open() {
  if (!ensure_state()) { ++g_open_failed; return; }
  g_t_open_ms = now_ms();
  // A fresh visit never shows the previous visit's vessels: the receiver restarts with an empty store.
  g_state->snapshot.~Snapshot();                  // rebuilt in place: a Snapshot temporary is 17 KB of UI-task stack
  new (&g_state->snapshot) ais_ui::Snapshot();
  g_state->feed_sequence = 0;
  g_start_pending = true;                         // the radio starts from tick() once the first frame has settled
  g_start_wait_since_ms = now_ms();
  g_last_repaint_ms = g_start_wait_since_ms;
  (void)refresh_device_fields();
  g_state->snapshot.revision = 1;
  screens::begin_transition(screens::Id::ais, now_ms());
  g_t_enter_start_ms = now_ms();
  const bool drawn = ais_ui::enter(g_state->snapshot);
  g_t_enter_end_ms = now_ms();
  if (drawn && g_hooks.header_controls) g_hooks.header_controls();
  screens::finish_transition();
  if (!drawn) {                                   // the dashboard could not allocate: release the radio and leave the screen
    ++g_open_failed;
    close();
    if (g_hooks.go_home) g_hooks.go_home();
  }
}

void close() {
  g_start_pending = false;
  if (!g_state && !ais_ui::active() && !ais_radio::running()) return;
  ais_radio::stop();                              // gives the sample rate back and frees the decoder
  ais_ui::leave();
  free_state();
}

void tick() {
  if (!g_state || !ais_ui::active()) return;
  const uint32_t now = now_ms();
  if (g_start_pending && screens::owns(screens::Id::ais)) {
    // Repaint so the map frame state is current, then start the radio once it is no longer pending (or after the cap).
    if (now - g_last_repaint_ms >= 250) {
      g_last_repaint_ms = now;
      ++g_state->snapshot.revision;
      ais_ui::update(g_state->snapshot);
    }
    if (ais_ui::map_settled() || now - g_start_wait_since_ms >= kStartWaitMaxMs) {
      g_start_pending = false;
      (void)ais_radio::start();                   // without hooks (host tests) the screen stays "disconnected"
    }
  }
  ais_radio::service();
  bool changed = ais_radio::copy_feed(&g_state->snapshot, &g_state->feed_sequence);
  if (now - g_state->last_refresh_ms >= kRefreshMs) {
    g_state->last_refresh_ms = now;
    changed = refresh_device_fields() || changed;
  }
  if (!changed || !screens::owns(screens::Id::ais)) return;
  if (g_hooks.overlay_open && g_hooks.overlay_open()) return;   // repaints once the tray closes (the next change repaints)
  ++g_state->snapshot.revision;
  ++g_state->updates;
  screens::note_visible_update(screens::Id::ais);
  ais_ui::update(g_state->snapshot);
}

void draw(bool static_panel) {
  if (!g_state) return;
  if (static_panel) {                             // full redraw after Settings / Visualizer (returns to the first page)
    if (!ais_ui::enter(g_state->snapshot)) {
      ++g_open_failed;
      close();
      if (g_hooks.go_home) g_hooks.go_home();
      return;
    }
    if (g_hooks.header_controls) g_hooks.header_controls();
    return;
  }
  if (!ais_ui::active() || !screens::may_draw(screens::Id::ais)) return;
  if (g_hooks.overlay_open && g_hooks.overlay_open()) return;
  ++g_state->snapshot.revision;
  ais_ui::update(g_state->snapshot);
}

void touch(int32_t x, int32_t y) {
  if (!g_state || !ais_ui::active()) return;
  // Header buttons are handled by the firmware before this is called. Channel, output and logging toggles are returned as
  // actions; none is applied yet, so the SETUP page keeps showing the real (unchanged) state.
  (void)ais_ui::handle_touch(x, y);
}

bool active() { return ais_ui::active(); }

void describe(char* out, size_t size) {
  if (!out || size == 0) return;
  if (!g_state) {
    std::snprintf(out, size, "RTL_AIS_UI open=0 open_failed=%u", static_cast<unsigned>(g_open_failed));
    return;
  }
  static const char* const kViews[] = {"map", "targets", "messages", "protocol", "signal", "events", "setup"};
  const size_t view = static_cast<size_t>(ais_ui::view());
  const ais_ui::Snapshot& s = g_state->snapshot;
  std::snprintf(out, size,
                "RTL_AIS_UI open=%d view=%s link=%s settled=%d test=%d targets=%u messages=%u events=%u ch1_ok=%u ch2_ok=%u "
                "located=%d remote=%d revision=%u updates=%u snapshot_bytes=%u open_failed=%u open_ms=%u enter_start_ms=%u enter_end_ms=%u",
                ais_ui::active() ? 1 : 0, view < 7 ? kViews[view] : "?", ais_radio::link_name(g_state->shown_link),
                s.receiver_settled ? 1 : 0, s.test_replay ? 1 : 0, static_cast<unsigned>(s.target_count),
                static_cast<unsigned>(s.message_count), static_cast<unsigned>(s.event_count), static_cast<unsigned>(s.crc_ok[0]),
                static_cast<unsigned>(s.crc_ok[1]), s.coordinates_valid ? 1 : 0, s.rtl_tcp ? 1 : 0,
                static_cast<unsigned>(s.revision), static_cast<unsigned>(g_state->updates),
                static_cast<unsigned>(sizeof(State)), static_cast<unsigned>(g_open_failed), static_cast<unsigned>(g_t_open_ms),
                static_cast<unsigned>(g_t_enter_start_ms), static_cast<unsigned>(g_t_enter_end_ms));
}

}  // namespace orcsdr::ais_app
