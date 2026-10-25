#include "orcsdr_ui/home_layout.hpp"
#include <algorithm>

namespace orcsdr::ui::home {

int scale_x(int design, int width) {
  // Round half up, in integer arithmetic.
  return static_cast<int>((static_cast<int64_t>(design) * width + kDesignW / 2) / kDesignW);
}
int scale_y(int design, int height) {
  return static_cast<int>((static_cast<int64_t>(design) * height + kDesignH / 2) / kDesignH);
}

Rect scale_rect(Rect d, int width, int height) {
  const int x0 = scale_x(d.x, width), x1 = scale_x(d.x + d.w, width);
  const int y0 = scale_y(d.y, height), y1 = scale_y(d.y + d.h, height);
  return {x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0)};
}
Point scale_point(Point d, int width, int height) { return {scale_x(d.x, width), scale_y(d.y, height)}; }

Rect hit_area(Rect v, int width, int height) {
  Rect r = v;
  if (r.w < kMinTouch) {
    const int grow = kMinTouch - r.w;
    r.x -= grow / 2;
    r.w = kMinTouch;
  }
  if (r.h < kMinTouch) {
    const int grow = kMinTouch - r.h;
    r.y -= grow / 2;
    r.h = kMinTouch;
  }
  r.x = std::clamp(r.x, 0, std::max(0, width - r.w));
  r.y = std::clamp(r.y, 0, std::max(0, height - r.h));
  return r;
}

int cell_for_size(int tab5_size, int width) {
  // Tab5 size n draws n x n pixels per font pixel on a 1280 wide panel. Scale by the width ratio
  // and round to a whole pixel cell, never below 1.
  const int scaled = static_cast<int>((static_cast<int64_t>(tab5_size) * width * 2 + kDesignW) / (2 * kDesignW));
  return std::max(1, scaled);
}

Layout plan(int width, int height) {
  Layout l;
  if (width < 640 || height < 360) return l;
  using namespace design;
  const auto R = [&](int x, int y, int w, int h) { return scale_rect({x, y, w, h}, width, height); };
  const auto P = [&](int x, int y) { return scale_point({x, y}, width, height); };
  l.width = width;
  l.height = height;
  l.outer = R(10, 10, 1260, 700);
  l.badge = R(24, 14, 88, 88);
  l.brand = P(128, 38);
  l.subtitle = P(128, 82);
  l.status_bar = R(kStatusBarX, kStatusBarY, kStatusBarW, kStatusBarH);
  l.battery = R(kRegionX, kRegionY, kRegionW, kRegionH);
  l.home_button = R(kHomeX, kHomeY, kHomeW, kHomeH);
  l.mute_button = R(kMuteX, kMuteY, kMuteW, kMuteH);
  l.visualizer_button = R(kVisualizerX, kVisualizerY, kVisualizerW, kVisualizerH);
  l.settings_button = R(kSettingsX, kSettingsY, kSettingsW, kSettingsH);
  l.rail = R(kRailX, kRailY, kRailW, kRailH);
  l.list = R(kListX, kListY, kListW, kListH);
  l.all_button = R(kListX, kAllY, kListW - 2, 42);
  l.last_used_label = P(34, 132);
  l.last_used_rule = R(132, 132, 110, 1);
  l.row_h = scale_y(kRowH, height);
  l.row_gap = scale_y(kRowH + kRowGap, height) - l.row_h;
  l.visible_rows = kVisibleRows;
  l.main_frame = R(kMainX, kMainY, kMainW, kMainH);
  l.spectrum_label = P(kPlotX, 98);
  l.band_label = P(kPlotX + kPlotW / 2, 98);
  l.spectrum = R(kPlotX, kSpectrumY, kPlotW, kSpectrumH);
  l.waterfall = R(kPlotX, kWaterfallY, kPlotW, kWaterfallH);
  l.frequency = R(kPlotX, kReadoutY, 350, kReadoutH);
  const int contrast_y = kReadoutY + (kReadoutH - kContrastButtonH) / 2;
  l.contrast_down = R(kContrastDownX, contrast_y, kContrastButtonW, kContrastButtonH);
  l.contrast_up = R(kContrastUpX, contrast_y, kContrastButtonW, kContrastButtonH);
  l.palette = R(kPaletteX, kReadoutY, kPaletteW, kReadoutH);
  l.speed = R(kSpeedX, kReadoutY, kSpeedW, kReadoutH);
  l.mode = R(kModeX, kReadoutY, kModeW, kReadoutH);
  const int stepper_x[3] = {kStepperSpan, kStepperTune, kStepperSize};
  for (int i = 0; i < 3; ++i) l.stepper[i] = R(stepper_x[i], kControlY, kStepperW, kControlH);
  l.filter = R(kFilterPanelX, kFilterPanelY, kFilterPanelW, kFilterPanelH);
  l.gain = R(kGainX, kGainY, kGainW, kGainH);
  l.footer = R(24, 654, 1224, 48);
  const int dividers[5] = {170, 384, 560, 700, 856};
  for (int i = 0; i < 5; ++i) l.footer_divider_x[i] = scale_x(dividers[i], width);
  // Footer cells in Tab5 text order: gain (564..696), bias (704..852), receiver (30..166),
  // sample rate (176..380), bandwidth (390..556), level (862..1074).
  l.footer_cell[0] = R(564, 660, 132, 36);
  l.footer_cell[1] = R(704, 660, 148, 36);
  l.footer_cell[2] = R(30, 660, 136, 36);
  l.footer_cell[3] = R(176, 660, 204, 36);
  l.footer_cell[4] = R(390, 660, 166, 36);
  l.footer_cell[5] = R(862, 660, 212, 36);
  l.footer_text_y = scale_y(678, height);
  l.valid = true;
  return l;
}

}  // namespace orcsdr::ui::home
