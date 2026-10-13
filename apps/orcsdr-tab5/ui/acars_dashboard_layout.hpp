#pragma once
// ACARS dashboard geometry on the Tab5's 1280 x 720 landscape display. Pure constants and functions
// (no M5GFX) so the same rectangles drive drawing, touch hit-testing and the host tests that prove no
// ACARS content lands inside the shared status bar.
//
//   y   0..104  shared OrcSDR header (brand left, status-bar reserve x420..866, battery + buttons right)
//   y 110..150  context strip (dashboard state; below the header, so it can never touch the reserve)
//   y 158..638  content
//   y 646..720  tab bar
#include <cstddef>
#include <cstdint>

namespace orcsdr::acars_ui {

enum class View : uint8_t { live, aircraft, channels, activity, archive, setup, count };

namespace layout {

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
  constexpr bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
  constexpr bool intersects(const Rect& o) const { return x < o.x + o.w && o.x < x + w && y < o.y + o.h && o.y < y + h; }
  constexpr bool inside(const Rect& o) const { return x >= o.x && y >= o.y && x + w <= o.x + o.w && y + h <= o.y + o.h; }
};

constexpr int kScreenW = 1280;
constexpr int kScreenH = 720;
constexpr int kHeaderH = 104;
// Mirrors audio_header::kStatusBar* (acars_dashboard.cpp static_asserts the equality against the real values).
constexpr Rect kStatusBar{420, 12, 446, 54};
constexpr Rect kContext{20, 110, 1240, 40};
constexpr int kContentY = 158;
constexpr int kContentBottom = 638;
constexpr int kTabsY = 646;
constexpr int kTabCount = 6;
constexpr int kRowH = 36;

constexpr Rect tab(int i) { return {i * 213, kTabsY, i == kTabCount - 1 ? 1280 - 5 * 213 : 213, 74}; }

// ---- LIVE
constexpr Rect kLiveList{20, 158, 800, 480};
constexpr Rect live_row(int i) { return {28, 196 + i * kRowH, 784, kRowH}; }
constexpr Rect kLiveStatus{836, 158, 424, 232};
constexpr Rect kLiveNav{836, 402, 424, 92};
constexpr Rect kLiveNavUp{848, 446, 196, 40};
constexpr Rect kLiveNavDown{1052, 446, 196, 40};
constexpr Rect kLiveNotice{836, 506, 424, 132};

// ---- Message detail (replaces the LIVE content while open)
constexpr Rect kDetailBar{20, 158, 1240, 48};
constexpr Rect kDetailBack{28, 162, 130, 40};
constexpr Rect kDetailNewer{988, 162, 124, 40};
constexpr Rect kDetailOlder{1124, 162, 128, 40};
constexpr Rect kDetailFields{20, 214, 400, 424};
constexpr Rect kDetailRaw{436, 214, 824, 268};
constexpr Rect kDetailRawUp{1100, 220, 70, 34};
constexpr Rect kDetailRawDown{1178, 220, 70, 34};
constexpr Rect kDetailRawText{448, 262, 800, 212};
constexpr int kDetailLines = 8;
constexpr int kDetailLineH = 26;
constexpr int kDetailCols = 57;                       // FreeMono12pt: 14 px advance
constexpr Rect kDetailInterp{436, 490, 824, 148};

// ---- AIRCRAFT
constexpr Rect kAcList{20, 158, 660, 480};
constexpr Rect ac_row(int i) { return {28, 200 + i * 54, 644, 54}; }
constexpr Rect kAcControls{696, 158, 564, 60};
constexpr Rect kAcSort{704, 164, 200, 48};
constexpr Rect kAcUp{916, 164, 70, 48};
constexpr Rect kAcDown{994, 164, 70, 48};
constexpr Rect kAcCount{1072, 164, 180, 48};
constexpr Rect kAcDetail{696, 226, 564, 412};
constexpr Rect ac_msg_row(int j) { return {704, 276 + j * 56, 548, 56}; }
constexpr Rect kAcNote{704, 612, 548, 24};

// ---- CHANNELS
constexpr Rect kChList{20, 158, 760, 480};
constexpr Rect kChRegion{28, 166, 330, 48};
constexpr Rect ch_row(int i) { return {28, 226 + i * 56, 744, 56}; }
constexpr Rect kChCustomLabel{28, 572, 100, 52};
constexpr Rect kChMinus{136, 572, 90, 52};
constexpr Rect kChPlus{232, 572, 90, 52};
constexpr Rect kChClear{328, 572, 110, 52};
constexpr Rect kChCustomValue{446, 572, 326, 52};
constexpr Rect kChStatus{796, 158, 464, 300};
constexpr Rect kChNotes{796, 470, 464, 168};

// ---- ACTIVITY
constexpr Rect act_tile(int i) { return {20 + (i % 4) * 312, 158 + (i / 4) * 128, 300, 116}; }
constexpr int kActTiles = 8;
constexpr Rect kActYield{20, 418, 616, 220};
constexpr Rect kActSignal{652, 418, 608, 220};

// ---- ARCHIVE
constexpr Rect kArFilters{20, 158, 400, 480};
constexpr Rect ar_filter(int i) { return {28, 200 + i * 68, 384, 60}; }
constexpr int kArFilterCount = 5;                     // TAIL, LABEL, FLIGHT, FREQ, SEARCH
constexpr Rect kArClear{28, 548, 384, 52};
constexpr Rect kArList{436, 158, 824, 480};
constexpr Rect kArUp{1100, 162, 70, 32};
constexpr Rect kArDown{1180, 162, 70, 32};
constexpr Rect ar_row(int i) { return {444, 200 + i * 54, 808, 54}; }
constexpr int kArRows = 8;

// ---- SETUP (two columns x five rows)
constexpr Rect setup_item(int column, int row) { return {20 + column * 624, 166 + row * 92, 616, 84}; }
constexpr int kSetupItems = 10;

// Every rectangle the dashboard draws in `view` (and in its detail page when `detail`), for tests.
// Returns the number written (<= capacity).
size_t collect_rects(View view, bool detail, Rect* out, size_t capacity);

}  // namespace layout
}  // namespace orcsdr::acars_ui
