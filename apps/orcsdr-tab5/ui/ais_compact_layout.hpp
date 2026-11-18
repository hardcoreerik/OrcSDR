#pragma once
// Geometry of the compact (responsive) AIS dashboard for views smaller than the Tab5's 1280 x 720 (LilyGO T-Display P4 portrait and
// landscape). Pure arithmetic, host-tested. Four pages: TARGETS, MESSAGES, SIGNAL, SETUP (the map page is not on this view yet).
#include "dashboard_header_layout.hpp"

namespace orcsdr::ais_ui::compact {
using Rect = audio_header::HeaderRect;

constexpr int kTabCount = 4;
constexpr int kMaxRows = 32;

struct Layout {
  Rect viewport, content, tabs, tab[kTabCount];
  // TARGETS: a paged list and the selected target's detail.
  Rect context, target_list, target_detail, page_prev, page_label, page_next;
  int target_rows = 4, target_row_h = 52;
  // MESSAGES: a paged list of recent frames.
  Rect message_list, message_prev, message_label, message_next;
  int message_rows = 4, message_row_h = 56;
  // SIGNAL: one panel per AIS channel and a status strip.
  Rect signal_panel[2], signal_status;
  // SETUP: status rows.
  Rect setup_row[8];
  int setup_rows = 8;
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
  constexpr int gap = 8, tab_h = 48, strip = 44;
  l.tabs = {v.x, v.y + v.h - tab_h, v.w, tab_h};
  const int tab_w = (v.w - (kTabCount - 1) * 4) / kTabCount;
  for (int i = 0; i < kTabCount; ++i)
    l.tab[i] = {v.x + i * (tab_w + 4), l.tabs.y, i + 1 == kTabCount ? v.x + v.w - (v.x + i * (tab_w + 4)) : tab_w, tab_h};
  const int top = header.content_top;
  l.content = {v.x, top, v.w, l.tabs.y - 4 - top};
  const int bottom = l.content.y + l.content.h;

  // ---- TARGETS ----
  l.context = {v.x, l.content.y, v.w, 30};
  const int body_y = l.content.y + l.context.h + 4;
  if (!l.portrait) {
    const int list_w = v.w * 56 / 100;
    l.target_row_h = 52;
    const int list_h = bottom - body_y;
    l.target_list = {v.x, body_y, list_w, list_h};
    l.target_detail = {v.x + list_w + gap, body_y, v.w - list_w - gap, list_h};
  } else {
    l.target_row_h = 52;
    const int detail_h = 200;
    l.target_detail = {v.x, bottom - detail_h, v.w, detail_h};
    l.target_list = {v.x, body_y, v.w, l.target_detail.y - gap - body_y};
  }
  const int paging_y = l.target_list.y + l.target_list.h - strip;
  const int btn_w = 92;
  l.page_prev = {l.target_list.x, paging_y, btn_w, strip};
  l.page_next = {l.target_list.x + l.target_list.w - btn_w, paging_y, btn_w, strip};
  l.page_label = {l.page_prev.x + btn_w + gap, paging_y, l.page_next.x - gap - (l.page_prev.x + btn_w + gap), strip};
  l.target_rows = (l.target_list.h - strip - gap - 4) / l.target_row_h;
  if (l.target_rows > kMaxRows) l.target_rows = kMaxRows;
  if (l.target_rows < 1) l.target_rows = 1;

  // ---- MESSAGES ----
  l.message_list = {v.x, l.content.y, v.w, l.content.h};
  const int mpaging_y = bottom - strip;
  l.message_prev = {v.x, mpaging_y, btn_w, strip};
  l.message_next = {v.x + v.w - btn_w, mpaging_y, btn_w, strip};
  l.message_label = {v.x + btn_w + gap, mpaging_y, v.w - 2 * (btn_w + gap), strip};
  l.message_row_h = l.portrait ? 64 : 40;
  l.message_rows = (l.message_list.h - 30 - strip - gap) / l.message_row_h;
  if (l.message_rows > kMaxRows) l.message_rows = kMaxRows;
  if (l.message_rows < 1) l.message_rows = 1;

  // ---- SIGNAL ----
  const int status_h = 60;
  l.signal_status = {v.x, bottom - status_h, v.w, status_h};
  const int panels_h = l.signal_status.y - gap - l.content.y;
  if (!l.portrait) {
    const int half = (v.w - gap) / 2;
    l.signal_panel[0] = {v.x, l.content.y, half, panels_h};
    l.signal_panel[1] = {v.x + half + gap, l.content.y, v.w - half - gap, panels_h};
  } else {
    const int half = (panels_h - gap) / 2;
    l.signal_panel[0] = {v.x, l.content.y, v.w, half};
    l.signal_panel[1] = {v.x, l.content.y + half + gap, v.w, panels_h - half - gap};
  }

  // ---- SETUP ----
  const int rows = 8;
  const int row_h = (l.content.h - (rows - 1) * 4) / rows;
  l.setup_rows = rows;
  for (int i = 0; i < rows; ++i) l.setup_row[i] = {v.x, l.content.y + i * (row_h + 4), v.w, row_h};

  l.valid = inside(v, l.content) && inside(v, l.tabs) && l.target_rows >= 1 && l.message_rows >= 1 && l.setup_row[0].h >= 28 &&
            l.signal_panel[0].h >= 100 && l.signal_panel[1].h >= 100 && l.signal_panel[0].w >= 150;
  return l;
}
}  // namespace orcsdr::ais_ui::compact
