#include "../ui/rf_lab_layout.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <iterator>

using orcsdr::rf_lab::layout::Rect;
using orcsdr::rf_lab::layout::View;

static bool contains(Rect outer, Rect inner) {
  return inner.w > 0 && inner.h > 0 && inner.x >= outer.x && inner.y >= outer.y &&
         inner.x + inner.w <= outer.x + outer.w && inner.y + inner.h <= outer.y + outer.h;
}

static bool overlaps(Rect a, Rect b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static void check(Rect safe) {
  const View v = orcsdr::rf_lab::layout::make_view(safe);
  assert(v.bounds.x == safe.x && v.bounds.y == safe.y);
  for (size_t i = 0; i < v.tabs.size(); ++i) {
    const Rect r = v.tabs[i];
    const char* label = orcsdr::rf_lab::layout::tab_label(i, r.w);
    assert(contains(safe, r) && r.w >= 44 && r.h >= 44);
    assert(orcsdr::rf_lab::layout::text_width_px(label, 2) <= r.w - 12);
  }
  constexpr const char* control_labels[] = {"-100 kHz", "+100 kHz", "NEXT RATE", "NEXT BW",
      "-1 PPM", "+1 PPM", "AUTO / MANUAL", "+ GAIN", "AGC TOGGLE", "BIAS ON / OFF", "KEEP / RESTORE"};
  for (size_t i = 0; i < v.control_buttons.size(); ++i) {
    const Rect r = v.control_buttons[i];
    assert(contains(safe, r) && r.w >= 44 && r.h >= 44);
    if (safe.w != 1280 || safe.h != 720)
      assert(orcsdr::rf_lab::layout::text_width_px(control_labels[i], 2) <= r.w - 12);
  }
  constexpr const char* measurement_labels[] = {"SNAPSHOT", "RUN 10 SEC", "ADD MARKER", "STOP"};
  for (size_t i = 0; i < v.measurement_buttons.size(); ++i) {
    const Rect r = v.measurement_buttons[i];
    assert(contains(safe, r) && r.w >= 44 && r.h >= 44);
    assert(orcsdr::rf_lab::layout::text_width_px(measurement_labels[i], 2) <= r.w - 12);
  }
  assert(contains(safe, v.back) && contains(safe, v.title) && contains(safe, v.status));
  const int title_size = safe.w == 1280 && safe.h == 720 ? 4 : 3;
  assert(v.title.x >= v.back.x + v.back.w);
  assert(v.title.x + orcsdr::rf_lab::layout::text_width_px("RF LAB", title_size) <= v.status.x);
  assert(orcsdr::rf_lab::layout::text_width_px(
      orcsdr::rf_lab::layout::source_label(true, v.status.w), 2) <= v.status.w - 12);
  for (const char* status : {"LIVE", "NO SOURCE", "BIAS ON"})
    assert(orcsdr::rf_lab::layout::text_width_px(status, 2) <= v.status.w - 12);
  assert(contains(safe, v.message) && !overlaps(v.message, v.live_plot));
  assert(!overlaps(v.message, v.spectrum) && !overlaps(v.message, v.waterfall) &&
         !overlaps(v.message, v.metrics));
  for (const Rect r : v.control_buttons) assert(!overlaps(v.message, r));
  for (const Rect r : v.measurement_buttons) assert(!overlaps(v.message, r));
  assert(contains(safe, v.spectrum) && contains(safe, v.waterfall) && contains(safe, v.metrics));
  assert(contains(v.spectrum, v.spectrum_data) && contains(v.waterfall, v.waterfall_data));
  assert(v.spectrum_data.y >= v.spectrum.y + 30);
  assert(v.waterfall_data.y >= v.waterfall.y + 30);
  assert(v.spectrum_data.h > 0 && v.waterfall_data.h > 0);
  assert(v.spectrum_data.x + v.spectrum_data.w <= v.spectrum.x + v.spectrum.w - 1);
  assert(v.waterfall_data.x + v.waterfall_data.w <= v.waterfall.x + v.waterfall.w - 1);
  const Rect waterfall = v.waterfall_data;
  const size_t bytes = static_cast<size_t>(waterfall.w) * waterfall.h * sizeof(uint16_t);
  assert(bytes <= 400u * 1024u);
}

int main() {
  const View legacy = orcsdr::rf_lab::layout::make_view({0, 0, 1280, 720});
  assert(legacy.back.x == 18 && legacy.back.y == 14 && legacy.tabs[0].y == 650);
  assert(legacy.control_buttons[0].x == 700 && legacy.measurement_buttons[3].x == 852);
  const Rect viewports[] = {{0, 0, 1280, 720}, {0, 0, 800, 480},
                            {72, 72, 424, 1088}, {72, 72, 1088, 424}};
  size_t canvas_bytes[std::size(viewports)]{};
  for (size_t i = 0; i < std::size(viewports); ++i) {
    const Rect viewport = viewports[i];
    check(viewport);
    const auto view = orcsdr::rf_lab::layout::make_view(viewport);
    const Rect waterfall = view.waterfall_data;
    canvas_bytes[i] =
        static_cast<size_t>(waterfall.w) * waterfall.h * sizeof(uint16_t);
  }
  // Transactional resize holds old and candidate canvases until allocation succeeds.
  size_t largest_transition = 0;
  for (size_t i = 0; i < std::size(viewports); ++i)
    for (size_t j = 0; j < std::size(viewports); ++j)
      if (viewports[i].w != viewports[j].w || viewports[i].h != viewports[j].h)
        largest_transition = std::max(largest_transition, canvas_bytes[i] + canvas_bytes[j]);
  assert(largest_transition <= 640u * 1024u);
  std::puts("rf_lab_layout_test: PASS");
}
