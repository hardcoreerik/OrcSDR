#pragma once

#include "dashboard_header_layout.hpp"

#include <algorithm>

namespace orcsdr::pocsag::layout {
using Rect = audio_header::HeaderRect;

struct Layout {
  bool valid = false, legacy = false, portrait = false;
  Rect viewport, header_status, content, tabs, tab[5];
  Rect live_hero, live_status, live_inbox, scan;
  Rect signal_stats, signal_plot, baud, polarity;
  Rect activity[8],activity_row[8], activity_previous, activity_next, activity_page;
  Rect list_title, list_rows[12], list_previous, list_next, list_page;
  Rect detail_title;
  Rect detail, detail_previous, detail_next, detail_page;
  int activity_row_count = 0, list_row_count = 0, detail_lines = 0, detail_columns = 0;
};

inline bool overlap(Rect a, Rect b) {
  return a.x < b.x + b.w && b.x < a.x + a.w &&
         a.y < b.y + b.h && b.y < a.y + a.h;
}

inline Rect tab_rect(Rect bar, int i, bool portrait) {
  if (portrait) {
    constexpr int gap = 4;
    const int row_h = (bar.h - gap) / 2;
    const int row = i < 3 ? 0 : 1;
    const int count = row == 0 ? 3 : 2;
    const int col = row == 0 ? i : i - 3;
    const int cell_w = (bar.w - gap * (count - 1)) / count;
    const int x = bar.x + col * (cell_w + gap);
    const int y = bar.y + row * (row_h + gap);
    return {x, y, col + 1 == count ? bar.x + bar.w - x : cell_w,
            row == 1 ? bar.y + bar.h - y : row_h};
  }
  const int cell_w = bar.w / 5;
  const int x = bar.x + i * cell_w;
  return {x, bar.y, i == 4 ? bar.x + bar.w - x : cell_w, bar.h};
}

inline Layout make_layout(Rect v) {
  Layout l{};
  l.viewport = v;
  l.portrait = v.h > v.w;

  if (v.x == 0 && v.y == 0 && v.w == 1280 && v.h == 720) {
    l.valid = l.legacy = true;
    l.content = {0, 93, 1280, 537};
    l.header_status = {420, 12, 446, 54};
    l.content = {0, 112, 1280, 522};
    l.tabs = {0, 646, 1280, 74};
    for (int i = 0; i < 5; ++i) l.tab[i] = {i * 256 + 4, 650, 248, 66};
    return l;
  }

  const auto header = audio_header::make_header_layout(v);
  if (!header.valid || v.w > 1280) return l;
  l.header_status = header.status;
  l.tabs = {v.x, v.y + v.h - (l.portrait ? 96 : 48), v.w,
            l.portrait ? 96 : 48};
  for (int i = 0; i < 5; ++i) l.tab[i] = tab_rect(l.tabs, i, l.portrait);
  l.content = {v.x, header.content_top, v.w,
               l.tabs.y - header.content_top - 8};
  if (l.content.h < 180) return l;

  constexpr int pad = 8, gap = 8, touch = 44;
  const int x = v.x + pad, y = l.content.y + pad;
  const int w = v.w - 2 * pad, h = l.content.h - 2 * pad;
  if (l.portrait) {
    l.live_hero = {x, y, w, 180};
    l.live_status = {x, y + 188, w, 150};
    l.live_inbox = {x, y + 346, w, h - 346};
    l.scan = {x + w - 176, l.live_inbox.y, 176, touch};
    l.signal_stats = {x, y, w, std::min(300, h / 3 + 12)};
    l.signal_plot = {x, l.signal_stats.y + l.signal_stats.h + gap, w,
                     std::min(250, h / 2)};
    const int controls_y = y + h - touch;
    l.baud = {x, controls_y, (w - gap) / 2, touch};
    l.polarity = {l.baud.x + l.baud.w + gap, controls_y,
                  w - l.baud.w - gap, touch};
    for (int i = 0; i < 8; ++i) {
      const int cw = (w - gap) / 2;
      const int ch = std::min(72, (h - 4 * gap - touch) / 4);
      l.activity[i] = {x + (i % 2) * (cw + gap), y + (i / 2) * (ch + gap), cw, ch};
    }
    l.activity_row_count = 8;
    l.activity_previous = {x, y + h - touch, touch, touch};
    l.activity_page = {x + touch + gap, l.activity_previous.y,
                       w - 2 * (touch + gap), touch};
    l.activity_next = {x + w - touch, l.activity_previous.y, touch, touch};
    const int list_h = h / 2;
    l.list_title = {x, y, w, touch};
    l.list_previous = {x, y + list_h - touch, touch, touch};
    l.list_page = {x + touch + gap, l.list_previous.y,
                   w - 2 * (touch + gap), touch};
    l.list_next = {x + w - touch, l.list_previous.y, touch, touch};
    const int rows_area_y = l.list_title.y + touch + gap;
    const int rows_area_h = l.list_previous.y - gap - rows_area_y;
    l.list_row_count = std::clamp((rows_area_h + gap) / (52 + gap), 1, 12);
    const int row_h = (rows_area_h - gap * (l.list_row_count - 1)) / l.list_row_count;
    for (int i = 0; i < l.list_row_count; ++i)
      l.list_rows[i] = {x, rows_area_y + i * (row_h + gap), w, row_h};
    const int detail_y = y + list_h + gap;
    l.detail_title = {x, detail_y, w, touch};
    l.detail_previous = {x, y + h - touch, touch, touch};
    l.detail_page = {x + touch + gap, l.detail_previous.y,
                     w - 2 * (touch + gap), touch};
    l.detail_next = {x + w - touch, l.detail_previous.y, touch, touch};
    l.detail = {x, detail_y + touch + gap, w,
                l.detail_previous.y - gap - (detail_y + touch + gap)};
    l.detail_columns = std::max(1, l.detail.w / 12);
    l.detail_lines = std::max(1, l.detail.h / 24);
  } else {
    const int main_h = std::min(160, h / 2);
    l.live_hero = {x, y, (w - gap) * 55 / 100, main_h};
    l.live_status = {l.live_hero.x + l.live_hero.w + gap, y,
                     w - l.live_hero.w - gap, main_h};
    l.live_inbox = {x, y + main_h + gap, w, h - main_h - gap};
    l.scan = {x + w - 176, l.live_inbox.y, 176, touch};
    l.signal_stats = {x, y, w / 2, h - touch - gap};
    l.signal_plot = {l.signal_stats.x + l.signal_stats.w + gap, y,
                     w - l.signal_stats.w - gap, h - touch - gap};
    l.baud = {x, y + h - touch, (w - gap) / 2, touch};
    l.polarity = {l.baud.x + l.baud.w + gap, l.baud.y,
                  w - l.baud.w - gap, touch};
    const int card_h = std::min(72, (h - 2 * gap - touch) / 2);
    const int card_w = (w - 3 * gap) / 4;
    for (int i = 0; i < 8; ++i)
      l.activity[i] = {x + (i % 4) * (card_w + gap), y + (i / 4) * (card_h + gap), card_w, card_h};
    const int activity_rows_h = h - touch - 2 * gap;
    l.activity_row_count = std::clamp((activity_rows_h + gap) / (24 + gap),1,8);
    l.activity_previous = {x, y + h - touch, touch, touch};
    l.activity_page = {x + touch + gap, l.activity_previous.y,
                       w - 2 * (touch + gap), touch};
    l.activity_next = {x + w - touch, l.activity_previous.y, touch, touch};
    const int list_w = (w - gap) * 45 / 100;
    l.list_title = {x, y, list_w, touch};
    l.list_previous = {x, y + h - touch, touch, touch};
    l.list_page = {x + touch + gap, l.list_previous.y,
                   list_w - 2 * (touch + gap), touch};
    l.list_next = {x + list_w - touch, l.list_previous.y, touch, touch};
    const int rows_y = y + touch + gap;
    const int rows_h = l.list_previous.y - gap - rows_y;
    l.list_row_count = std::clamp((rows_h + gap) / (52 + gap), 1, 12);
    const int row_h = (rows_h - gap * (l.list_row_count - 1)) / l.list_row_count;
    for (int i = 0; i < l.list_row_count; ++i)
      l.list_rows[i] = {x, rows_y + i * (row_h + gap), list_w, row_h};
    const int detail_x = x + list_w + gap;
    l.detail_previous = {detail_x, y + h - touch, touch, touch};
    l.detail_page = {detail_x + touch + gap, l.detail_previous.y,
                     w - list_w - 2 * (touch + gap), touch};
    l.detail_next = {x + w - touch, l.detail_previous.y, touch, touch};
    l.detail_title = {detail_x, y, w - list_w - gap, touch};
    l.detail = {detail_x, l.detail_title.y + touch + gap,
                w - list_w - gap, h - 2 * touch - 4 * gap - 28};
    l.detail_columns = std::max(1, l.detail.w / 12);
    l.detail_lines = std::max(1, l.detail.h / 24);
  }

  const int ranking_h=h-touch-2*gap;
  const int ranking_row_h=std::min(44,(ranking_h-gap*(l.activity_row_count-1))/l.activity_row_count);
  for(int i=0;i<l.activity_row_count;++i) l.activity_row[i]={x,y+i*(ranking_row_h+gap),w,ranking_row_h};
  l.valid = audio_header::header_inside(v, l.content) &&
            audio_header::header_inside(v, l.tabs);
  for (const auto r : l.tab) l.valid = l.valid && audio_header::header_inside(l.tabs, r) && r.w >= touch && r.h >= touch;
  const Rect required[] = {l.live_hero,l.live_status,l.live_inbox,l.scan,
    l.signal_stats,l.signal_plot,l.baud,l.polarity,l.list_title,
    l.list_previous,l.list_next,l.list_page,l.detail_title,l.detail,l.detail_previous,
    l.detail_next,l.detail_page};
  for (const auto r : required)
    l.valid = l.valid && audio_header::header_inside(l.content,r);
  for (const auto r : l.activity)
    l.valid = l.valid && audio_header::header_inside(l.content,r);
  for(int i=0;i<l.activity_row_count;++i) l.valid=l.valid && audio_header::header_inside(l.content,l.activity_row[i]);
  l.valid = l.valid && l.activity_row_count >= 1 && l.activity_row_count <= 8 &&
      audio_header::header_inside(l.content,l.activity_previous) &&
      audio_header::header_inside(l.content,l.activity_next) &&
      audio_header::header_inside(l.content,l.activity_page);
  l.valid = l.valid && audio_header::header_inside(l.live_inbox,l.scan) &&
      !overlap(l.signal_plot,l.baud) && !overlap(l.signal_plot,l.polarity) &&
      !overlap(l.detail,l.detail_previous) && !overlap(l.detail,l.detail_page) &&
      !overlap(l.detail,l.detail_next);
  for (int i=0;i<l.activity_row_count;++i)
    l.valid = l.valid && l.activity_row[i].h >= 24 &&
        !overlap(l.activity_row[i],l.activity_previous) &&
        !overlap(l.activity_row[i],l.activity_page) &&
        !overlap(l.activity_row[i],l.activity_next);
  for (int i = 0; i < l.list_row_count; ++i)
    l.valid = l.valid && audio_header::header_inside(l.content,l.list_rows[i]) &&
              l.list_rows[i].h >= 52 &&
              !overlap(l.list_rows[i],l.list_previous) &&
              !overlap(l.list_rows[i],l.list_next) &&
              !overlap(l.list_rows[i],l.list_page);
  return l;
}
}  // namespace orcsdr::pocsag::layout
