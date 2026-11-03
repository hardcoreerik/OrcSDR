#include "../ui/airband_dashboard_layout.hpp"

#include <cassert>
#include <cstdio>

using namespace orcsdr::airband::layout;

static void check(Rect v) {
  const Layout l = make_layout(v);
  assert(l.valid);
  assert(inside(v, l.content) && inside(v, l.tabs));
  for (Rect r : l.tab) assert(inside(l.tabs, r) && r.w >= 44 && r.h >= 44);
  for (Rect r : l.control) assert(inside(l.content, r) && r.w >= 44 && r.h >= 44);
  for(int i=0;i<l.scan_row_count;++i)assert(inside(l.content,l.scan_rows[i]));
  for (int i=0;i<l.airport_row_count;++i) assert(inside(l.content,l.airport_rows[i]));
  for (int i=0;i<l.activity_row_count;++i) assert(inside(l.content,l.activity_rows[i]));
  for(int i=0;i<l.setup_visible_count;++i){assert(inside(l.content,l.setup_row[i]));assert(inside(l.content,l.setup_value[i]));assert(l.setup_value[i].w>=44&&l.setup_value[i].h>=44);}
  for(int i=0;i<l.scope_control_count;++i){assert(inside(l.content,l.scope_control[i]));assert(l.scope_control[i].w>=44&&l.scope_control[i].h>=44);}
  assert(inside(l.content,l.scan_toggle)&&l.scan_toggle.w>=44&&l.scan_toggle.h>=44);
  for(Rect r:{l.scan_previous,l.scan_next,l.scan_page,l.airport_previous,l.airport_next,l.airport_page,l.activity_previous,l.activity_next,l.activity_page})
    assert(inside(l.content,r)&&r.w>=44&&r.h>=44);
  assert(inside(l.content,l.detail_overlay)&&inside(l.detail_overlay,l.detail_close));
  if(l.setup_page_count>1)assert(inside(l.content,l.setup_previous)&&inside(l.content,l.setup_next));
  if(l.listen_status.w>0)assert(!overlap(l.listen_now,l.listen_controls)&&!overlap(l.listen_controls,l.listen_status)&&!overlap(l.listen_status,l.listen_gain));
  assert(!overlap(l.scope_plot,l.scope_waterfall));
  assert(l.scope_plot_width == l.scope_plot.w);
  assert(l.scope_plot_height == l.scope_plot.h);
}

int main() {
  const Layout legacy=make_layout({0,0,1280,720});
  assert(legacy.valid && legacy.legacy && legacy.tabs.y==630 && legacy.tabs.h==90);
  assert(legacy.listen_now.x==24 && legacy.listen_now.y==108 && legacy.listen_now.w==520 && legacy.listen_now.h==286);
  assert(legacy.listen_controls.x==556 && legacy.listen_status.y==404 && legacy.listen_gain.y==524);
  check({0,0,800,480});
  check({72,72,424,1088});
  check({72,72,1088,424});
  std::puts("Airband layout geometry PASS");
}
