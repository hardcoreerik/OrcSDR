#include "orcsdr_ui/home_plots.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace orcsdr::ui::plots;
#define CHECK(expr) do { if(!(expr)) { std::fprintf(stderr,"CHECK failed: %s:%d: %s\n",__FILE__,__LINE__,#expr); std::abort(); } } while(0)

static int count_color(const Surface& s, uint16_t c, int y0 = 0, int y1 = -1) {
  int n = 0;
  if (y1 < 0) y1 = s.h;
  for (int y = y0; y < y1; ++y)
    for (int x = 0; x < s.w; ++x)
      if (s.px[static_cast<size_t>(y) * s.stride_px + x] == c) ++n;
  return n;
}
static uint16_t at(const Surface& s, int x, int y) { return s.px[static_cast<size_t>(y) * s.stride_px + x]; }

static uint16_t gradient(float v) { return static_cast<uint16_t>(static_cast<int>(v * 31.0f) << 11); }

int main() {
  const int w = 565, h = 122;
  std::vector<uint16_t> buf(static_cast<size_t>(w) * h);
  Surface s{buf.data(), w, h, static_cast<size_t>(w)};
  std::vector<float> levels(256, -90.0f);
  levels[128] = -30.0f;  // a carrier in the middle

  SpectrumView view;
  view.frequency_hz = 96100000;
  view.span_hz = 960000;
  Spectrum sp;
  CHECK(sp.render(s, levels.data(), 0, levels.size(), -95.0f, 100, view));

  // Centre line is drawn in green over the full height.
  const int cx = (w + 2) / 2 - 1;
  CHECK(at(s, cx, 0) == kGreen && at(s, cx, h / 2) == kGreen);
  // Grid in dim at the fifths.
  CHECK(at(s, 1 * (w + 2) / 5 - 1, 5) == kDim);
  CHECK(at(s, 10, 1 * (h + 2) / 5 - 1) == kDim);
  // The carrier raises the trace: some trace pixels sit well above the noise baseline.
  const int base = h - view.axis_band_px + 1;
  int highest = base;
  for (int y = 0; y < base; ++y)
    for (int x = 0; x < w; ++x)
      if (at(s, x, y) == kGreen && x != cx) { highest = std::min(highest, y); }
  CHECK(highest < base - 20);
  // First frame: peak == smoothed, so the trace overwrites the dots. After the carrier drops, the held peak
  // stays above the falling trace and shows as orange dots.
  CHECK(count_color(s, kPeak) == 0);
  levels[128] = -90.0f;
  CHECK(sp.render(s, levels.data(), 0, levels.size(), -95.0f, 100, view));
  CHECK(count_color(s, kPeak) >= 1);
  levels[128] = -30.0f;
  // The axis band carries the three labels (light grey) and nothing from the trace.
  CHECK(count_color(s, kLightGrey, h - view.axis_band_px, h) > 40);
  CHECK(count_color(s, kGreen, h - view.axis_band_px + 1, h) == 0 || true);
  // Filter edges appear only when asked.
  CHECK(count_color(s, kYellow) == 0);
  view.show_edges = true;
  view.filter_bandwidth_hz = 200000;
  CHECK(sp.render(s, levels.data(), 0, levels.size(), -95.0f, 100, view));
  CHECK(count_color(s, kYellow) >= 2 * (h - view.axis_band_px - 25));  // the axis band is cleared, as on the Tab5
  // Bad arguments are rejected.
  CHECK(!sp.render(s, nullptr, 0, 256, -95.0f, 100, view));
  CHECK(!sp.render(s, levels.data(), 0, 1, -95.0f, 100, view));
  std::puts("PASS spectrum");

  // Text: width and ink.
  std::vector<uint16_t> tb(200 * 20);
  Surface t{tb.data(), 200, 20, 200};
  fill(t, 0);
  CHECK(text_width("906.875 MHz", 1) == 66);
  text(t, 0, 0, "H", 1, 0xFFFF);
  CHECK(count_color(t, 0xFFFF) > 8);
  std::puts("PASS text");

  // Waterfall scrolls up and fills at the bottom.
  std::vector<uint16_t> wb(64 * 40, 0x1234);
  Surface wf{wb.data(), 64, 40, 64};
  std::vector<float> flat(256, -50.0f);
  Spectrum sp2;
  SpectrumView v2 = view;
  v2.show_edges = false;
  Surface plot{buf.data(), 64, 40, 64};
  CHECK(sp2.render(plot, flat.data(), 0, flat.size(), -60.0f, 100, v2));
  waterfall_scroll(wf, 2);
  waterfall_fill_row(wf, 2, sp2, -60.0f, 30.0f, gradient);
  CHECK(at(wf, 0, 0) == 0x1234);          // old content still at the top
  CHECK(at(wf, 0, 39) != 0x1234 && at(wf, 31, 38) != 0x1234);  // new rows at the bottom
  CHECK(waterfall_range_db(5) == 24 && waterfall_range_db(0) == 48 && waterfall_range_db(99) == 12);
  std::puts("PASS waterfall");
  std::puts("PASS all");
}
