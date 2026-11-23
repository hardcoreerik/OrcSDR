#pragma once
// Shared header tag that says a dashboard is fed by a network receiver (rtl_tcp), on every RTL-based
// dashboard. It is drawn only while the source is remote: with a USB dongle the header looks exactly
// as before. Serviced from the main loop like map_download_notice, so a dashboard does not need its
// own redraw logic and nothing repaints the full screen.
//
// Where it sits depends on the dashboard, because most dashboards draw their own title in the
// header's status-bar area (audio_header::kStatusBar*): see place_for().
#include <cstddef>
#include <cstdint>

#include "rtl_tcp_session.hpp"
#include "screen_controller.hpp"

namespace orcsdr::rtl_tcp_header_tag {

struct State {
  bool remote = false;      // the receiver source is an rtl_tcp server (streaming or not)
  rtl_tcp::State state = rtl_tcp::State::idle;
  uint32_t link_pct = 0;    // received vs needed byte rate; 0 = not measured, below 85 = behind
  uint32_t sps = 0;         // sample rate of the live stream, 0 when not streaming
  char host[40]{};          // "host:port"
};

enum class Severity : uint8_t { ok, warn };
// Pure rule (host-tested): the state word and how serious it is.
void describe(const State& state, char* label, size_t label_size, Severity* severity);

// Where a dashboard shows the tag. `full` is the 446x54 tag in the empty status bar; `bar` is one
// line with an outline; `strip` is one line of text only, in the thin band above the header content.
enum class Style : uint8_t { none, full, bar, strip };
struct Place {
  Style style = Style::none;
  int x = 0, y = 0, w = 0, h = 0;
};
Place place_for(screens::Id screen);

// The header was repainted: draw the tag again on the next update().
void invalidate();
// Call about every 250 ms; paints only when something changed, and re-asserts itself every 2 s in
// case a dashboard repainted that area without going through the shared header.
void update(const State& state, screens::Id screen, uint32_t now_ms, bool blocked);

}  // namespace orcsdr::rtl_tcp_header_tag
