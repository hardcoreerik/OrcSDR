#include "../ui/rf24_dashboard_layout.hpp"

#include <cassert>
#include <cstdio>
#include <iterator>

using orcsdr::audio_header::HeaderRect;
using orcsdr::rf24::layout::Rect;
using orcsdr::rf24::layout::View;

static void check(HeaderRect viewport) {
  const View v = orcsdr::rf24::layout::make_view(viewport);
  assert(v.valid);
  for (const Rect tab : v.tabs) {
    assert(orcsdr::rf24::layout::inside({viewport.x, viewport.y, viewport.w, viewport.h}, tab));
    assert(tab.w >= 44 && tab.h >= 44);
  }
  for (size_t i = 0; i < v.tabs.size(); ++i) {
    const char* label = orcsdr::rf24::layout::tab_label(i, v.tabs[i].w);
    assert(orcsdr::rf24::layout::text_width_px(label, 2) <= v.tabs[i].w - 12);
  }
  assert(orcsdr::rf24::layout::inside({viewport.x, viewport.y, viewport.w, viewport.h}, v.rescan));
  assert(orcsdr::rf24::layout::inside(v.page, v.overview_summary));
  assert(orcsdr::rf24::layout::inside(v.page, v.overview_channels));
  assert(orcsdr::rf24::layout::inside(v.page, v.overview_table));
  assert(orcsdr::rf24::layout::inside(v.page, v.channel_plot));
  assert(orcsdr::rf24::layout::inside(v.page, v.channel_overlap));
  assert(orcsdr::rf24::layout::inside(v.page, v.devices_data));
  assert(orcsdr::rf24::layout::inside(v.page, v.settings_button));
  assert(!orcsdr::rf24::layout::overlaps(v.overview_summary, v.overview_channels));
  assert(!orcsdr::rf24::layout::overlaps(v.overview_summary, v.overview_table));
  assert(!orcsdr::rf24::layout::overlaps(v.overview_channels, v.overview_table));
  assert(!orcsdr::rf24::layout::overlaps(v.channel_plot, v.channel_overlap));
  assert(!orcsdr::rf24::layout::overlaps(v.scan_band, v.page));
}

int main() {
  const View legacy = orcsdr::rf24::layout::make_view({0, 0, 1280, 720});
  assert(legacy.valid && legacy.legacy);
  assert(legacy.tabs[0].x == 20 && legacy.tabs[4].x == 1012 && legacy.tabs[4].y == 648);
  assert(legacy.rescan.x == 830 && legacy.settings_button.x == 930);
  check({0, 0, 1280, 720});
  check({0, 0, 800, 480});
  check({72, 72, 424, 1088});
  check({72, 72, 1088, 424});
  std::puts("rf24_dashboard_layout_test: PASS");
}
