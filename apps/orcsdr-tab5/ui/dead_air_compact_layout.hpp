#pragma once
// Geometry of the compact (responsive) DEAD AIR dashboard for views smaller than 1280 x 720 (LilyGO T-Display P4 portrait and
// landscape). Pure arithmetic, host-tested. Pages: OVERVIEW (status, level, history, scan / record), SPECTRUM (plot, waterfall, tune),
// EVENTS (paged journal), SETUP (receiver and sweep controls).
#include <algorithm>
#include "dashboard_header_layout.hpp"

namespace orcsdr::dead_air::compact {
using Rect = audio_header::HeaderRect;

constexpr int kTabCount = 4;
constexpr int kEventRows = 6;
constexpr int kSetupButtons = 6;

struct Layout {
  Rect viewport, content, tabs, tab[kTabCount];
  // OVERVIEW
  Rect status, level, history, scan_button, record_button;
  // SPECTRUM
  Rect plot, waterfall, tune_down, tune_up, frequency;
  // EVENTS
  Rect row[kEventRows], prev, label, next;
  // SETUP
  Rect setup_button[kSetupButtons];
  bool portrait = false, valid = false;
};

inline bool inside(Rect outer, Rect r) {
  return r.w > 0 && r.h > 0 && r.x >= outer.x && r.y >= outer.y && r.x + r.w <= outer.x + outer.w && r.y + r.h <= outer.y + outer.h;
}

inline Layout make_layout(Rect v) {
  Layout l{};
  l.viewport = v;
  l.portrait = v.h > v.w;
  if (v.w < 360 || v.h < 300 || v.w > 1280) return l;
  const auto header = audio_header::make_header_layout(v);
  if (!header.valid || header.legacy) return l;
  constexpr int gap = 8, tab_h = 48, btn_h = 48;
  l.tabs = {v.x, v.y + v.h - tab_h, v.w, tab_h};
  const int tab_w = (v.w - (kTabCount - 1) * 4) / kTabCount;
  for (int i = 0; i < kTabCount; ++i)
    l.tab[i] = {v.x + i * (tab_w + 4), l.tabs.y, i + 1 == kTabCount ? v.x + v.w - (v.x + i * (tab_w + 4)) : tab_w, tab_h};
  const int top = header.content_top;
  l.content = {v.x, top, v.w, l.tabs.y - 4 - top};
  const int bottom = l.content.y + l.content.h;
  const int half = (v.w - gap) / 2;

  // ---- OVERVIEW ----
  if (!l.portrait) {
    const int lw = v.w * 45 / 100;
    l.status = {v.x, l.content.y, lw, 90};
    l.level = {v.x, l.status.y + l.status.h + gap, lw, l.content.h - l.status.h - gap - btn_h - gap};
    l.history = {v.x + lw + gap, l.content.y, v.w - lw - gap, l.content.h - btn_h - gap};
    l.scan_button = {v.x, bottom - btn_h, (lw - gap) / 2, btn_h};
    l.record_button = {v.x + (lw - gap) / 2 + gap, bottom - btn_h, lw - (lw - gap) / 2 - gap, btn_h};
  } else {
    l.status = {v.x, l.content.y, v.w, 96};
    l.level = {v.x, l.status.y + l.status.h + gap, v.w, 120};
    l.scan_button = {v.x, bottom - btn_h, half, btn_h};
    l.record_button = {v.x + half + gap, bottom - btn_h, v.w - half - gap, btn_h};
    const int hy = l.level.y + l.level.h + gap;
    l.history = {v.x, hy, v.w, bottom - btn_h - gap - hy};
  }

  // ---- SPECTRUM ----
  const int ctl_y = bottom - btn_h;
  const int fw = std::max(120, v.w - 2 * (btn_h + gap) - 2 * gap);
  l.tune_down = {v.x, ctl_y, btn_h + 12, btn_h};
  l.frequency = {v.x + btn_h + 12 + gap, ctl_y, v.w - 2 * (btn_h + 12 + gap), btn_h};
  l.tune_up = {v.x + v.w - (btn_h + 12), ctl_y, btn_h + 12, btn_h};
  (void)fw;
  const int plot_h = (ctl_y - gap - l.content.y) * 40 / 100;
  l.plot = {v.x, l.content.y, v.w, plot_h};
  l.waterfall = {v.x, l.plot.y + l.plot.h + gap, v.w, ctl_y - gap - (l.plot.y + l.plot.h + gap)};

  // ---- EVENTS ----
  const int rows = l.portrait ? kEventRows : 3;
  const int paging_y = bottom - btn_h;
  const int rh = (paging_y - gap - l.content.y - (rows - 1) * 4) / rows;
  for (int i = 0; i < kEventRows; ++i)
    l.row[i] = i < rows ? Rect{v.x, l.content.y + i * (rh + 4), v.w, rh} : Rect{};
  const int bw = (v.w - 2 * gap) / 3;
  l.prev = {v.x, paging_y, bw, btn_h};
  l.label = {v.x + bw + gap, paging_y, bw, btn_h};
  l.next = {v.x + 2 * (bw + gap), paging_y, v.w - 2 * (bw + gap), btn_h};

  // ---- SETUP: two columns in landscape, one in portrait ----
  const int cols = l.portrait ? 1 : 2;
  const int srows = (kSetupButtons + cols - 1) / cols;
  const int sbw = (v.w - (cols - 1) * gap) / cols;
  const int sbh = std::min(56, (l.content.h - (srows - 1) * gap) / srows);
  for (int i = 0; i < kSetupButtons; ++i)
    l.setup_button[i] = {v.x + (i % cols) * (sbw + gap), l.content.y + (i / cols) * (sbh + gap), sbw, sbh};

  l.valid = inside(v, l.content) && inside(v, l.tabs) && l.history.w >= 120 && l.history.h >= 60 && l.level.h >= 60 && inside(l.content, l.waterfall) &&
            l.plot.h >= 60 && l.waterfall.h >= 60 && rh >= 40 && inside(l.content, l.setup_button[kSetupButtons - 1]);
  return l;
}
}  // namespace orcsdr::dead_air::compact
