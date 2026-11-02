#include "../ui/cb_dashboard_layout.hpp"
#include <cassert>
#include <cstdio>
using orcsdr::audio_header::HeaderRect;
using orcsdr::cb::DashboardLayout;
static bool inside(HeaderRect o, HeaderRect r) {
  return r.w > 0 && r.h > 0 && r.x >= o.x && r.y >= o.y &&
      r.x + r.w <= o.x + o.w && r.y + r.h <= o.y + o.h;
}
static bool overlap(HeaderRect a, HeaderRect b) {
  return a.x < b.x+b.w && b.x < a.x+a.w && a.y < b.y+b.h && b.y < a.y+a.h;
}
static bool text_fits(const char* s, HeaderRect r, int size) {
  int n=0; while(s[n]) ++n; return n*6*size <= r.w;
}
static void check(HeaderRect v) {
  const auto l=orcsdr::cb::make_layout(v);
  assert(l.valid && !l.legacy);
  assert(inside(v,l.content) && inside(v,l.tabs) && inside(v,l.listen_now));
  assert(inside(v,l.spectrum_plot) && inside(v,l.waterfall) && inside(v,l.spectrum_axis));
  assert(!overlap(l.spectrum_plot,l.waterfall));
  assert(!overlap(l.spectrum_axis,l.waterfall));
  assert(l.spectrum_plot.w<=1232 && l.spectrum_plot.w>4);
  assert(l.waterfall.w==l.spectrum_plot.w);
  for(const auto cell:l.channel_cell) {
    assert(inside(v,cell) && cell.h>=44 && cell.w>=44);
    assert(inside(l.content,cell));
    assert(!overlap(cell,l.tabs));
    assert(text_fits("65535",cell,2));
    assert(text_fits("27.405",cell,2));
    assert(text_fits("LOCKED",cell,2));
  }
  for(int i=0;i<8;++i) {
    assert(inside(v,l.setup_row[i]));
    assert(!overlap(l.setup_row[i],l.setup_previous));
    assert(!overlap(l.setup_help[i],l.setup_value[i]));
    assert(l.setup_value[i].h>=44 && text_fits("UNLIMITED",l.setup_value[i],2));
  }

  assert(l.activity_row_count>0 && l.activity_row_count<=7);
  for(int i=0;i<l.activity_row_count;++i) {
    assert(inside(v,l.activity_row[i]) && l.activity_row[i].h>=44);
    assert(!overlap(l.activity_summary,l.activity_row[i]));
    if(i) assert(!overlap(l.activity_row[i-1],l.activity_row[i]));
  }


  for (auto r:l.tab) assert(inside(v,r) && r.w>=44 && r.h>=44);
  for (auto r:l.control) assert(inside(v,r) && r.w>=44 && r.h>=44);
  for (auto r:l.listen_channel) assert(inside(v,r) && r.w>=44 && r.h>=44);
  assert(text_fits("LISTEN",l.tab[0],2));
  assert(text_fits("SPECTRUM",l.tab[1],2));
  assert(text_fits("CHANNELS",l.tab[3],2));
  assert(!overlap(l.listen_now,l.listen_controls));
  assert(!overlap(l.listen_controls,l.listen_channels));
  assert(l.listen_meter.y>=l.listen_now.y &&
      l.listen_meter.y+l.listen_meter.h<=l.listen_now.y+l.listen_now.h);
  if(l.portrait) {
    assert(l.listen_channel_columns==5);
    assert(l.tab[0].h>=44 && l.tab[4].h>=44);
    assert(l.listen_channel[0].y>=l.listen_channel_title.y+l.listen_channel_title.h);
  } else {
    assert(l.listen_channel_columns==20);
    assert(l.listen_channel[0].y>=l.listen_channel_title.y+l.listen_channel_title.h);
  }
}
int main() {
  const auto legacy=orcsdr::cb::make_layout({0,0,1280,720});
  assert(legacy.valid && legacy.legacy && legacy.tabs.y==630);
  check({72,72,424,1088});
  check({72,72,1088,424});
  std::puts("CB responsive layout tests PASS");
}
