// Host tests for the shared volume tray (dashboard_audio_control.cpp): geometry, artwork proportions, double-tap guard,
// what a touch inside, outside and on a header button means, and that drawing stays inside the tray's own rectangle.
// The main.cpp touch gate (dashboards see no touch while the tray owns the finger) is firmware-only and is checked on the Tab5.
#include "dashboard_audio_control.hpp"

#include <M5Unified.h>
#include <cassert>
#include <cstdio>

#include "focus_nav.hpp"
#include "map_download_notice.hpp"
#include "orc_badge.hpp"
#include "rtl_tcp_header_tag.hpp"

// The badge image is linked in from a binary blob on the device; the host build has none, so the module takes its fallback.
extern "C" {
extern const unsigned char _binary_orc_badge_104_rgb565_start[1];
extern const unsigned char _binary_orc_badge_104_rgb565_end[1];
const unsigned char _binary_orc_badge_104_rgb565_start[1] = {0};
const unsigned char _binary_orc_badge_104_rgb565_end[1] = {0};
}
namespace orcsdr::focus_nav { void note(int, int, int, int) {} }
namespace orcsdr::map_download_notice { void invalidate() {} }
namespace orcsdr::rtl_tcp_header_tag { void invalidate() {} }

using namespace orcsdr::audio_header;

int main() {
  assert(self_check());
  // Proportions of the artwork (2000 x 667) and placement: a 3:1 popup below the status bar, centred under it, on screen.
  static_assert(kTrayW == 3 * kTrayH, "the tray keeps the artwork's 3:1 shape");
  static_assert(kTrayY >= kStatusBarY + kStatusBarH, "the tray opens below the status bar");
  static_assert(kTrayX + kTrayW / 2 == kStatusBarX + kStatusBarW / 2, "centred under the status bar");
  static_assert(kTrayX >= 0, "on screen");
  static_assert(kTrayX + kTrayW <= 1280 && kTrayY + kTrayH <= 720, "on screen");

  Control c{};
  // Open on the speaker, then a double tap: the second tap is swallowed, not a mute.
  assert(handle_touch(c, 1165, 30, 1000, 128) == Action::opened && c.expanded);
  assert(handle_touch(c, 1165, 30, 1200, 128) == Action::consumed);
  assert(handle_touch(c, 1165, 30, 1000 + kRepeatTapGuardMs, 128) == Action::mute_toggle);
  // Blank space inside the tray never falls through to the dashboard below, and keeps the tray open.
  assert(handle_touch(c, kTrayX + kTrayW - 6, kTrayY + 6, 2000, 128) == Action::consumed && c.expanded);
  // Slider: ends clamp, a drag that wanders above or below the bar still tracks.
  assert(handle_touch(c, kTrayX + 176 - 30, kTrayY + 150, 2100, 128) == Action::volume_set && c.volume == 0);
  assert(handle_touch(c, kTrayX + 176 + 496 + 30, kTrayY + 150 + 50, 2200, 128) == Action::volume_set && c.volume == 255);
  assert(handle_touch(c, kTrayX + 176 + 248, kTrayY + 150 - 50, 2300, 128) == Action::volume_set && c.volume >= 127 && c.volume <= 129);
  // The speaker square inside the tray is a mute button too.
  assert(handle_touch(c, kTrayX + 90, kTrayY + 140, 2400, 128) == Action::mute_toggle);
  // Header buttons are not the tray's: the caller closes it and lets the tap through. Anywhere else outside closes it.
  assert(handle_touch(c, 1239, 40, 2500, 128) == Action::none && c.expanded);       // Settings, spaced out at the right edge
  assert(handle_touch(c, 1202, 40, 2550, 128) == Action::closed && !c.expanded);    // the gap between Mute and Settings is empty header
  c.expanded = true;
  assert(handle_touch(c, 100, 500, 2600, 128) == Action::closed && !c.expanded);
  assert(handle_touch(c, 100, 500, 2700, 128) == Action::none);   // closed tray ignores everything but the speaker

  // Drawing: everything the open tray paints lies inside its own rectangle or the battery region (no stray pixels).
  host_display::reset();
  Control open{};
  open.expanded = true;
  draw(open, 173, true, 80);
  int tray_prims = 0;
  for (const Prim& p : host_display::prims) {
    int x, y, w, h;
    if (!host_display::effective(p, &x, &y, &w, &h)) continue;
    const bool in_tray = x >= kTrayX && y >= kTrayY && x + w <= kTrayX + kTrayW && y + h <= kTrayY + kTrayH;
    const bool in_battery = x >= 866 && y >= 12 && x + w <= 866 + 174 && y + h <= 12 + 54;
    assert(!visualizer_hit(1165, 40));   // no VIS button
    if (in_tray) ++tray_prims;
    if (!in_tray && !in_battery) std::printf("stray primitive %d,%d %dx%d\n", x, y, w, h);
    assert(in_tray || in_battery);
  }
  assert(tray_prims >= 10);
  bool has_percent = false, has_label = false;
  for (const Prim& p : host_display::prims) {
    if (p.str == "68%") has_percent = true;    // 173 / 255 rounds to 68 %
    if (p.str == "VOLUME") has_label = true;
  }
  assert(has_percent && has_label);

  // Successive redraws: the visible percentage and label follow every volume / mute change, an unchanged tray is not re-pushed
  // inside the safety interval, a failing sprite allocation is not retried every loop, and nothing ever clears the full screen.
  auto shows = [](const char* text) {
    for (const Prim& p : host_display::prims) if (p.str == text) return true;
    return false;
  };
  reset(open);                       // a fresh tray (closes and releases any sprite)
  open.expanded = true;
  host_display::reset();
  uint32_t t = 10000;
  refresh_tray_overlay(open, 0, true, t);
  assert(shows("0%") && shows("VOLUME"));
  host_display::prims.clear();
  refresh_tray_overlay(open, 255, true, t += 10);
  assert(shows("100%") && !shows("0%"));
  host_display::prims.clear();
  refresh_tray_overlay(open, 255, false, t += 10);                 // mute: label changes, value kept
  assert(shows("MUTED") && shows("100%"));
  const int pushes_before = host_display::pushes;
  refresh_tray_overlay(open, 255, false, t += 10);                 // unchanged, inside kOverlayRepushMs: no push
  assert(host_display::pushes == pushes_before);
  refresh_tray_overlay(open, 255, false, t + kOverlayRepushMs + 1);   // safety re-push
  assert(host_display::pushes == pushes_before + 1);
  reset(open);                                                     // close, then re-enter: starts clean and shows the new value
  open.expanded = true;
  host_display::prims.clear();
  refresh_tray_overlay(open, 128, true, t + 5000);
  assert(shows("50%"));
  // Sprite allocation failure: draws straight onto the display once, not on every loop.
  reset(open);
  open.expanded = true;
  host_display::fail_sprite_alloc = true;
  host_display::reset();
  refresh_tray_overlay(open, 100, true, 20000);
  const int direct_first = host_display::direct_ops;
  assert(direct_first > 0);
  refresh_tray_overlay(open, 100, true, 20010);
  refresh_tray_overlay(open, 100, true, 20020);
  assert(host_display::direct_ops == direct_first);                // unchanged: not repainted
  host_display::fail_sprite_alloc = false;
  assert(host_display::full_clears == 0);                          // no full-screen clear anywhere in the tray's redraws
  std::puts("audio header tray host tests passed");
  return 0;
}
