#include "rtl_tcp_header_tag.hpp"

#include <M5Unified.h>

#include <cstdio>
#include <cstring>

#include "dashboard_audio_control.hpp"
#include "rtl_tcp_protocol.hpp"

namespace orcsdr::rtl_tcp_header_tag {
namespace {
constexpr uint16_t kBg = TFT_BLACK, kCyan = 0x07ff;
constexpr uint32_t kReassertMs = 2000;

bool dirty = true, drawn = false;
screens::Id owner = screens::Id::none;
Place shown_place{};
State shown{};
uint32_t last_draw_ms = 0;
}  // namespace

void describe(const State& state, char* label, size_t label_size, Severity* severity) {
  using S = rtl_tcp::State;
  Severity level = Severity::ok;
  switch (state.state) {
    case S::streaming:
      if (state.link_pct > 0 && state.link_pct < rtl_tcp::RateAdapter::kBehindPct) {
        std::snprintf(label, label_size, "BEHIND %u%%", static_cast<unsigned>(state.link_pct));
        level = Severity::warn;
      } else {
        std::snprintf(label, label_size, "LIVE");
      }
      break;
    case S::connecting:
    case S::reconnecting:
      std::snprintf(label, label_size, "CONNECTING");
      level = Severity::warn;
      break;
    case S::failed:
      std::snprintf(label, label_size, "FAILED");
      level = Severity::warn;
      break;
    default:
      std::snprintf(label, label_size, "IDLE");
      break;
  }
  if (severity) *severity = level;
}

Place place_for(screens::Id screen) {
  using I = screens::Id;
  switch (screen) {
    // These leave the header status bar empty by design.
    case I::adsb: case I::acars:
      return {Style::full, audio_header::kStatusBarX, audio_header::kStatusBarY,
              audio_header::kStatusBarW, audio_header::kStatusBarH};
    // The title block sits between the dividers at x365 and x865 with its text at y66: the band above it is free.
    case I::fm: case I::am: case I::p25:
      return {Style::bar, 372, 26, 486, 22};
    // The title is at the left; the right of the title area (x640..866) is free.
    case I::cb: case I::airband:
      return {Style::bar, 640, 24, 226, 24};
    // Title and status text fill the header; only the thin top band is free.
    case I::ft8: case I::sstv: case I::lora: case I::pocsag: case I::dead_air: case I::shortwave:
      return {Style::strip, 420, 2, 438, 12};
    default:
      return {};
  }
}

void invalidate() { dirty = true; }

static void erase(const Place& p) { M5.Display.fillRect(p.x, p.y, p.w, p.h, kBg); }

void update(const State& state, screens::Id screen, uint32_t now_ms, bool blocked) {
  const Place place = blocked ? Place{} : place_for(screen);
  const bool want = state.remote && place.style != Style::none;
  if (!want) {
    if (drawn && owner == screen) erase(shown_place);
    drawn = false;
    owner = screen;
    dirty = true;
    return;
  }
  const bool same = drawn && owner == screen && shown.state == state.state && shown.sps == state.sps &&
                    shown.link_pct / 5 == state.link_pct / 5 && std::strcmp(shown.host, state.host) == 0;
  if (!dirty && same && now_ms - last_draw_ms < kReassertMs) return;
  dirty = false;
  shown = state;
  shown_place = place;
  last_draw_ms = now_ms;

  char label[16]{};
  Severity severity = Severity::ok;
  describe(state, label, sizeof(label), &severity);
  const uint16_t color = severity == Severity::ok ? kCyan : TFT_ORANGE;
  char rate[20]{};
  if (state.sps) std::snprintf(rate, sizeof(rate), "%.3f MSPS", state.sps / 1.0e6);

  M5.Display.setFont(nullptr);
  if (place.style == Style::full) {
    M5.Display.fillRect(place.x, place.y, place.w, place.h, kBg);
    M5.Display.drawRoundRect(place.x, place.y, place.w, place.h, 9, color);
    M5.Display.setTextDatum(middle_left);
    M5.Display.setTextColor(TFT_WHITE, kBg);
    M5.Display.setTextSize(3);
    M5.Display.drawString("RTL-TCP", place.x + 16, place.y + 17);
    M5.Display.setTextDatum(middle_right);
    M5.Display.setTextColor(color, kBg);
    M5.Display.setTextSize(2);
    M5.Display.drawString(label, place.x + place.w - 14, place.y + 17);
    char line[64]{};
    std::snprintf(line, sizeof(line), state.sps ? "%s   %s" : "%s", state.sps ? rate : state.host,
                  state.host);
    M5.Display.setTextDatum(middle_left);
    M5.Display.setTextSize(1);
    M5.Display.drawString(line, place.x + 16, place.y + 40);
  } else {
    // One line: "RTL-TCP  LIVE  1.440 MSPS". The bar has an outline; the strip is text only.
    const int size = place.style == Style::bar && place.h >= 20 ? 2 : 1;
    M5.Display.fillRect(place.x, place.y, place.w, place.h, kBg);
    if (place.style == Style::bar) M5.Display.drawRoundRect(place.x, place.y, place.w, place.h, 6, color);
    const int mid = place.y + place.h / 2;
    M5.Display.setTextDatum(middle_left);
    M5.Display.setTextSize(size);
    M5.Display.setTextColor(TFT_WHITE, kBg);
    M5.Display.drawString("RTL-TCP", place.x + 8, mid);
    M5.Display.setTextColor(color, kBg);
    M5.Display.drawString(label, place.x + 8 + 8 * (size * 6), mid);
    if (rate[0] && place.w >= 300) {
      M5.Display.setTextDatum(middle_right);
      M5.Display.drawString(rate, place.x + place.w - 8, mid);
    }
  }
  drawn = true;
  owner = screen;
}

}  // namespace orcsdr::rtl_tcp_header_tag
