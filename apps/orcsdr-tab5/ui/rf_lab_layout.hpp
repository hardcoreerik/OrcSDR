#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

namespace orcsdr::rf_lab::layout {

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
};

struct View {
  Rect bounds;
  Rect back;
  Rect title;
  Rect status;
  std::array<Rect, 4> tabs{};
  Rect live_plot;
  Rect spectrum;
  Rect spectrum_data;
  Rect waterfall;
  Rect waterfall_data;
  Rect metrics;
  Rect controls;
  std::array<Rect, 11> control_buttons{};
  std::array<Rect, 4> measurement_buttons{};
  Rect message;
};

constexpr int text_width_px(const char* value, int size) {
  int length = 0;
  if (value) while (value[length]) ++length;
  return length * 6 * size;
}

constexpr const char* tab_label(std::size_t index, int width) {
  constexpr const char* labels[] = {"LIVE", "CONTROLS", "MEASUREMENTS", "RECORDS"};
  if (index >= 4) return "";
  if (text_width_px(labels[index], 2) <= width - 12) return labels[index];
  if (index == 1 && text_width_px("CTRL", 2) <= width - 12) return "CTRL";
  return index == 2 && text_width_px("MEASURE", 2) <= width - 12 ? "MEASURE" : labels[index];
}

constexpr const char* source_label(bool available, int width) {
  if (!available) return "NO SOURCE";
  return text_width_px("SOURCE LIVE", 2) <= width - 12 ? "SOURCE LIVE" : "LIVE";
}

constexpr Rect inset(Rect r, int n) {
  return {r.x + n, r.y + n, std::max(0, r.w - 2 * n), std::max(0, r.h - 2 * n)};
}

constexpr View make_view(Rect safe) {
  if (safe.w <= 0 || safe.h <= 0) safe = {0, 0, 1280, 720};
  View v{};
  v.bounds = safe;
  if (safe.w == 1280 && safe.h == 720) {
    v.back = {18, 14, 104, 52};
    v.title = {148, 12, 600, 56};
    v.status = {1080, 14, 176, 52};
    for (int i = 0; i < 4; ++i) v.tabs[i] = {20 + i * 312, 650, 304, 56};
    v.live_plot = {22, 96, 1236, 530};
    v.spectrum = {32, 108, 810, 260};
    v.spectrum_data = {33, 139, 808, 228};
    v.waterfall = {32, 372, 810, 242};
    v.waterfall_data = {33, 403, 808, 210};
    v.metrics = {870, 96, 388, 530};
    v.controls = {22, 96, 1236, 530};
    v.control_buttons = {{{700,154,120,48},{832,154,120,48},{700,214,120,48},
                          {832,214,120,48},{700,274,120,48},{832,274,120,48},
                          {700,334,120,48},{832,334,120,48},{700,394,252,48},
                          {700,454,252,48},{700,514,252,48}}};
    v.measurement_buttons = {{{48,214,250,48},{316,214,250,48},
                              {584,214,250,48},{852,214,250,48}}};
    v.message = {360, 14, 620, 52};
    return v;
  }
  const bool landscape = safe.w >= safe.h;
  const int header_h = landscape ? 64 : 72;
  const int footer_h = landscape ? 56 : 72;
  const int margin = landscape ? 12 : 8;
  v.back = {safe.x + margin, safe.y + (header_h - 44) / 2, 72, 44};
  v.status = {safe.x + safe.w - (landscape ? 190 : 136), safe.y + (header_h - 44) / 2,
              landscape ? 178 : 128, 44};
  const int title_x = v.back.x + v.back.w + 12;
  v.title = {title_x, safe.y + 4, std::max(0, v.status.x - title_x - 8), header_h - 8};
  const int tab_gap = 6;
  const int tab_w = (safe.w - 2 * margin - 3 * tab_gap) / 4;
  for (int i = 0; i < 4; ++i)
    v.tabs[i] = {safe.x + margin + i * (tab_w + tab_gap), safe.y + safe.h - footer_h + 4,
                 tab_w, footer_h - 8};
  const int content_y = safe.y + header_h;
  const int content_h = safe.h - header_h - footer_h;
  v.message = {safe.x + margin, content_y, safe.w - 2 * margin, 40};
  const int body_y = content_y + 44;
  const int body_h = content_h - 44;
  v.live_plot = {safe.x + margin, body_y + 4, safe.w - 2 * margin, body_h - 8};
  v.controls = v.live_plot;
  if (landscape) {
    const int gap = 6;
    const int bw = (safe.w - 2 * margin - gap) / 2;
    const int bh = 44;
    const int y0 = body_y;
    const Rect left{safe.x + margin, y0, bw, bh};
    const Rect right{safe.x + margin + bw + gap, y0, bw, bh};
    for (int row = 0; row < 4; ++row) {
      v.control_buttons[row * 2] = {left.x, y0 + row * bh, left.w, bh};
      v.control_buttons[row * 2 + 1] = {right.x, y0 + row * bh, right.w, bh};
    }
    const int action_w = (safe.w - 2 * margin - 2 * gap) / 3;
    for (int i = 8; i < 11; ++i)
      v.control_buttons[i] = {safe.x + margin + (i - 8) * (action_w + gap),
                              y0 + 4 * bh, action_w, bh};
    const int measurement_w = (safe.w - 2 * margin - 3 * gap) / 4;
    for (int i = 0; i < 4; ++i)
      v.measurement_buttons[i] = {safe.x + margin + i * (measurement_w + gap), y0 + bh,
                                  measurement_w, bh};
  } else {
    const int row_h = 68;
    const int y0 = body_y + 12;
    const int gap = 6;
    const int bw = (safe.w - 2 * margin - gap) / 2;
    for (int row = 0; row < 4; ++row) {
      v.control_buttons[row * 2] = {safe.x + margin, y0 + row * row_h + 18, bw, 44};
      v.control_buttons[row * 2 + 1] = {safe.x + margin + bw + gap,
                                       y0 + row * row_h + 18, bw, 44};
    }
    for (int i = 8; i < 11; ++i)
      v.control_buttons[i] = {safe.x + margin, y0 + (i - 4) * row_h + 18,
                              safe.w - 2 * margin, 44};
    for (int i = 0; i < 4; ++i)
      v.measurement_buttons[i] = {safe.x + margin, body_y + 64 + i * 58,
                                  safe.w - 2 * margin, 48};
  }
  if (landscape) {
    const int metrics_w = std::clamp(safe.w / 3, 250, 360);
    const int plot_w = v.live_plot.w - metrics_w - 8;
    v.spectrum = {v.live_plot.x, v.live_plot.y, plot_w, v.live_plot.h * 54 / 100};
    v.spectrum_data = {v.spectrum.x + 1, v.spectrum.y + 30,
                       std::max(0, v.spectrum.w - 2), std::max(0, v.spectrum.h - 31)};
    v.waterfall = {v.live_plot.x, v.spectrum.y + v.spectrum.h + 6, plot_w,
                   v.live_plot.h - v.spectrum.h - 6};
    v.waterfall_data = {v.waterfall.x + 1, v.waterfall.y + 30,
                        std::max(0, v.waterfall.w - 2), std::max(0, v.waterfall.h - 31)};
    v.metrics = {v.live_plot.x + plot_w + 8, v.live_plot.y, metrics_w, v.live_plot.h};
  } else {
    const int plot_h = v.live_plot.h * 68 / 100;
    v.spectrum = {v.live_plot.x, v.live_plot.y, v.live_plot.w, plot_h * 52 / 100};
    v.spectrum_data = {v.spectrum.x + 1, v.spectrum.y + 30,
                       std::max(0, v.spectrum.w - 2), std::max(0, v.spectrum.h - 31)};
    v.waterfall = {v.live_plot.x, v.spectrum.y + v.spectrum.h + 6, v.live_plot.w,
                   plot_h - v.spectrum.h - 6};
    v.waterfall_data = {v.waterfall.x + 1, v.waterfall.y + 30,
                        std::max(0, v.waterfall.w - 2), std::max(0, v.waterfall.h - 31)};
    v.metrics = {v.live_plot.x, v.waterfall.y + v.waterfall.h + 8, v.live_plot.w,
                 v.live_plot.h - plot_h - 8};
  }
  return v;
}

}  // namespace orcsdr::rf_lab::layout
