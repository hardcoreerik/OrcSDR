#pragma once
// Geometry of the compact (responsive) FT8 dashboard. Pure arithmetic, no drawing, so it is host-tested.
// The legacy FT8 dashboard (ft8_dashboard.cpp) is written for 1280 x 720; this layout serves any smaller view
// (the LilyGO T-Display P4 portrait 424 x 1088 and landscape 1088 x 424, a rotated Tab5 later).
#include "dashboard_header_layout.hpp"

namespace orcsdr::ft8::compact {
using Rect = audio_header::HeaderRect;

constexpr int kTabCount = 3;            // LIVE, DECODES, SETUP
constexpr int kMaxListRows = 24;

struct Layout {
  Rect viewport, content, tabs, tab[kTabCount];
  // LIVE
  Rect band_prev, dial, band_next, clock, gain, status;
  Rect wf_frame, wf_plot, timer, live_list;
  int live_rows = 3;
  // DECODES
  Rect summary[3], list, newer, page, older, clear;
  int list_rows = 4, row_h = 36;
  bool two_line_rows = false;
  // SETUP
  Rect mode_ft8, mode_ft4, sync_clock, open_clock, info[6];
  bool portrait = false, valid = false;
};

inline bool inside(Rect outer, Rect r) {
  return r.w > 0 && r.h > 0 && r.x >= outer.x && r.y >= outer.y &&
         r.x + r.w <= outer.x + outer.w && r.y + r.h <= outer.y + outer.h;
}

inline Layout make_layout(Rect v) {
  Layout l{};
  l.viewport = v;
  l.portrait = v.h > v.w;
  if (v.w < 360 || v.h < 300 || v.w > 1280) return l;
  const auto header = audio_header::make_header_layout(v);
  if (!header.valid || header.legacy) return l;
  constexpr int gap = 8, chip_h = 48, tab_h = 48;
  l.tabs = {v.x, v.y + v.h - tab_h, v.w, tab_h};
  const int tab_w = (v.w - 2 * 4) / kTabCount;
  for (int i = 0; i < kTabCount; ++i)
    l.tab[i] = {v.x + i * (tab_w + 4), l.tabs.y, i + 1 == kTabCount ? v.x + v.w - (v.x + i * (tab_w + 4)) : tab_w, tab_h};
  const int top = header.content_top;
  l.content = {v.x, top, v.w, l.tabs.y - 4 - top};
  const int bottom = l.content.y + l.content.h;

  // ---- LIVE ----
  const int arrow = 56;
  int y = l.content.y;
  if (!l.portrait) {
    l.band_prev = {v.x, y, arrow, chip_h};
    l.dial = {v.x + arrow + gap, y, 224, chip_h};
    l.band_next = {l.dial.x + l.dial.w + gap, y, arrow, chip_h};
    const int rest_x = l.band_next.x + arrow + gap;
    const int rest_w = v.x + v.w - rest_x;
    const int third = (rest_w - 2 * gap) / 3;
    l.clock = {rest_x, y, third, chip_h};
    l.gain = {rest_x + third + gap, y, third, chip_h};
    l.status = {rest_x + 2 * (third + gap), y, v.x + v.w - (rest_x + 2 * (third + gap)), chip_h};
    y += chip_h + gap;
  } else {
    l.band_prev = {v.x, y, arrow, chip_h};
    l.band_next = {v.x + v.w - arrow, y, arrow, chip_h};
    l.dial = {v.x + arrow + gap, y, v.w - 2 * (arrow + gap), chip_h};
    y += chip_h + gap;
    const int third = (v.w - 2 * gap) / 3;
    l.clock = {v.x, y, third, chip_h};
    l.gain = {v.x + third + gap, y, third, chip_h};
    l.status = {v.x + 2 * (third + gap), y, v.x + v.w - (v.x + 2 * (third + gap)), chip_h};
    y += chip_h + gap;
  }
  const int area_h = bottom - y;
  const int list_title = 28, list_row = 26;
  if (!l.portrait) {
    l.live_rows = 3;
    const int list_h = list_title + l.live_rows * list_row + 6;
    const int wf_h = area_h - list_h - gap;
    const int wf_w = v.w * 62 / 100;
    l.wf_frame = {v.x, y, wf_w, wf_h};
    l.timer = {v.x + wf_w + gap, y, v.w - wf_w - gap, wf_h};
    l.live_list = {v.x, y + wf_h + gap, v.w, list_h};
  } else {
    const int timer_h = 84;
    l.live_rows = (area_h - 300 - timer_h - 2 * gap - list_title - 6) / list_row;
    if (l.live_rows > 8) l.live_rows = 8;
    if (l.live_rows < 2) l.live_rows = 2;
    const int list_h = list_title + l.live_rows * list_row + 6;
    const int wf_h = area_h - timer_h - list_h - 2 * gap;
    l.wf_frame = {v.x, y, v.w, wf_h};
    l.timer = {v.x, y + wf_h + gap, v.w, timer_h};
    l.live_list = {v.x, l.timer.y + timer_h + gap, v.w, list_h};
  }
  // The plot sits inside the frame under a 22 px title and above a 20 px frequency axis (both drawn by the dashboard).
  l.wf_plot = {l.wf_frame.x + 6, l.wf_frame.y + 26, l.wf_frame.w - 12, l.wf_frame.h - 26 - 22};

  // ---- DECODES ----
  const int sum_w = (v.w - 2 * gap) / 3;
  for (int i = 0; i < 3; ++i)
    l.summary[i] = {v.x + i * (sum_w + gap), l.content.y, i == 2 ? v.x + v.w - (v.x + 2 * (sum_w + gap)) : sum_w, chip_h};
  const int controls_y = bottom - chip_h;
  const int btn_w = l.portrait ? (v.w - 3 * gap) / 4 : 150;
  l.newer = {v.x, controls_y, btn_w, chip_h};
  l.older = {v.x + btn_w + gap, controls_y, btn_w, chip_h};
  l.clear = {v.x + v.w - btn_w, controls_y, btn_w, chip_h};
  l.page = {l.older.x + btn_w + gap, controls_y, l.clear.x - gap - (l.older.x + btn_w + gap), chip_h};
  l.list = {v.x, l.content.y + chip_h + gap, v.w, controls_y - gap - (l.content.y + chip_h + gap)};
  l.two_line_rows = l.portrait;
  l.row_h = l.two_line_rows ? 56 : 34;
  l.list_rows = (l.list.h - 32) / l.row_h;
  if (l.list_rows > kMaxListRows) l.list_rows = kMaxListRows;
  if (l.list_rows < 1) l.list_rows = 1;

  // ---- SETUP ----
  const int half = (v.w - gap) / 2;
  l.mode_ft8 = {v.x, l.content.y, half, chip_h};
  l.mode_ft4 = {v.x + half + gap, l.content.y, v.w - half - gap, chip_h};
  l.sync_clock = {v.x, bottom - chip_h, half, chip_h};
  l.open_clock = {v.x + half + gap, bottom - chip_h, v.w - half - gap, chip_h};
  const int info_top = l.content.y + chip_h + gap;
  const int info_h = (bottom - chip_h - gap - info_top - 5 * 4) / 6;
  for (int i = 0; i < 6; ++i) l.info[i] = {v.x, info_top + i * (info_h + 4), v.w, info_h};

  l.valid = inside(v, l.content) && inside(v, l.tabs) && l.wf_plot.w > 80 && l.wf_plot.h >= 60 &&
            l.live_list.y + l.live_list.h <= bottom && l.info[0].h >= 28;
  return l;
}
}  // namespace orcsdr::ft8::compact
