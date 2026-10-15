#include "home_v3_drawer_layout.hpp"
#include <cassert>
#include <cstdio>
using namespace orcsdr::waveshare4b::home_v3;
int main(){
  static_assert(valid());
  assert(kSpectrumPlot.w==668 && kWaterfallPlot.w==668);
  assert(kDrawer.w==286 && kBackdrop.w==406);
  assert(kHeaderHome.disjoint(kDrawer)&&kFooter.disjoint(kDrawer));
  for(const auto& r:kNavItems)assert(kDrawer.contains(r));
  Navigation nav{};
  assert(!nav.open && nav.main_content_interactive() && !nav.outside_hit(500,300));
  nav.apply(NavAction::badge_tap);
  assert(nav.open && !nav.main_content_interactive() && nav.outside_hit(500,300));
  assert(!nav.outside_hit(40,200) && !nav.outside_hit(40,45));
  nav.apply(NavAction::other);assert(nav.open);
  nav.apply(NavAction::badge_tap);assert(!nav.open);
  nav.apply(NavAction::badge_tap);nav.apply(NavAction::outside_tap);assert(!nav.open);
  nav.apply(NavAction::badge_tap);nav.apply(NavAction::item_selected);assert(!nav.open);
  nav.apply(NavAction::badge_tap);nav.apply(NavAction::screen_changed);assert(!nav.open);
  assert(kNavAll.hit(kNavAll.x+10,kNavAll.y+10));
  std::puts("HOME_V3_720 PASS: full-width plots, overlay drawer, badge/tap state, navigation hitboxes");
}
