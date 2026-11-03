#pragma once

#include "dashboard_header_layout.hpp"

#include <algorithm>

namespace orcsdr::p25::layout {
using Rect = audio_header::HeaderRect;

inline bool inside(Rect outer, Rect r) {
  return r.w > 0 && r.h > 0 && r.x >= outer.x && r.y >= outer.y &&
         r.x + r.w <= outer.x + outer.w && r.y + r.h <= outer.y + outer.h;
}
inline bool overlap(Rect a, Rect b) {
  return a.x < b.x + b.w && b.x < a.x + a.w &&
         a.y < b.y + b.h && b.y < a.y + a.h;
}

struct Layout {
  bool valid = false, legacy = false, portrait = false;
  Rect viewport{}, content{}, tabs{}, tab[5];
  Rect monitor_control{}, monitor_profile{}, monitor_grant{}, monitor_signal{}, monitor_actions[5];
  Rect spectrum_frequency{}, spectrum_filter{}, spectrum_activity{}, spectrum_plot{}, waterfall{};
  Rect span_down{}, span_up{}, spectrum_hint{};
  Rect talk_summary{}, talk_current{}, talk_rows[8]{}, talk_previous{}, talk_next{}, talk_page{};
  int talk_row_count = 0;
  Rect program_profiles{}, profile_rows[8]{}, profile_previous{}, profile_next{}, profile_page{};
  Rect program_profiles_tab{}, program_details_tab{};
  int profile_row_count = 0;
  Rect program_details{}, program_candidates[8]{}, program_controls[6]{}, policy_controls[2]{};
  Rect program_candidate_previous{}, program_candidate_next{}, program_candidate_page{};
  int candidate_row_count = 0;
  Rect health_summary[3]{}, health_rows[8]{}, health_footer[3]{};
  int health_row_count = 0;
};

inline Rect tab_rect(Rect bar, int i, bool portrait) {
  if (portrait) {
    constexpr int gap = 4;
    const int row = i < 3 ? 0 : 1;
    const int count = row == 0 ? 3 : 2;
    const int col = row == 0 ? i : i - 3;
    const int cell_w = (bar.w - gap * (count - 1)) / count;
    const int cell_h = (bar.h - gap) / 2;
    const int x = bar.x + col * (cell_w + gap);
    const int y = bar.y + row * (cell_h + gap);
    return {x, y, col + 1 == count ? bar.x + bar.w - x : cell_w,
            row == 1 ? bar.y + bar.h - y : cell_h};
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
    l.content = {0, 132, 1280, 498};
    l.tabs = {0, 630, 1280, 90};
    for (int i = 0; i < 5; ++i) l.tab[i] = {i * 256, 630, 256, 90};
    l.monitor_control = {24, 150, 420, 118};
    l.monitor_profile = {458, 150, 798, 118};
    l.monitor_grant = {24, 282, 800, 205};
    l.monitor_signal = {838, 282, 418, 205};
    const int ax[] = {24, 270, 516, 762, 1008};
    const int aw[] = {232, 232, 232, 232, 248};
    for (int i = 0; i < 5; ++i) l.monitor_actions[i] = {ax[i], 505, aw[i], 92};
    l.spectrum_frequency = {42, 180, 430, 55};
    l.spectrum_filter = {510, 145, 250, 80};
    l.spectrum_activity = {880, 145, 354, 80};
    l.spectrum_plot = {46, 246, 1188, 145};
    l.waterfall = {46, 420, 1188, 130};
    l.span_down = {390, 565, 80, 45};
    l.span_up = {810, 565, 80, 45};
    l.spectrum_hint = {900, 565, 334, 45};
    l.talk_summary = {24, 148, 1232, 86};
    l.talk_current = {770, 160, 466, 62};
    l.talk_row_count = 8;
    for (int i = 0; i < 8; ++i) l.talk_rows[i] = {32, 280 + i * 37, 1216, 37};
    l.program_profiles = {24, 148, 600, 368};
    l.profile_previous = {420, 148, 94, 46};
    l.profile_next = {514, 148, 110, 46};
    l.profile_row_count = 8;
    for (int i = 0; i < 8; ++i) l.profile_rows[i] = {42, 198 + i * 34, 564, 32};
    l.program_details = {638, 148, 618, 368};
    l.candidate_row_count = 3;
    for (int i = 0; i < 3; ++i) l.program_candidates[i] = {668, 348 + i * 28, 550, 28};
    const int cx[] = {24, 226, 428, 638, 840, 1042};
    const int cw[] = {190, 190, 190, 190, 190, 214};
    for (int i = 0; i < 6; ++i) l.program_controls[i] = {cx[i], 536, cw[i], 62};
    l.policy_controls[0] = {668, 438, 260, 50};
    l.policy_controls[1] = {948, 438, 270, 50};
    l.health_summary[0] = {24, 148, 390, 110};
    l.health_summary[1] = {428, 148, 390, 110};
    l.health_summary[2] = {832, 148, 424, 110};
    l.health_row_count = 8;
    for (int i = 0; i < 8; ++i) l.health_rows[i] = {24 + (i % 4) * 308, 274 + (i / 4) * 126, 294, 112};
    l.health_footer[0] = {24, 536, 280, 74};
    l.health_footer[1] = {318, 536, 280, 74};
    l.health_footer[2] = {612, 536, 644, 74};
    return l;
  }

  const auto header = audio_header::make_header_layout(v);
  if (!header.valid || v.w > 1280) return l;
  const int gap = 8;
  const int touch = 44;
  const int card_gap = 6;
  const int tab_h = l.portrait ? 96 : 48;
  l.tabs = {v.x, v.y + v.h - tab_h, v.w, tab_h};
  for (int i = 0; i < 5; ++i) l.tab[i] = tab_rect(l.tabs, i, l.portrait);
  l.content = {v.x + gap, header.content_top, v.w - 2 * gap,
               l.tabs.y - header.content_top - gap};
  if (l.content.h < 150 || l.content.w < 360) return l;
  const int x = l.content.x, y = l.content.y, w = l.content.w, h = l.content.h;
  if (l.portrait) {
    const int action_h = 2 * touch + card_gap;
    const int signal_h = 86;
    const int top_h = 92;
    const int grant_h = std::max(150, h - top_h * 2 - signal_h - action_h - 4 * card_gap);
    l.monitor_control = {x, y, w, top_h};
    l.monitor_profile = {x, y + top_h + card_gap, w, top_h};
    l.monitor_grant = {x, y + 2 * (top_h + card_gap), w, grant_h};
    l.monitor_signal = {x, l.monitor_grant.y + grant_h + card_gap, w, signal_h};
    for (int i = 0; i < 5; ++i) {
      const int row = i < 3 ? 0 : 1;
      const int col = row == 0 ? i : i - 3;
      const int count = row == 0 ? 3 : 2;
      const int cell = (w - (count - 1) * card_gap) / count;
      const int ay = y + h - action_h + row * (touch + card_gap);
      const int ax = x + col * (cell + card_gap);
      l.monitor_actions[i] = {ax, ay, col + 1 == count ? x + w - ax : cell, touch};
    }
    l.spectrum_frequency = {x, y, w / 2 - card_gap, 52};
    l.spectrum_filter = {x + w / 2, y, w / 2, 52};
    l.spectrum_activity = {x, y + 58, w, touch};
    const int controls_h = touch + card_gap;
    const int plot_y = y + 58 + touch + card_gap;
    const int plot_h = std::max(90, (h - (plot_y - y) - controls_h - 48) / 2);
    l.spectrum_plot = {x, plot_y, w, plot_h};
    l.waterfall = {x, plot_y + plot_h + card_gap + 24, w, plot_h};
    l.span_down = {x, y + h - touch, touch, touch};
    l.span_up = {x + w - touch, y + h - touch, touch, touch};
    l.spectrum_hint = {x + touch + card_gap, y + h - touch, w - 2 * (touch + card_gap), touch};
    l.talk_summary = {x, y, w, 64};
    l.talk_current = {x, y + 70, w, 60};
    const int rows_top = y + 136;
    const int pager_y = y + h - touch;
    l.talk_row_count = std::clamp((pager_y - rows_top - 4 * card_gap) / 52, 1, 8);
    for (int i = 0; i < l.talk_row_count; ++i) l.talk_rows[i] = {x, rows_top + i * (52 + card_gap), w, 52};
    l.talk_previous = {x, pager_y, touch, touch};
    l.talk_next = {x + w - touch, pager_y, touch, touch};
    l.talk_page = {x + touch + card_gap, pager_y, w - 2 * (touch + card_gap), touch};
    l.profile_row_count = std::clamp((h - 320) / 48, 1, 8);
    l.program_profiles = {x, y, w, h - 2 * (touch + card_gap)};
    l.program_profiles_tab = {x, y, (w - card_gap) / 2, touch};
    l.program_details_tab = {x + (w + card_gap) / 2, y, (w - card_gap) / 2, touch};
    for (int i = 0; i < l.profile_row_count; ++i) l.profile_rows[i] = {x + 4, y + 58 + i * 48, w - 8, 44};
    l.profile_previous = {x, y + 58 + l.profile_row_count * 48, touch, touch};
    l.profile_next = {x + w - touch, y + 58 + l.profile_row_count * 48, touch, touch};
    l.profile_page = {x + touch + card_gap, l.profile_previous.y, w - 2 * (touch + card_gap), touch};
    l.program_details = {x, y, w, h};
    l.candidate_row_count = std::clamp((h - 280) / 34, 1, 8);
    for (int i = 0; i < l.candidate_row_count; ++i) l.program_candidates[i] = {x + 4, y + 180 + i * 34, w - 8, 30};
    l.program_candidate_previous = {x, y + 180 + l.candidate_row_count * 34, touch, touch};
    l.program_candidate_next = {x + w - touch, l.program_candidate_previous.y, touch, touch};
    l.program_candidate_page = {x + touch + card_gap, l.program_candidate_previous.y, w - 2 * (touch + card_gap), touch};
    for (int i = 0; i < 6; ++i) {
      const int row = i / 2, col = i % 2;
      const int cell = (w - card_gap) / 2;
      l.program_controls[i] = {x + col * (cell + card_gap), y + h - 3 * (touch + card_gap) + row * (touch + card_gap), cell, touch};
    }
    l.policy_controls[0] = {x, y + h - touch, (w - card_gap) / 2, touch};
    l.policy_controls[1] = {x + (w + card_gap) / 2, y + h - touch, (w - card_gap) / 2, touch};
    for (int i = 0; i < 3; ++i) l.health_summary[i] = {x, y + i * 54, w, 50};
    l.health_row_count = 8;
    for (int i = 0; i < 8; ++i) l.health_rows[i] = {x, y + 168 + i * 54, w, 50};
    for (int i = 0; i < 3; ++i) l.health_footer[i] = {x, y + h - (3 - i) * 48, w, 44};
  } else {
    // Compact landscape: the smaller content height is handled by paged rows and a two-column summary.
    const int col_gap = 8, col_w = (w - col_gap) / 2;
    l.monitor_control = {x, y, col_w, 80};
    l.monitor_profile = {x + col_w + col_gap, y, col_w, 80};
    l.monitor_grant = {x, y + 88, col_w, h - 88 - touch - card_gap};
    l.monitor_signal = {x + col_w + col_gap, y + 88, col_w, h - 88 - touch - card_gap};
    const int cell = (w - 4 * card_gap) / 5;
    for (int i = 0; i < 5; ++i) l.monitor_actions[i] = {x + i * (cell + card_gap), y + h - touch, i == 4 ? x + w - (x + i * (cell + card_gap)) : cell, touch};
    l.spectrum_frequency = {x, y, col_w, 44};
    l.spectrum_filter = {x + col_w + col_gap, y, col_w, 44};
    l.spectrum_activity = {x, y + 50, w, 34};
    l.spectrum_plot = {x, y + 90, w, std::max(64, (h - 90 - 64) / 2)};
    l.waterfall = {x, l.spectrum_plot.y + l.spectrum_plot.h + 4, w, std::max(44, h - (l.spectrum_plot.y - y) - l.spectrum_plot.h - 64)};
    l.span_down = {x, y + h - touch, touch, touch};
    l.span_up = {x + w - touch, y + h - touch, touch, touch};
    l.spectrum_hint = {x + touch + gap, y + h - touch, w - 2 * (touch + gap), touch};
    l.talk_summary = {x, y, w, 44};
    l.talk_current = {x, y + 50, w, 44};
    const int rows_top = y + 100, pager_y = y + h - touch;
    l.talk_row_count = std::clamp((pager_y - rows_top) / 44, 1, 3);
    for (int i = 0; i < l.talk_row_count; ++i) l.talk_rows[i] = {x, rows_top + i * 44, w, 44};
    l.talk_previous = {x, pager_y, touch, touch};
    l.talk_next = {x + w - touch, pager_y, touch, touch};
    l.talk_page = {x + touch + gap, pager_y, w - 2 * (touch + gap), touch};
    l.profile_row_count = std::clamp((h - 200) / 48, 1, 3);
    l.program_profiles = {x, y, col_w, h - touch - gap};
    l.program_profiles_tab = {x, y, (col_w - gap) / 2, touch};
    l.program_details_tab = {x + (col_w + gap) / 2, y, (col_w - gap) / 2, touch};
    for (int i = 0; i < l.profile_row_count; ++i) l.profile_rows[i] = {x + 4, y + 50 + i * 48, col_w - 8, 44};
    l.profile_previous = {x, y + 50 + l.profile_row_count * 48, touch, touch};
    l.profile_next = {x + col_w - touch, y + 50 + l.profile_row_count * 48, touch, touch};
    l.profile_page = {x + touch + gap, l.profile_previous.y, col_w - 2 * (touch + gap), touch};
    l.program_details = {x + col_w + gap, y, col_w, h - touch - gap};
    l.candidate_row_count = std::clamp((h - 280) / 34, 1, 8);
    for (int i = 0; i < l.candidate_row_count; ++i) l.program_candidates[i] = {l.program_details.x + 4, y + 180 + i * 34, col_w - 8, 30};
    l.program_candidate_previous = {l.program_details.x, y + 180 + l.candidate_row_count * 34, touch, touch};
    l.program_candidate_next = {l.program_details.x + col_w - touch, l.program_candidate_previous.y, touch, touch};
    l.program_candidate_page = {l.program_details.x + touch + gap, l.program_candidate_previous.y, col_w - 2 * (touch + gap), touch};
    for (int i = 0; i < 6; ++i) {
      const int row = i / 3, col = i % 3;
      const int cw = (w - 2 * card_gap) / 3;
      l.program_controls[i] = {x + col * (cw + card_gap), y + h - 2 * (touch + card_gap) + row * (touch + card_gap), cw, touch};
    }
    l.policy_controls[0] = {x + col_w + gap, y + h - touch, (col_w - gap) / 2, touch};
    l.policy_controls[1] = {x + col_w + gap + (col_w + gap) / 2, y + h - touch, (col_w - gap) / 2, touch};
    const int summary_w = (w - 2 * col_gap) / 3;
    for (int i = 0; i < 3; ++i) l.health_summary[i] = {x + i * (summary_w + col_gap), y, i == 2 ? x + w - (x + i * (summary_w + col_gap)) : summary_w, 48};
    l.health_row_count = 8;
    const int health_w = (w - col_gap) / 2;
    for (int i = 0; i < 8; ++i)
      l.health_rows[i] = {x + (i % 2) * (health_w + col_gap), y + 56 + (i / 2) * 50,
                          i % 2 ? x + w - (x + health_w + col_gap) : health_w, 44};
    for (int i = 0; i < 3; ++i) l.health_footer[i] = {x + i * (w / 3), y + h - 44, i == 2 ? x + w - (x + i * (w / 3)) : w / 3, 44};
  }

  l.valid = audio_header::header_inside(v, l.content) && audio_header::header_inside(v, l.tabs);
  for (const auto r : l.tab) l.valid = l.valid && audio_header::header_inside(l.tabs, r) && r.w >= 44 && r.h >= 44;
  return l;
}
}  // namespace orcsdr::p25::layout
