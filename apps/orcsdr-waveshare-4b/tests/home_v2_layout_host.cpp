#include "home_v2_layout.hpp"
#include <cassert>
#include <cstdio>
using namespace orcsdr::waveshare4b::home_v2;
int main(){
  static_assert(valid());
  assert(kSpectrumPlot.w >= 480);
  assert(kWaterfallPlot.w >= 480);
  assert(kNavAll.h >= 48);
  assert(kHeaderSettings.w >= 48);
  assert(kReadoutPanel.disjoint(kControlsPanel));
  assert(kRecentPanel.disjoint(kSpectrumPanel));
  assert(kFooter.disjoint(kControlsPanel));
  std::puts("HOME_V2_720 PASS: square geometry, legible plot area, touch regions");
}
