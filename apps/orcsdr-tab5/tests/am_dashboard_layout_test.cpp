#include "../ui/am_dashboard_layout.hpp"
#include "../ui/receiver_band_plan.hpp"

#include <cassert>
#include <cstdio>

using orcsdr::audio_header::HeaderRect;
using orcsdr::am::DashboardLayout;

static bool inside(HeaderRect v, HeaderRect r) {
  return r.x >= v.x && r.y >= v.y && r.w > 0 && r.h > 0 &&
         r.x + r.w <= v.x + v.w && r.y + r.h <= v.y + v.h;
}

static bool overlaps(HeaderRect a, HeaderRect b) {
  return a.x < b.x + b.w && b.x < a.x + a.w &&
         a.y < b.y + b.h && b.y < a.y + a.h;
}

static bool font0_text_fits(const char* value, HeaderRect r, int size) {
  // Font 0 uses a six-pixel cell; size 2 is the minimum responsive label size.
  int chars = 0;
  while (value[chars]) ++chars;
  return chars * 6 * size <= r.w;
}

static void check(HeaderRect viewport) {
  const DashboardLayout l = orcsdr::am::make_layout(viewport);
  assert(l.valid && !l.legacy);
  const HeaderRect required[] = {l.tabs, l.frequency, l.frequency_value, l.signal,
      l.signal_meter, l.signal_text, l.tune_down, l.tune_up, l.step, l.direct,
      l.gain_auto, l.gain_slider, l.preset_save, l.preset_previous, l.preset_page,
      l.preset_next, l.preset_modal, l.modal_cancel, l.modal_replace, l.modal_delete,
      l.finder_panel,l.finder_title,l.finder_scan,l.finder_spacing,l.finder_hint,
      l.finder_discard,l.finder_save,l.scan_prompt,l.spectrum_panel,l.spectrum_title,
      l.spectrum_readout,
      l.spectrum_plot,l.waterfall,l.filter_label,l.filter_slider,l.span_down,l.span_label,
      l.span_up,l.settings_panel,l.settings_title,l.settings_status,l.settings_gain_auto,
      l.settings_gain_slider,l.spectrum_gain_auto,l.spectrum_gain_slider};
  for (const auto r : required) assert(inside(viewport, r));
  for (const auto r : l.preset) assert(inside(viewport, r));
  for (const auto r : l.scan_result) assert(inside(viewport, r));
  for (const auto r : l.scan_prompt_button) assert(inside(viewport, r));
  for (const auto r : l.settings_control) assert(inside(viewport, r));
  assert(l.portrait == (viewport.h > viewport.w));
  assert(l.tabs.y + l.tabs.h == viewport.y + viewport.h);
  assert(l.tune_down.h >= 44 && l.tune_up.h >= 44 && l.step.h >= 44 && l.direct.h >= 44);
  assert(l.frequency_meta.w >= 336);
  assert(!overlaps(l.frequency_label, l.frequency_value));
  assert(!overlaps(l.spectrum_title, l.spectrum_readout));
  assert(!overlaps(l.spectrum_plot, l.waterfall));
  assert(!overlaps(l.waterfall, l.filter_label));
  assert(!overlaps(l.filter_label, l.spectrum_gain_auto));
  assert(!overlaps(l.spectrum_gain_auto, l.span_down));
  assert(!overlaps(l.spectrum_gain_slider, l.span_label));
  assert(font0_text_fits("AM CHANNEL SPECTRUM", l.spectrum_title, 2));
  assert(l.frequency_label.y + l.frequency_label.h <= l.frequency_value.y);
  assert(l.preset_previous.w >= 44 && l.preset_next.w >= 44);
  assert(l.modal_cancel.w >= 44 && l.modal_cancel.h >= 44);
  assert(l.modal_replace.w >= 44 && l.modal_replace.h >= 44);
  assert(l.modal_delete.w >= 44 && l.modal_delete.h >= 44);
  assert(l.span_down.w >= 44 && l.span_down.h >= 44);
  assert(l.span_up.w >= 44 && l.span_up.h >= 44);
  assert(l.settings_gain_auto.w >= 44 && l.settings_gain_auto.h >= 44);
  assert(l.settings_control[6].w >= 180);
  const auto header = orcsdr::audio_header::make_header_layout(viewport);
  assert(l.scan_prompt.y >= header.content_top);
  assert(l.scan_prompt.y + l.scan_prompt.h <= l.tabs.y);
  for (const auto r : l.scan_prompt_button) {
    assert(r.x >= l.scan_prompt.x && r.y >= l.scan_prompt.y);
    assert(r.x + r.w <= l.scan_prompt.x + l.scan_prompt.w);
    assert(r.y + r.h <= l.scan_prompt.y + l.scan_prompt.h);
  }
  assert(l.filter_slider.w >= 200);
  for (int i = 0; i < 4; ++i) {
    const auto tab = orcsdr::am::tab_rect(l, i);
    assert(inside(viewport, tab));
    assert(orcsdr::am::tab_rect(l, i).x == tab.x);
    assert(tab.w >= 96);
  }
  assert(orcsdr::am::tab_rect(l, 0).x == l.tabs.x);
  assert(orcsdr::am::tab_rect(l, 3).x + orcsdr::am::tab_rect(l, 3).w ==
         l.tabs.x + l.tabs.w);
  for (const auto r : l.preset) assert(r.w >= 160 && r.h >= 44);
  if (l.portrait) {
    assert(inside(viewport, l.frequency_label));
    assert(font0_text_fits("FREQUENCY", l.frequency_label, 2));
    assert(font0_text_fits("STEP 1 kHz BW 10 kHz", l.spectrum_readout, 2));
    assert(l.gain_slider.y + l.gain_slider.h <= l.preset[0].y);
    assert(l.preset[5].y + l.preset[5].h + 12 <= l.preset_previous.y);
    assert(l.preset_previous.y + l.preset_previous.h + 8 <= l.preset_save.y);
  } else {
    assert(font0_text_fits("24000 kHz STEP 1 kHz BW 10 kHz SPAN 960 kHz",
                           l.spectrum_readout, 2));
    assert(l.span_up.y + l.span_up.h <= l.tabs.y);
  }
  assert(l.spectrum_gain_auto.y >= l.waterfall.y + l.waterfall.h);
  assert(l.spectrum_gain_slider.y + l.spectrum_gain_slider.h <= l.span_down.y);
}

int main() {
  const auto& am_band = orcsdr::receiver_bands::kAmBroadcast;
  assert(am_band.default_hz == 1000000u);
  assert(am_band.min_hz == 520000u && am_band.max_hz == 1710000u);
  assert(am_band.default_hz >= am_band.min_hz && am_band.default_hz <= am_band.max_hz);
  const DashboardLayout legacy = orcsdr::am::make_layout({0, 0, 1280, 720});
  assert(legacy.valid && legacy.legacy);
  assert(legacy.tabs.y == 630 && legacy.tune_down.x == 24 && legacy.tune_down.y == 398);
  assert(legacy.preset[5].x == 854 && legacy.preset_save.x == 1020);
  check({72, 72, 424, 1088});
  check({72, 72, 1088, 424});
  std::puts("AM dashboard layout tests PASS");
}
