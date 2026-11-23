#pragma once
// OrcSDR Tab5 Digital Voice preproduction geometry; no display or decoder dependency.
#include <cstdint>

namespace orcsdr::digital_voice_ui::layout {
struct Rect {
  int x, y, w, h;
  constexpr bool contains(int px, int py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
  constexpr bool intersects(Rect r) const {
    return x < r.x + r.w && r.x < x + w && y < r.y + r.h && r.y < y + h;
  }
};
constexpr int kWidth = 1280, kHeight = 720;
constexpr Rect kSharedStatus{420, 12, 446, 54};
constexpr Rect kContext{20, 114, 1240, 34};
constexpr Rect kContent{20, 158, 1240, 478};
constexpr Rect kTabs{20, 650, 1240, 52};
constexpr int kTabCount = 6;
constexpr Rect tab(int i) { return {20 + i * 208, 650, 198, 52}; }
// Discovery first: center hero + right facts panel.
constexpr Rect kBasicHero{20, 158, 802, 478};
constexpr Rect kBasicFacts{834, 158, 426, 478};
// Analysis first: balanced raw metadata and diagnostics panels.
constexpr Rect kAdvancedMain{20, 158, 610, 478};
constexpr Rect kAdvancedFacts{642, 158, 618, 478};
constexpr bool inside(Rect r, Rect parent) {
  return r.x >= parent.x && r.y >= parent.y && r.x + r.w <= parent.x + parent.w && r.y + r.h <= parent.y + parent.h;
}
static_assert(kWidth == 1280 && kHeight == 720);
static_assert(!kContext.intersects(kSharedStatus));
static_assert(inside(kBasicHero, kContent) && inside(kBasicFacts, kContent));
static_assert(inside(kAdvancedMain, kContent) && inside(kAdvancedFacts, kContent));
static_assert(!kBasicHero.intersects(kBasicFacts) && !kAdvancedMain.intersects(kAdvancedFacts));
static_assert(inside(tab(0), kTabs) && inside(tab(5), kTabs));
static_assert(!tab(0).intersects(tab(1)) && !tab(4).intersects(tab(5)));
}
