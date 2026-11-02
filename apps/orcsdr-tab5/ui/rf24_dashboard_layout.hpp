#pragma once

#include "dashboard_header_layout.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace orcsdr::rf24::layout {

struct Rect { int x = 0, y = 0, w = 0, h = 0; };

struct View {
  audio_header::HeaderRect viewport{};
  Rect scan_band{}, state{}, detail{}, counts{}, rescan{};
  Rect page{};
  std::array<Rect, 5> tabs{};
  Rect overview_summary{}, overview_channels{}, overview_table{};
  Rect channel_plot{}, channel_overlap{};
  Rect devices_data{};
  Rect settings_button{};
  bool legacy = true;
  bool portrait = false;
  bool valid = false;
};

constexpr bool inside(Rect outer, Rect inner) {
  return inner.w >= 0 && inner.h >= 0 && inner.x >= outer.x && inner.y >= outer.y &&
         inner.x - outer.x <= outer.w - inner.w && inner.y - outer.y <= outer.h - inner.h;
}

constexpr bool inside_viewport(audio_header::HeaderRect outer, Rect inner) {
  return inside(Rect{outer.x, outer.y, outer.w, outer.h}, inner);
}

constexpr bool overlaps(Rect a, Rect b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

constexpr int text_width_px(const char* text, int size) {
  int length = 0;
  if (text) while (text[length]) ++length;
  return length * 6 * size;
}

constexpr const char* tab_label(std::size_t index, int width) {
  constexpr const char* labels[] = {"OVERVIEW", "CHANNELS", "DEVICES", "CSI", "SETTINGS"};
  if (index >= 5) return "";
  if (text_width_px(labels[index], 2) <= width - 12) return labels[index];
  constexpr const char* short_labels[] = {"OVERV", "CHANNEL", "DEVICES", "CSI", "SETUP"};
  return short_labels[index];
}

constexpr View make_view(audio_header::HeaderRect safe) {
  if (safe.w == 1280 && safe.h == 720 && safe.x == 0 && safe.y == 0) {
    View v{};
    v.viewport = safe;
    v.page = {20, 100, 1240, 524};
    v.scan_band = {0, 0, 1280, 100};
    v.state = {808, 25, 160, 28};
    v.detail = {370, 62, 470, 20};
    v.counts = {900, 58, 340, 24};
    v.rescan = {830, 16, 190, 46};
    v.tabs = {{{20,648,242,52},{268,648,242,52},{516,648,242,52},
               {764,648,242,52},{1012,648,242,52}}};
    v.overview_summary = {20,100,400,180};
    v.overview_channels = {438,100,822,180};
    v.overview_table = {20,298,1240,326};
    v.channel_plot = {20,100,1240,430};
    v.channel_overlap = {20,530,1240,94};
    v.devices_data = {20,178,1240,446};
    v.settings_button = {930,520,290,62};
    v.valid = true;
    return v;
  }
  View v{};
  v.viewport = safe;
  v.legacy = false;
  v.portrait = safe.h > safe.w;
  const auto header = audio_header::make_header_layout(safe);
  if (!header.valid) return v;
  const int margin = v.portrait ? 8 : 12;
  const int gap = 8;
  const int footer_h = v.portrait ? 104 : 48;
  const int footer_y = safe.y + safe.h - margin - footer_h;
  if (v.portrait) {
    const int tab_gap = 6;
    const int tab_w = (safe.w - 2 * margin - 2 * tab_gap) / 3;
    const int tab_h = 48;
    for (int i = 0; i < 3; ++i)
      v.tabs[i] = {safe.x + margin + i * (tab_w + tab_gap), footer_y, tab_w, tab_h};
    for (int i = 3; i < 5; ++i)
      v.tabs[i] = {safe.x + margin + (i - 3) * (tab_w + tab_gap), footer_y + tab_h + 8,
                   tab_w, tab_h};
  } else {
    const int tab_gap = 6;
    const int tab_w = (safe.w - 2 * margin - 4 * tab_gap) / 5;
    for (int i = 0; i < 5; ++i)
      v.tabs[i] = {safe.x + margin + i * (tab_w + tab_gap), footer_y, tab_w, 48};
  }
  const int status_y = header.content_top;
  const int status_h = v.portrait ? 72 : 48;
  v.scan_band = {safe.x + margin, status_y, safe.w - 2 * margin, status_h};
  if (v.portrait) {
    v.state = {v.scan_band.x + 8, status_y + 5, v.scan_band.w - 152, 22};
    v.rescan = {v.scan_band.x + v.scan_band.w - 128, status_y + 6, 120, 48};
    v.detail = {v.scan_band.x + 8, status_y + 42, v.scan_band.w / 2 - 12, 22};
    v.counts = {v.scan_band.x + v.scan_band.w / 2, status_y + 42,
                v.scan_band.w / 2 - 8, 22};
  } else {
    v.state = {v.scan_band.x + 8, status_y + 10, 112, 28};
    v.detail = {v.scan_band.x + 128, status_y + 10, v.scan_band.w - 344, 28};
    v.counts = {v.scan_band.x + v.scan_band.w - 208, status_y + 10, 96, 28};
    v.rescan = {v.scan_band.x + v.scan_band.w - 104, status_y + 2, 96, 44};
  }
  const int body_y = status_y + status_h + gap;
  const int body_bottom = footer_y - gap;
  v.page = {safe.x + margin, body_y, safe.w - 2 * margin, body_bottom - body_y};
  if (v.portrait) {
    const int summary_h = 128;
    const int channels_h = 150;
    v.overview_summary = {v.page.x, v.page.y, v.page.w, summary_h};
    v.overview_channels = {v.page.x, v.page.y + summary_h + gap, v.page.w, channels_h};
    v.overview_table = {v.page.x, v.overview_channels.y + channels_h + gap, v.page.w,
                        v.page.y + v.page.h - (v.overview_channels.y + channels_h + gap)};
  } else {
    const int top_h = std::max(76, v.page.h * 35 / 100);
    const int summary_w = v.page.w * 33 / 100;
    v.overview_summary = {v.page.x, v.page.y, summary_w, top_h};
    v.overview_channels = {v.page.x + summary_w + gap, v.page.y,
                           v.page.w - summary_w - gap, top_h};
    v.overview_table = {v.page.x, v.page.y + top_h + gap, v.page.w,
                        v.page.h - top_h - gap};
  }
  const int title_h = 30;
  const int plot_bottom = v.page.y + v.page.h - (v.portrait ? 92 : 70);
  v.channel_plot = {v.page.x, v.page.y + title_h, v.page.w, plot_bottom - (v.page.y + title_h)};
  v.channel_overlap = {v.page.x, plot_bottom + 8, v.page.w,
                       v.page.y + v.page.h - plot_bottom - 8};
  v.devices_data = {v.page.x, v.page.y + 50, v.page.w, v.page.h - 50};
  const int button_w = std::min(290, v.page.w - 24);
  v.settings_button = {v.page.x + v.page.w - button_w - 8,
                       v.page.y + v.page.h - 58, button_w, 48};
  v.valid = inside_viewport(safe, {v.page.x,v.page.y,v.page.w,v.page.h}) &&
      inside_viewport(safe, v.scan_band) && inside_viewport(safe, v.rescan) &&
      inside_viewport(safe, v.overview_summary) && inside_viewport(safe, v.overview_channels) &&
      inside_viewport(safe, v.overview_table) && inside_viewport(safe, v.channel_plot) &&
      inside_viewport(safe, v.channel_overlap) && inside_viewport(safe, v.devices_data) &&
      inside_viewport(safe, v.settings_button);
  for (const auto tab : v.tabs) v.valid = v.valid && inside_viewport(safe, tab) && tab.w >= 44 && tab.h >= 44;
  return v;
}

}  // namespace orcsdr::rf24::layout
