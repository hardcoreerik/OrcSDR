#include "../ui/settings_layout.hpp"
#include <cassert>
#include <cstdio>
using namespace orcsdr::settings;
int main() {
  const auto legacy=make_settings_layout({0,0,1280,720});
  assert(legacy.valid && legacy.legacy);
  assert(legacy.row(0).minus.x==930 && legacy.row(0).minus.y==144);
  assert(legacy.row(4).action.y==544);
  const Rect views[]={{72,72,424,1088},{72,72,1088,424},{0,0,800,480},{0,0,720,1280}};
  for (auto v:views) {
    const auto l=make_settings_layout(v);
    assert(l.valid && !l.legacy && l.rows_per_page>0);
    assert(l.content.y>=l.navigation.y+l.navigation.h+20);
    assert(orcsdr::audio_header::header_inside(l.content,l.firmware_action()));
    assert(orcsdr::audio_header::header_inside(l.content,l.firmware_status()));
    assert(orcsdr::audio_header::header_inside(l.content,l.firmware_help()));
    assert(l.firmware_action().h>=44);
    assert(settings_page_count(6,l)*l.rows_per_page>=6);
    for (std::size_t i=0;i<l.rows_per_page;++i) {
      const auto r=l.row(i);
      assert(orcsdr::audio_header::header_inside(l.content,r.bounds));
      assert(orcsdr::audio_header::header_inside(r.bounds,r.minus));
      assert(orcsdr::audio_header::header_inside(r.bounds,r.plus));
      for (auto angle:r.angles) {
        assert(orcsdr::audio_header::header_inside(r.bounds,angle));
        assert(angle.w>=44 && angle.h>=44);
      }
      assert(r.minus.w>=44 && r.plus.h>=44);
    }
  }
  std::puts("Settings shared geometry tests PASS");
}
