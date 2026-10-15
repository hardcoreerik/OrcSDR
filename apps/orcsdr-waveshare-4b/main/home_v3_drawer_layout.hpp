#pragma once
// Native 720x720 OrcSDR Home v3 geometry / navigation-drawer design contract.
// Used by home_v3_ui.cpp; physical verification pending.
#include <cstdint>
#include <initializer_list>
namespace orcsdr::waveshare4b::home_v3 {
struct Rect {
  int x,y,w,h;
  constexpr bool inside() const { return x>=0 && y>=0 && w>0 && h>0 && x+w<=720 && y+h<=720; }
  constexpr bool disjoint(Rect b) const {
    return x+w<=b.x || b.x+b.w<=x || y+h<=b.y || b.y+b.h<=y;
  }
  constexpr bool contains(Rect b) const {
    return b.x>=x && b.y>=y && b.x+b.w<=x+w && b.y+b.h<=y+h;
  }
  constexpr bool hit(int px,int py) const {
    return px>=x && py>=y && px<x+w && py<y+h;
  }
};
constexpr Rect kBadgeHit{14,10,64,67};
constexpr Rect kBrand{86,10,155,67};
constexpr Rect kHeaderStatus{246,10,250,67};
constexpr Rect kHeaderHome{503,18,46,51};
constexpr Rect kHeaderMute{555,18,46,51};
constexpr Rect kHeaderVisualizer{607,18,46,51};
constexpr Rect kHeaderSettings{659,18,47,51};
constexpr Rect kSpectrumPanel{14,88,692,185};
constexpr Rect kSpectrumPlot{26,119,668,142};
constexpr Rect kWaterfallPanel{14,282,692,185};
constexpr Rect kWaterfallPlot{26,314,668,141};
constexpr Rect kReadoutPanel{14,476,692,73};
constexpr Rect kFrequencyTouch{22,496,425,46};
constexpr Rect kMode{455,498,117,44};
constexpr Rect kFilter{579,498,118,44};
constexpr Rect kControlsPanel{14,559,692,107};
constexpr Rect kSpan{22,565,161,44};
constexpr Rect kStep{192,565,161,44};
constexpr Rect kTuneDown{362,565,161,44};
constexpr Rect kTuneUp{532,565,166,44};
constexpr Rect kGain{22,615,161,44};
constexpr Rect kBias{192,615,161,44};
constexpr Rect kPalette{362,615,161,44};
constexpr Rect kMore{532,615,166,44};
constexpr Rect kFooter{14,674,692,37};
// Drawer overlays the radio view; no plot move/re-allocation while open.
constexpr Rect kDrawer{14,88,286,578};
constexpr Rect kBackdrop{300,88,406,578};
constexpr Rect kNavHome{27,130,260,49};
constexpr Rect kNavP25{27,188,260,49};
constexpr Rect kNavAm{27,246,260,49};
constexpr Rect kNavSettings{27,304,260,49};
constexpr Rect kNavRfLab{27,362,260,49};
constexpr Rect kNavFm{27,420,260,49};
constexpr Rect kNavAdsb{27,478,260,49};
constexpr Rect kNavAll{27,597,260,56};
constexpr Rect kBasePanels[]={kSpectrumPanel,kWaterfallPanel,kReadoutPanel,kControlsPanel,kFooter};
constexpr Rect kControls[]={kBadgeHit,kHeaderHome,kHeaderMute,kHeaderVisualizer,kHeaderSettings,
 kFrequencyTouch,kMode,kFilter,kSpan,kStep,kTuneDown,kTuneUp,kGain,kBias,kPalette,kMore};
constexpr Rect kNavItems[]={kNavHome,kNavP25,kNavAm,kNavSettings,kNavRfLab,kNavFm,kNavAdsb,kNavAll};
constexpr bool valid() {
  if(!kBrand.inside()||!kHeaderStatus.inside()||!kDrawer.inside()||
     !kBackdrop.inside()||!kDrawer.disjoint(kBackdrop)) return false;
  for(auto r:kBasePanels)if(!r.inside())return false;
  if(!kSpectrumPanel.contains(kSpectrumPlot)||!kWaterfallPanel.contains(kWaterfallPlot)||
     !kReadoutPanel.contains(kFrequencyTouch)||!kReadoutPanel.contains(kMode)||
     !kReadoutPanel.contains(kFilter))return false;
  for(auto r:kControls)if(!r.inside()||r.w<44||r.h<44)return false;
  for(auto r:{kSpan,kStep,kTuneDown,kTuneUp,kGain,kBias,kPalette,kMore})
     if(!kControlsPanel.contains(r))return false;
  for(auto r:kNavItems)if(!r.inside()||!kDrawer.contains(r)||r.w<44||r.h<44)return false;
  for(unsigned i=0;i<sizeof(kBasePanels)/sizeof(Rect);++i)
    for(unsigned j=i+1;j<sizeof(kBasePanels)/sizeof(Rect);++j)
      if(!kBasePanels[i].disjoint(kBasePanels[j]))return false;
  for(unsigned i=0;i<sizeof(kControls)/sizeof(Rect);++i)
    for(unsigned j=i+1;j<sizeof(kControls)/sizeof(Rect);++j)
      if(!kControls[i].disjoint(kControls[j]))return false;
  for(unsigned i=0;i<sizeof(kNavItems)/sizeof(Rect);++i)
    for(unsigned j=i+1;j<sizeof(kNavItems)/sizeof(Rect);++j)
      if(!kNavItems[i].disjoint(kNavItems[j]))return false;
  return kBadgeHit.disjoint(kDrawer)&&kHeaderStatus.disjoint(kDrawer)&&
     kHeaderHome.disjoint(kDrawer)&&kHeaderVisualizer.disjoint(kDrawer)&&
     kFooter.disjoint(kDrawer)&&kFooter.disjoint(kBackdrop);
}
static_assert(valid(),"OrcSDR 720-square Home requires nonoverlapping accessible hitboxes");
enum class NavAction:uint8_t { badge_tap,outside_tap,item_selected,screen_changed,other };
struct Navigation {
 bool open=false;
 constexpr void apply(NavAction a) {
   switch(a) {
     case NavAction::badge_tap:open=!open;break;
     case NavAction::outside_tap:
     case NavAction::item_selected:
     case NavAction::screen_changed:open=false;break;
     case NavAction::other:break;
   }
 }
 constexpr bool outside_hit(int x,int y) const { return open&&kBackdrop.hit(x,y); }
 constexpr bool main_content_interactive() const { return !open; }
};
} // namespace orcsdr::waveshare4b::home_v3
