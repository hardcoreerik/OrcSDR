#include "../ui/home_layout.hpp"

#include <cassert>
#include <cstdio>

using orcsdr::audio_header::HeaderRect;
using orcsdr::home::Layout;

static bool inside(HeaderRect v, HeaderRect r) {
  return r.x >= v.x && r.y >= v.y && r.w > 0 && r.h > 0 &&
         r.x + r.w <= v.x + v.w && r.y + r.h <= v.y + v.h;
}

static void check(int w, int h) {
  const HeaderRect v{0, 0, w, h};
  const auto header = orcsdr::audio_header::make_header_layout(v);
  const Layout l = orcsdr::home::make_layout(v, header.content_top);
  assert(header.valid && l.valid);
  assert(l.portrait == (h > w));
  const HeaderRect required[] = {l.main,l.rail,l.spectrum,l.waterfall,l.frequency,l.contrast,
    l.palette,l.speed,l.mode,l.span,l.tune,l.step,l.filter,l.gain,l.recent,l.all,l.footer,l.popup};
  for (const auto r : required) assert(inside(v,r));
  const HeaderRect controls[] = {l.frequency,l.contrast,l.palette,l.speed,l.mode,l.span,l.tune,
    l.step,l.filter,l.gain,l.all};
  for (const auto r : controls) assert(r.w >= 44 && r.h >= 44);
  assert(l.spectrum.w >= 500 || l.portrait);
}

static void check_viewport(HeaderRect v) {
  const auto header=orcsdr::audio_header::make_header_layout(v);
  const Layout l=orcsdr::home::make_layout(v,header.content_top+28);
  assert(header.valid && l.valid);
  assert(inside(v,l.spectrum) && inside(v,l.waterfall) && inside(v,l.footer));
  assert(l.spectrum.y>=header.content_top+28);
}

int main() {
  const auto legacy = orcsdr::home::make_layout({0,0,1280,720},112);
  assert(legacy.valid && legacy.legacy);
  assert(legacy.spectrum.x==330 && legacy.spectrum.y==112 && legacy.spectrum.w==906 && legacy.spectrum.h==186);
  assert(legacy.waterfall.x==330 && legacy.waterfall.y==301 && legacy.waterfall.w==906 && legacy.waterfall.h==194);
  check(1232,568);
  check(568,1232);
  check(800,480);
  check_viewport({72,72,1088,424});
  check_viewport({72,72,424,1088});
  std::puts("Home shared geometry tests PASS");
}
