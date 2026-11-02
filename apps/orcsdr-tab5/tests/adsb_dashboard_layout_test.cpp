#include "../ui/adsb_dashboard_layout.hpp"

#include <cassert>
#include <cstdio>
#include <iterator>

using orcsdr::adsb::layout::Rect;
using orcsdr::adsb::layout::View;
using orcsdr::audio_header::HeaderRect;

static void check(HeaderRect safe) {
  const View v=orcsdr::adsb::layout::make_view(safe);
  assert(v.valid);
  for(const Rect r:v.tabs) {
    assert(orcsdr::adsb::layout::contains(safe,r));
    assert(r.w>=44 && r.h>=44);
  }
  for(size_t i=0;i<v.tabs.size();++i)
    assert(orcsdr::adsb::layout::tab_text_width(orcsdr::adsb::layout::tab_label(i,v.tabs[i].w))
           <=v.tabs[i].w-12);
  for(const Rect r:v.list_items) {
    assert(orcsdr::adsb::layout::inside(v.list_rows,r));
    assert(r.h>=42);
  }
  if(v.legacy) return;
  assert(orcsdr::adsb::layout::inside(v.content,v.radar_map));
  assert(orcsdr::adsb::layout::inside(v.content,v.radar_stats));
  assert(orcsdr::adsb::layout::inside(v.content,v.radar_list));
  assert(orcsdr::adsb::layout::inside(v.content,v.list_rows));
  if(v.list_summary.w>0&&v.list_summary.h>0)
    assert(orcsdr::adsb::layout::inside(v.content,v.list_summary));
  assert(orcsdr::adsb::layout::inside(v.content,v.target_identity));
  assert(orcsdr::adsb::layout::inside(v.content,v.target_metrics));
  for(const Rect r:v.stats_charts)assert(orcsdr::adsb::layout::inside(v.content,r));
  for(const Rect r:v.stats_metrics)assert(orcsdr::adsb::layout::inside(v.content,r));
  for(const Rect r:v.stats_data)assert(orcsdr::adsb::layout::inside(v.content,r));
  assert(orcsdr::adsb::layout::inside(v.content,v.settings_main));
  for(const Rect r:{v.latitude,v.longitude,v.range,v.gain,v.exit,
                    v.keypad_value,v.keypad_cancel,v.keypad_save}) {
    assert(orcsdr::adsb::layout::inside(v.content,r) ||
           (r.x>=v.keypad.x && orcsdr::adsb::layout::inside(v.keypad,r)));
    assert(r.w>=44&&r.h>=44);
  }
  for(const Rect r:v.keypad_keys) {
    assert(orcsdr::adsb::layout::inside(v.keypad,r));
    assert(r.w>=44&&r.h>=44);
  }
  assert(orcsdr::adsb::layout::inside(v.radar_map,v.map_zoom_in));
  assert(orcsdr::adsb::layout::inside(v.radar_map,v.map_zoom_out));
  assert(orcsdr::adsb::layout::inside(v.radar_map,v.radar_map_area));
  assert(orcsdr::adsb::layout::inside(v.target_identity,v.target_lock));
  assert(orcsdr::adsb::layout::inside(v.target_identity,v.target_atc_button));
  assert(orcsdr::adsb::layout::inside(v.target_signal,v.target_map_button));
  if(!v.legacy)for(const Rect r:v.settings_data)assert(orcsdr::adsb::layout::inside(v.content,r));
  const size_t bytes=static_cast<size_t>(v.radar_map.w)*v.radar_map.h*sizeof(uint16_t);
  assert(bytes<=646u*390u*sizeof(uint16_t));
  assert(2*bytes<=2u*646u*390u*sizeof(uint16_t));
}

int main() {
  const View legacy=orcsdr::adsb::layout::make_view({0,0,1280,720});
  assert(legacy.valid&&legacy.legacy);
  assert(legacy.tabs[0].x==0&&legacy.tabs[4].x==1024&&legacy.radar_map.w==646);
  const HeaderRect viewports[]={{0,0,1280,720},{0,0,800,480},
    {72,72,424,1088},{72,72,1088,424}};
  for(const auto viewport:viewports)check(viewport);
  std::puts("adsb_dashboard_layout_test: PASS");
}
