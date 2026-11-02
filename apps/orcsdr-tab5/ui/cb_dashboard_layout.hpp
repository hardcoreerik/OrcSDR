#pragma once

#include "dashboard_header_layout.hpp"

namespace orcsdr::cb {
using LayoutRect = audio_header::HeaderRect;

struct DashboardLayout {
  LayoutRect viewport, content, tabs, tab[5];
  LayoutRect listen_now, listen_meter, listen_controls, control[12];
  LayoutRect listen_channels, listen_channel_title, listen_channel[40];
  LayoutRect spectrum_title, spectrum_plot, spectrum_axis, waterfall;
  LayoutRect activity_title, activity_summary, activity_previous, activity_page,
      activity_next, activity_clear, activity_row[7];
  LayoutRect channels_title, channels_subtitle, channels_clear, channels_ssb,
      channel_cell[40];
  LayoutRect setup_row[8], setup_label[8], setup_help[8], setup_minus[8],
      setup_value[8], setup_plus[8], setup_previous, setup_page, setup_next;
  int activity_row_count = 7, setup_page_size = 8, setup_page_count = 1;
  int channel_columns = 8, listen_channel_columns = 40;
  bool portrait = false, legacy = false, valid = false;
};

inline LayoutRect tab_rect(LayoutRect bar, int count, int index, bool portrait,
                           int portrait_first_row_count = 3) {
  if (index < 0 || index >= count || count <= 0) return {};
  if (portrait) {
    const int gap = 4;
    if (index < portrait_first_row_count) {
      const int width = (bar.w - (portrait_first_row_count - 1) * gap) /
                        portrait_first_row_count;
      return {bar.x + index * (width + gap), bar.y, width, bar.h / 2};
    }
    const int second_count = count - portrait_first_row_count;
    const int row_y = bar.y + bar.h / 2 + gap;
    const int row_h = bar.h - (row_y - bar.y);
    const int width = (bar.w - (second_count - 1) * gap) / second_count;
    const int offset = index - portrait_first_row_count;
    return {bar.x + offset * (width + gap), row_y, width, row_h};
  }
  const int width = bar.w / count;
  const int x = bar.x + index * width;
  return {x, bar.y, index + 1 == count ? bar.x + bar.w - x : width, bar.h};
}

inline DashboardLayout make_layout(LayoutRect v) {
  DashboardLayout l{};
  l.viewport = v;
  l.portrait = v.h > v.w;
  if (v.x == 0 && v.y == 0 && v.w == 1280 && v.h == 720) {
    l.legacy = true;
    l.content = {0, 93, 1280, 537}; l.tabs = {0, 630, 1280, 90};
    for (int i = 0; i < 5; ++i) l.tab[i] = {i * 256 + 4, 634, 248, 82};
    l.listen_now = {24, 104, 520, 256}; l.listen_meter = {48, 300, 472, 22};
    l.listen_controls = {556, 104, 700, 256};
    for (int i = 0; i < 12; ++i)
      l.control[i] = {572 + (i % 4) * 172, 116 + (i / 4) * 82, 160, 70};
    l.listen_channels = {24, 372, 1232, 250}; l.listen_channel_title = {40, 390, 1200, 24};
    l.spectrum_title = {36, 110, 1208, 52}; l.spectrum_plot = {24, 146, 1232, 230};
    l.spectrum_axis = {24, 376, 1232, 30}; l.waterfall = {24, 410, 1232, 184};
    l.activity_title = {24, 104, 1232, 52}; l.activity_summary = {320, 132, 680, 48};
    l.activity_previous = {24, 130, 80, 52}; l.activity_page = {108, 130, 124, 52};
    l.activity_next = {240, 130, 80, 52}; l.activity_clear = {1016, 130, 240, 52};
    l.activity_row_count = 7;
    for (int i = 0; i < 7; ++i) l.activity_row[i] = {24, 196 + i * 56, 1232, 48};
    l.channels_title = {24, 100, 1232, 42}; l.channels_subtitle = {24, 146, 720, 32};
    l.channels_clear = {760, 130, 240, 52}; l.channels_ssb = {1016, 130, 240, 52};
    l.channel_columns = 8;
    for (int i = 0; i < 40; ++i)
      l.channel_cell[i] = {24 + (i % 8) * 154, 196 + (i / 8) * 84, 146, 76};
    l.setup_page_size = 8; l.setup_page_count = 1;
    for (int i = 0; i < 8; ++i) {
      const int y = 104 + i * 65;
      l.setup_row[i] = {24, y, 1232, 58}; l.setup_label[i] = {44, y + 4, 680, 24};
      l.setup_help[i] = {44, y + 28, 680, 24};
      l.setup_minus[i] = {760, y + 3, 110, 52};
      l.setup_value[i] = {880, y + 3, 250, 52};
      l.setup_plus[i] = {1140, y + 3, 110, 52};
    }
    l.setup_previous = {}; l.setup_page = {}; l.setup_next = {};
    l.valid = true;
    return l;
  }

  if (v.w > 1280) return l;
  const auto header = audio_header::make_header_layout(v);
  if (!header.valid) return l;
  l.legacy = false;
  const int gap = 8;
  const int tab_h = l.portrait ? 96 : 48;
  l.tabs = {v.x, v.y + v.h - tab_h, v.w, tab_h};
  for (int i = 0; i < 5; ++i) l.tab[i] = tab_rect(l.tabs, 5, i, l.portrait);
  l.content = {v.x, header.content_top, v.w,
               l.tabs.y - header.content_top - 8};
  if (l.content.h < 260) return l;
  const int x = v.x + gap, w = v.w - 2 * gap, top = l.content.y + gap;

  if (l.portrait) {
    l.listen_now = {x, top, w, 132};
    l.listen_controls = {x, top + 140, w, 204};
    const int control_gap = 6, control_w = (w - 2 * control_gap) / 3;
    for (int i = 0; i < 12; ++i)
      l.control[i] = {x + (i % 3) * (control_w + control_gap),
          top + 148 + (i / 3) * 48, control_w, 44};
    l.listen_channels = {x, top + 352, w, 392};
    l.listen_channel_title = {x + 8, l.listen_channels.y + 4, w - 16, 24};
    l.listen_channel_columns = 5;
    const int listen_cell_gap = 2, listen_cell_w = (w - 4 * listen_cell_gap) / 5;
    for (int i = 0; i < 40; ++i)
      l.listen_channel[i] = {x + (i % 5) * (listen_cell_w + listen_cell_gap),
          l.listen_channels.y + 32 + (i / 5) * 44, listen_cell_w, 44};
    l.listen_meter = {x + 12, top + 98, w - 24, 16};

    l.spectrum_title = {x + 8, top + 4, w - 16, 48};
    l.spectrum_plot = {x, top + 60, w, 240};
    l.spectrum_axis = {x, l.spectrum_plot.y + l.spectrum_plot.h, w, 28};
    l.waterfall = {x, l.spectrum_axis.y + l.spectrum_axis.h + 4, w, 300};

    l.activity_title = {x + 8, top + 4, w - 16, 28};
    l.activity_previous = {x, top + 40, 56, 44};
    l.activity_page = {x + 64, top + 40, w - 192, 44};
    l.activity_next = {x + w - 120, top + 40, 56, 44};
    l.activity_clear = {x + w - 56, top + 40, 56, 44};
    l.activity_summary = {x, top + 90, w, 40};
    l.activity_row_count = 7;
    for (int i = 0; i < 7; ++i) l.activity_row[i] = {x, top + 136 + i * 92, w, 84};

    l.channels_title = {x + 8, top + 4, w - 16, 28};
    l.channels_subtitle = {x, top + 34, w, 24};
    const int action_w = (w - gap) / 2;
    l.channels_clear = {x, top + 62, action_w, 44};
    l.channels_ssb = {x + action_w + gap, top + 62, w - action_w - gap, 44};
    l.channel_columns = 4;
    const int channel_cell_gap = 4, channel_cell_w = (w - 3 * channel_cell_gap) / 4, cell_h = 68;
    for (int i = 0; i < 40; ++i)
      l.channel_cell[i] = {x + (i % 4) * (channel_cell_w + channel_cell_gap),
          top + 114 + (i / 4) * cell_h, channel_cell_w, cell_h - 4};

    l.setup_page_size = 4; l.setup_page_count = 2;
    for (int i = 0; i < 8; ++i) {
      const int row = i % 4, y = top + 8 + row * 164;
      l.setup_row[i] = {x, y, w, 156};
      l.setup_label[i] = {x + 8, y + 8, w - 16, 24};
      l.setup_help[i] = {x + 8, y + 34, w - 16, 52};
      l.setup_minus[i] = {x + 8, y + 100, 64, 44};
      l.setup_value[i] = {x + 80, y + 100, w - 160, 44};
      l.setup_plus[i] = {x + w - 72, y + 100, 64, 44};
    }
    l.setup_previous = {x, l.content.y + l.content.h - 48, 64, 44};
    l.setup_page = {x + 72, l.setup_previous.y, w - 144, 44};
    l.setup_next = {x + w - 64, l.setup_previous.y, 64, 44};
  } else {
    l.listen_now = {x, top, w, 76};
    l.listen_controls = {x, top + 84, w, 96};
    const int control_gap = 6, control_w = (w - 5 * control_gap) / 6;
    for (int i = 0; i < 12; ++i)
      l.control[i] = {x + (i % 6) * (control_w + control_gap),
          top + 88 + (i / 6) * 48, control_w, 44};
    l.listen_channels = {x, top + 184, w, 112};
    l.listen_channel_title = {x + 8, l.listen_channels.y + 2, w - 16, 20};
    l.listen_channel_columns = 20;
    const int listen_cell_gap = 2, listen_cell_w = (w - 19 * listen_cell_gap) / 20;
    for (int i = 0; i < 40; ++i)
      l.listen_channel[i] = {x + (i % 20) * (listen_cell_w + listen_cell_gap),
          l.listen_channels.y + 24 + (i / 20) * 44, listen_cell_w, 44};
    l.listen_meter = {x + 700, top + 18, w - 720, 16};

    l.spectrum_title = {x + 8, top + 4, w - 16, 28};
    l.spectrum_plot = {x, top + 36, w, 88};
    l.spectrum_axis = {x, l.spectrum_plot.y + l.spectrum_plot.h, w, 20};
    l.waterfall = {x, l.spectrum_axis.y + l.spectrum_axis.h + 4, w, 128};

    l.activity_title = {x, top + 4, 284, 44};
    l.activity_previous = {x + 292, top + 4, 48, 44};
    l.activity_page = {x + 348, top + 4, 100, 44};
    l.activity_next = {x + 456, top + 4, 48, 44};
    l.activity_clear = {x + w - 160, top + 4, 160, 44};
    l.activity_summary = {x + 8, top + 48, w - 16, 24};
    l.activity_row_count = 4;
    for (int i = 0; i < 4; ++i) l.activity_row[i] = {x, top + 80 + i * 46, w, 44};

    l.channels_title = {x + 8, top + 4, 360, 24};
    l.channels_subtitle = {x + 380, top + 4, w - 388, 24};
    const int action_w = (w - gap) / 2;
    l.channels_clear = {x, top + 28, action_w, 44};
    l.channels_ssb = {x + action_w + gap, top + 28, w - action_w - gap, 44};
    l.channel_columns = 8;
    const int channel_cell_gap = 4, channel_cell_w = (w - 7 * channel_cell_gap) / 8, cell_h = 44;
    for (int i = 0; i < 40; ++i)
      l.channel_cell[i] = {x + (i % 8) * (channel_cell_w + channel_cell_gap),
          top + 76 + (i / 8) * cell_h, channel_cell_w, cell_h};

    l.setup_page_size = 4; l.setup_page_count = 2;
    for (int i = 0; i < 8; ++i) {
      const int row = i % 4, y = top + row * 60;
      l.setup_row[i] = {x, y, w, 56};
      l.setup_label[i] = {x + 8, y + 3, w - 340, 22};
      l.setup_help[i] = {x + 8, y + 25, w - 340, 28};
      l.setup_minus[i] = {x + w - 312, y + 6, 48, 44};
      l.setup_value[i] = {x + w - 256, y + 6, 200, 44};
      l.setup_plus[i] = {x + w - 48, y + 6, 44, 44};
    }
    l.setup_previous = {x, l.content.y + l.content.h - 44, 64, 44};
    l.setup_page = {x + 72, l.setup_previous.y, w - 144, 44};
    l.setup_next = {x + w - 64, l.setup_previous.y, 64, 44};
  }

  const LayoutRect required[] = {l.content,l.tabs,l.listen_now,l.listen_meter,
      l.listen_controls,l.listen_channels,l.listen_channel_title,l.spectrum_title,
      l.spectrum_plot,l.spectrum_axis,l.waterfall,l.activity_title,l.activity_summary,
      l.activity_previous,l.activity_page,l.activity_next,l.activity_clear,
      l.channels_title,l.channels_subtitle,l.channels_clear,l.channels_ssb,
      l.setup_previous,l.setup_page,l.setup_next};
  l.valid = true;
  for (const auto r : required) if (!audio_header::header_inside(v,r)) l.valid = false;
  for (const auto r : l.tab) if (!audio_header::header_inside(v,r)) l.valid = false;
  for (const auto r : l.control) if (!audio_header::header_inside(v,r)) l.valid = false;
  for (const auto r : l.listen_channel) if (!audio_header::header_inside(v,r)) l.valid = false;
  for (int i = 0; i < l.activity_row_count; ++i)
    if (!audio_header::header_inside(v,l.activity_row[i])) l.valid = false;
  for (const auto r : l.channel_cell) if (!audio_header::header_inside(v,r)) l.valid = false;
  for (int i = 0; i < 8; ++i) {
    if (!audio_header::header_inside(v,l.setup_row[i]) ||
        !audio_header::header_inside(v,l.setup_label[i]) ||
        !audio_header::header_inside(v,l.setup_help[i]) ||
        !audio_header::header_inside(v,l.setup_value[i])) l.valid = false;
    if (!audio_header::header_inside(v,l.setup_minus[i]) ||
        !audio_header::header_inside(v,l.setup_plus[i])) l.valid = false;
  }
  return l;
}
}  // namespace orcsdr::cb
