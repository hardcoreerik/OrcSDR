#pragma once
// Geometry of the compact (responsive) MAPS screen for views smaller than 1280 x 720 (LilyGO T-Display P4 portrait and landscape).
// Pure arithmetic, host-tested. Page: PACKS (the map packs found on the card).
#include <algorithm>
#include "dashboard_header_layout.hpp"

namespace orcsdr::maps_ui::compact {
using Rect = audio_header::HeaderRect;

constexpr int kRows = 6;

struct Layout {
  Rect viewport, content, summary, row[kRows], prev, label, next, rescan;
  Rect pick_map;   // LOCATION page: the map; its buttons reuse prev (zoom -), label (zoom +), next (back), rescan (use)
  int rows = 0;   // 6 in portrait, 3 in landscape (the rows below `rows` are empty)
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
  constexpr int gap = 6, btn_h = 48, summary_h = 36;
  l.content = {v.x, header.content_top, v.w, v.y + v.h - header.content_top};
  const int bottom = l.content.y + l.content.h;
  l.summary = {v.x, l.content.y, v.w, summary_h};
  const int btn_y = bottom - btn_h;
  const int bw = (v.w - 3 * gap) / 4;
  l.prev = {v.x, btn_y, bw, btn_h};
  l.label = {v.x + bw + gap, btn_y, bw, btn_h};
  l.next = {v.x + 2 * (bw + gap), btn_y, bw, btn_h};
  l.rescan = {v.x + 3 * (bw + gap), btn_y, v.w - 3 * (bw + gap), btn_h};
  const int top = l.summary.y + l.summary.h + gap;
  // The map library's picker takes at most 1280 x 720 pixels.
  l.pick_map = {v.x, l.summary.y + l.summary.h + gap, v.w, std::min(720, btn_y - gap - (l.summary.y + l.summary.h + gap))};
  l.rows = l.portrait ? kRows : 3;
  const int rows_h = btn_y - gap - top;
  const int rh = (rows_h - (l.rows - 1) * gap) / l.rows;
  for (int i = 0; i < l.rows; ++i) l.row[i] = {v.x, top + i * (rh + gap), v.w, rh};
  l.valid = rh >= 44 && inside(l.content, l.summary) && inside(l.content, l.row[l.rows - 1]) && inside(l.content, l.rescan) &&
            inside(l.content, l.pick_map) && l.pick_map.w >= 300 && l.pick_map.h >= 150;
  return l;
}
}  // namespace orcsdr::maps_ui::compact
