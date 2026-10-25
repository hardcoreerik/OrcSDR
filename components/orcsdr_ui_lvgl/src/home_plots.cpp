#include "orcsdr_ui/home_plots.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "spectrum_resample.hpp"  // apps/orcsdr-tab5/ui, hardware independent

#ifndef PROGMEM
#define PROGMEM
#endif
#include "glcdfont.h"

namespace orcsdr::ui::plots {
namespace {
constexpr float kSmoothKeep = 0.6f;      // share of the previous value kept per 100 ms
constexpr float kPeakFallDb = 0.4f;      // peak-hold dots fall this much per 100 ms
constexpr float kHeadroomDb = 5.0f;
constexpr float kMinRangeDb = 30.0f;
constexpr float kFloorMarginDb = 4.0f;

float home_floor(const float* smooth, int count, float pipeline_floor) {
  float sum = 0.0f;
  for (int i = 0; i < count; ++i) sum += smooth[i];
  return std::max(pipeline_floor, sum / static_cast<float>(count) - 4.0f);
}

void format_frequency(char* out, size_t size, uint32_t hz) {
  if (hz >= 1000000u) std::snprintf(out, size, "%.3f MHz", hz / 1000000.0);
  else std::snprintf(out, size, "%.1f kHz", hz / 1000.0);
}
}  // namespace

void fill(Surface s, uint16_t color) {
  for (int y = 0; y < s.h; ++y) std::fill_n(s.px + static_cast<size_t>(y) * s.stride_px, s.w, color);
}
void set_pixel(Surface s, int x, int y, uint16_t color) {
  if (x < 0 || y < 0 || x >= s.w || y >= s.h) return;
  s.px[static_cast<size_t>(y) * s.stride_px + x] = color;
}
void hline(Surface s, int x, int y, int w, uint16_t color) {
  for (int i = 0; i < w; ++i) set_pixel(s, x + i, y, color);
}
void vline(Surface s, int x, int y, int h, uint16_t color) {
  for (int i = 0; i < h; ++i) set_pixel(s, x, y + i, color);
}
void line(Surface s, int x0, int y0, int x1, int y1, uint16_t color) {
  const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
  const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    set_pixel(s, x0, y0, color);
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

int text_width(const char* t, int cell) { return static_cast<int>(std::strlen(t)) * 6 * cell; }

void text(Surface s, int x, int y, const char* t, int cell, uint16_t color) {
  for (; *t; ++t, x += 6 * cell) {
    const unsigned char ch = static_cast<unsigned char>(*t);
    for (int col = 0; col < 5; ++col) {
      const uint8_t bits = font[ch * 5 + col];
      for (int row = 0; row < 8; ++row) {
        if (!((bits >> row) & 1)) continue;
        for (int dy = 0; dy < cell; ++dy)
          for (int dx = 0; dx < cell; ++dx) set_pixel(s, x + col * cell + dx, y + row * cell + dy, color);
      }
    }
  }
}

bool Spectrum::render(Surface out, const float* levels, size_t first_bin, size_t visible_bins, float floor,
                      uint32_t elapsed_ms, const SpectrumView& view) {
  const int w = out.w, h = out.h;
  if (!out.px || !levels || visible_bins < 2 || w < 8 || h < 24 || w > kMaxPixels) return false;
  const uint32_t interval_ms = std::min<uint32_t>(elapsed_ms ? elapsed_ms : 100, 333);
  const bool same_view = valid_ && last_frequency_ == view.frequency_hz && last_span_ == view.span_hz &&
                         last_first_bin_ == first_bin && last_visible_ == visible_bins;
  valid_ = true;
  last_frequency_ = view.frequency_hz;
  last_span_ = view.span_hz;
  last_first_bin_ = first_bin;
  last_visible_ = visible_bins;
  const float keep = std::pow(kSmoothKeep, interval_ms / 100.0f);
  for (int x = 0; x < w; ++x) {
    const float level = spectrum::peak_for_pixel(levels, first_bin, visible_bins, static_cast<size_t>(x), static_cast<size_t>(w));
    smooth_[x] = same_view ? keep * smooth_[x] + (1.0f - keep) * level : level;
    peak_[x] = same_view ? std::max(level, peak_[x] - kPeakFallDb * interval_ms / 100.0f) : level;
  }
  floor = home_floor(smooth_, w, floor);
  last_floor_ = floor;

  fill(out, kBlack);
  // Grid: four lines each way, at fifths of the plot.
  for (int i = 1; i < 5; ++i) {
    hline(out, 0, i * (h + 2) / 5 - 1, w, kDim);
    vline(out, i * (w + 2) / 5 - 1, 0, h, kDim);
  }
  floor -= kFloorMarginDb;
  float strongest = floor;
  for (int x = 0; x < w; ++x) strongest = std::max(strongest, smooth_[x]);
  ceiling_ = (!ceiling_valid_ || strongest > ceiling_)
                 ? strongest
                 : ceiling_ + (strongest - ceiling_) * (1.0f - std::pow(0.96f, interval_ms / 100.0f));
  ceiling_valid_ = true;
  const float range_db = std::max(kMinRangeDb, ceiling_ + kHeadroomDb - floor);
  const int trace_base = h - view.axis_band_px + 1;
  const int trace_height = (h + 2) - view.axis_band_px - 4;
  int px = 0, py = trace_base;
  for (int x = 0; x < w; ++x) {
    const float normalized = std::clamp((smooth_[x] - floor) / range_db, 0.0f, 1.0f);
    const float held = std::clamp((peak_[x] - floor) / range_db, 0.0f, 1.0f);
    const int y = trace_base - static_cast<int>(normalized * trace_height);
    set_pixel(out, x, trace_base - static_cast<int>(held * trace_height), kPeak);
    if (x) line(out, px, py, x, y, kGreen);
    px = x;
    py = y;
  }
  vline(out, (w + 2) / 2 - 1, 0, h, kGreen);
  if (view.show_edges && view.filter_bandwidth_hz > 0 && view.span_hz > 0) {
    const int half = std::clamp<int>(
        static_cast<int>((static_cast<int64_t>(view.filter_bandwidth_hz) * (w + 2)) / (2 * static_cast<int64_t>(view.span_hz))),
        2, (w + 2) / 2 - 2);
    vline(out, (w + 2) / 2 - half - 1, 0, h, kYellow);
    vline(out, (w + 2) / 2 + half - 1, 0, h, kYellow);
  }
  // Frequency axis along the bottom: low edge, centre, high edge.
  const uint32_t half_span = view.span_hz / 2u;
  const uint32_t low = view.frequency_hz > half_span ? view.frequency_hz - half_span : 0u;
  const uint32_t high = view.frequency_hz + half_span;
  char lo[20], mid[20], hi[20];
  format_frequency(lo, sizeof(lo), low);
  format_frequency(mid, sizeof(mid), view.frequency_hz);
  format_frequency(hi, sizeof(hi), high);
  const int cell = view.axis_cell;
  const int band_top = h - view.axis_band_px + (view.axis_band_px - 8 * cell) / 2 - 1;
  for (int y = h - view.axis_band_px + 1; y < h; ++y) hline(out, 0, y, w, kBlack);
  text(out, 4, band_top, lo, cell, kLightGrey);
  text(out, (w - text_width(mid, cell)) / 2, band_top, mid, cell, kLightGrey);
  text(out, w - 4 - text_width(hi, cell), band_top, hi, cell, kLightGrey);
  return true;
}

void waterfall_scroll(Surface s, int rows) {
  if (rows <= 0 || rows >= s.h) return;
  // Content moves up; the newest rows enter at the bottom (as M5GFX scroll(0, -rows) does).
  std::memmove(s.px, s.px + static_cast<size_t>(rows) * s.stride_px,
               static_cast<size_t>(s.h - rows) * s.stride_px * sizeof(uint16_t));
}

void waterfall_fill_row(Surface s, int rows, const Spectrum& spectrum, float floor_db, float range_db, ColorFn color) {
  if (rows <= 0) return;
  rows = std::min(rows, s.h);
  for (int x0 = 0; x0 < s.w; x0 += 2) {
    const int width = std::min(2, s.w - x0);
    const float level = width > 1 ? std::max(spectrum.smoothed(x0), spectrum.smoothed(x0 + 1)) : spectrum.smoothed(x0);
    const float normalized = std::clamp((level - floor_db) / range_db, 0.0f, 1.0f);
    const uint16_t c = color(normalized);
    for (int y = s.h - rows; y < s.h; ++y)
      for (int dx = 0; dx < width; ++dx) s.px[static_cast<size_t>(y) * s.stride_px + x0 + dx] = c;
  }
}

}  // namespace orcsdr::ui::plots
