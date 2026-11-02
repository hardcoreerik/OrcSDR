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
  constexpr bool operator==(const Rect& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
};

// The default remains the Tab5's exact legacy canvas. Ports may configure a
// safe viewport at a structural transition; LIVE geometry then adapts to it.
extern int kScreenW;
extern int kScreenH;
extern int kHeaderH;
// Mirrors audio_header::kStatusBar* (acars_dashboard.cpp static_asserts the equality against the real values).
constexpr Rect kStatusBar{420, 12, 446, 54};
extern Rect kContext;
extern int kContentY;
extern int kContentBottom;
extern int kTabsY;
constexpr int kTabCount = 6;
constexpr int kRowH = 36;
extern Rect kViewport;
extern size_t kLiveVisibleRows;
bool set_viewport(Rect viewport);
void reset_viewport();

Rect tab(int i);

// ---- LIVE
extern Rect kLiveList;
Rect live_row(int i);
extern Rect kLiveStatus;
extern Rect kLiveNav;
extern Rect kLiveNavUp;
extern Rect kLiveNavDown;
extern Rect kLiveNotice;

// ---- Message detail (replaces the LIVE content while open)
extern Rect kDetailBar;
extern Rect kDetailBack;
extern Rect kDetailNewer;
extern Rect kDetailOlder;
extern Rect kDetailFields;
extern Rect kDetailRaw;
extern Rect kDetailRawUp;
extern Rect kDetailRawDown;
extern Rect kDetailRawText;
extern int kDetailLines;
extern int kDetailLineH;
extern int kDetailCols;                       // FreeMono12pt: about 14 px advance
extern Rect kDetailInterp;

// ---- AIRCRAFT
extern Rect kAcList;
Rect ac_row(int i);
extern Rect kAcControls;
extern Rect kAcSort;
extern Rect kAcUp;
extern Rect kAcDown;
extern Rect kAcCount;
extern Rect kAcDetail;
Rect ac_msg_row(int j);
extern Rect kAcMsgUp;
extern Rect kAcMsgDown;
extern Rect kAcNote;
extern size_t kAcVisibleRows;
extern size_t kAcMessageRows;

// ---- CHANNELS
extern Rect kChList;
extern Rect kChRegion;
constexpr int kChRows = 7;
Rect ch_row(int i);
extern Rect kChCustomLabel, kChMinus, kChPlus, kChClear, kChCustomValue;
extern Rect kChStatus, kChNotes;
extern int kChVisibleRows;

// ---- ACTIVITY
Rect act_tile(int i);
constexpr int kActTiles = 8;
extern Rect kActYield, kActSignal;

// ---- ARCHIVE
extern Rect kArFilters;
Rect ar_filter(int i);
constexpr int kArFilterCount = 5;                     // TAIL, LABEL, FLIGHT, FREQ, SEARCH
extern Rect kArClear, kArList, kArUp, kArDown;
Rect ar_row(int i);
constexpr int kArRows = 8;
extern int kArVisibleRows;

// ---- SETUP (two columns x five rows)
Rect setup_item(int column, int row);
constexpr int kSetupItems = 10;

// Every rectangle the dashboard draws in `view` (and in its detail page when `detail`), for tests.
// Returns the number written (<= capacity).
size_t collect_rects(View view, bool detail, Rect* out, size_t capacity);

}  // namespace layout
}  // namespace orcsdr::acars_ui
