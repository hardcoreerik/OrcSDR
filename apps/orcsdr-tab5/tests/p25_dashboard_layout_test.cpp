#include "../ui/p25_dashboard_layout.hpp"

#include <cassert>
#include <cstdio>

using orcsdr::p25::layout::Layout;
using orcsdr::p25::layout::Rect;
using orcsdr::p25::layout::make_layout;

static void in_content(const Layout& l, Rect r) {
  if (!orcsdr::p25::layout::inside(l.content, r)) {
    std::fprintf(stderr, "outside %dx%d at %d,%d: rect=%d,%d %dx%d content=%d,%d %dx%d\n",
                 l.viewport.w, l.viewport.h, l.viewport.x, l.viewport.y,
                 r.x, r.y, r.w, r.h, l.content.x, l.content.y, l.content.w, l.content.h);
    std::abort();
  }
}

static void check(Rect viewport) {
  const Layout l = make_layout(viewport);
  assert(l.valid);
  assert(orcsdr::p25::layout::inside(viewport, l.content));
  assert(orcsdr::p25::layout::inside(viewport, l.tabs));
  for (const Rect r : l.tab) {
    assert(orcsdr::p25::layout::inside(l.tabs, r));
    assert(r.w >= 44 && r.h >= 44);
  }
  if (l.legacy) {
    for (const Rect r : {l.monitor_control, l.monitor_profile, l.monitor_grant,
                         l.monitor_signal, l.spectrum_plot, l.waterfall,
                         l.spectrum_span, l.spectrum_frequency_label,
                         l.spectrum_filter_label, l.spectrum_activity_label,
                         l.talk_summary_value, l.talk_current_value,
                         l.program_profiles, l.program_details, l.profile_page,
                         l.program_profiles_label, l.program_details_label,
                         l.program_detail_name, l.program_detail_identity,
                         l.program_detail_status}) in_content(l, r);
    for (const Rect r : l.monitor_actions) in_content(l, r);
    for (int i = 0; i < l.talk_row_count; ++i) in_content(l, l.talk_rows[i]);
    for (int i = 0; i < l.profile_row_count; ++i) in_content(l, l.profile_rows[i]);
    for (int i = 0; i < l.candidate_row_count; ++i) in_content(l, l.program_candidates[i]);
    for (const Rect r : l.program_controls) in_content(l, r);
    for (const Rect r : l.policy_controls) in_content(l, r);
    for (const Rect r : l.health_rows) in_content(l, r);
    for (int i = 0; i < l.candidate_row_count; ++i)
      assert(!orcsdr::p25::layout::overlap(l.program_candidates[i], l.policy_controls[0]));
    assert(!orcsdr::p25::layout::overlap(l.program_detail_status, l.program_candidates[0]));
    assert(!orcsdr::p25::layout::overlap(l.profile_page, l.profile_rows[0]));
    return;
  }
  for (const Rect r : {l.monitor_control, l.monitor_profile, l.monitor_grant,
                       l.monitor_signal, l.spectrum_plot, l.waterfall,
                       l.talk_summary, l.talk_current, l.program_profiles,
                       l.program_details}) in_content(l, r);
  for (const Rect r : l.monitor_actions) {
    in_content(l, r);
    assert(r.w >= 44 && r.h >= 44);
  }
  for (int i = 0; i < l.talk_row_count; ++i) {
    in_content(l, l.talk_rows[i]);
    assert(l.talk_rows[i].h >= 44);
  }
  for (int i = 0; i < l.profile_row_count; ++i) in_content(l, l.profile_rows[i]);
  in_content(l, l.profile_previous);
  in_content(l, l.profile_next);
  in_content(l, l.profile_page);
  in_content(l, l.program_profiles_tab);
  in_content(l, l.program_details_tab);
  in_content(l, l.program_candidate_previous);
  in_content(l, l.program_candidate_next);
  in_content(l, l.program_candidate_page);
  for (int i = 0; i < l.candidate_row_count; ++i) in_content(l, l.program_candidates[i]);
  for (const Rect r : l.program_controls) {
    in_content(l, r);
    assert(r.w >= 44 && r.h >= 44);
  }
  for (const Rect r : l.policy_controls) {
    in_content(l, r);
    assert(r.w >= 44 && r.h >= 44);
  }
  for (const Rect r : l.health_summary) in_content(l, r);
  for (int i = 0; i < l.health_row_count; ++i) in_content(l, l.health_rows[i]);
  for (const Rect r : l.health_footer) in_content(l, r);
  assert(!orcsdr::p25::layout::overlap(l.monitor_signal, l.monitor_actions[0]));
  assert(!orcsdr::p25::layout::overlap(l.talk_rows[l.talk_row_count - 1], l.talk_previous));
  assert(!orcsdr::p25::layout::overlap(l.program_profiles_tab, l.profile_rows[0]));
  assert(!orcsdr::p25::layout::overlap(l.program_candidate_page, l.policy_controls[0]));
  for (int i = 0; i < l.profile_row_count; ++i) {
    assert(!orcsdr::p25::layout::overlap(l.profile_rows[i], l.profile_previous));
    assert(!orcsdr::p25::layout::overlap(l.profile_rows[i], l.profile_next));
  }
  for (int i = 0; i < l.candidate_row_count; ++i) {
    assert(!orcsdr::p25::layout::overlap(l.program_candidates[i], l.program_candidate_previous));
    assert(!orcsdr::p25::layout::overlap(l.program_candidates[i], l.program_candidate_next));
  }
  assert(!orcsdr::p25::layout::overlap(l.program_candidate_page, l.policy_controls[1]));
  assert(!orcsdr::p25::layout::overlap(l.spectrum_plot, l.waterfall));
  assert(!orcsdr::p25::layout::overlap(l.waterfall, l.span_down));
  assert(!orcsdr::p25::layout::overlap(l.waterfall, l.span_up));
  for (int i = 0; i < l.health_row_count; ++i)
    for (const Rect footer : l.health_footer)
      assert(!orcsdr::p25::layout::overlap(l.health_rows[i], footer));
}

int main() {
  const Layout legacy = make_layout({0, 0, 1280, 720});
  assert(legacy.valid && legacy.legacy && legacy.tabs.y == 630 &&
         legacy.content.y == 132 && legacy.tab[4].x == 1024);
  check({0, 0, 800, 480});
  check({72, 72, 424, 1088});
  check({72, 72, 1088, 424});
  std::puts("P25 responsive layout geometry tests PASS");
}
