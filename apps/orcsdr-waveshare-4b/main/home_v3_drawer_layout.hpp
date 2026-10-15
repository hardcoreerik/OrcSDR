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
constexpr Rect kHeaderPanel{8,8,704,83};
constexpr Rect kMenuHit{16,26,44,48};
constexpr Rect kPrimaryStep{456,501,126,70};
constexpr Rect kBadgeHit{68,18,64,67};
constexpr Rect kBrand{140,17,146,67};
constexpr Rect kHeaderStatus{294,18,184,65};
constexpr Rect kHeaderHome{486,25,48,51};
constexpr Rect kHeaderMute{540,25,48,51};
constexpr Rect kHeaderVisualizer{594,25,48,51};
constexpr Rect kHeaderSettings{648,25,54,51};
constexpr Rect kSpectrumPanel{8,101,704,223};
constexpr Rect kSpectrumPlot{20,143,680,166};
constexpr Rect kWaterfallPanel{8,332,704,149};
constexpr Rect kWaterfallPlot{20,364,680,108};
constexpr Rect kReadoutPanel{8,489,704,95};
constexpr Rect kFrequencyTouch{20,526,326,50};
constexpr Rect kMode{354,501,94,70};
constexpr Rect kFilter{540,600,78,49};
constexpr Rect kControlsPanel{8,592,704,65};
constexpr Rect kSpan{16,600,170,49};
constexpr Rect kStep{194,600,170,49};
constexpr Rect kTuneDown{590,512,50,58};
constexpr Rect kTuneUp{648,512,50,58};
constexpr Rect kGain{626,600,78,49};
constexpr Rect kPalette{372,600,160,49};
constexpr Rect kFooter{8,665,704,47};
// Drawer overlays the radio view; no plot move/re-allocation while open.
constexpr Rect kDrawer{8,101,286,556};
constexpr Rect kBackdrop{294,101,418,556};
constexpr Rect kNavHome{20,143,262,49};
constexpr Rect kNavP25{20,201,262,49};
constexpr Rect kNavAm{20,259,262,49};
constexpr Rect kNavSettings{20,317,262,49};
constexpr Rect kNavRfLab{20,375,262,49};
constexpr Rect kNavFm{20,433,262,49};
constexpr Rect kNavAdsb{20,491,262,49};
constexpr Rect kNavAll{20,597,262,52};
constexpr Rect kBasePanels[]={kHeaderPanel,kSpectrumPanel,kWaterfallPanel,kReadoutPanel,kControlsPanel,kFooter};
constexpr Rect kControls[]={kMenuHit,kBadgeHit,kBrand,kHeaderHome,kHeaderMute,kHeaderVisualizer,kHeaderSettings,
 kFrequencyTouch,kMode,kPrimaryStep,kFilter,kSpan,kStep,kTuneDown,kTuneUp,kGain,kPalette};
constexpr Rect kNavItems[]={kNavHome,kNavP25,kNavAm,kNavSettings,kNavRfLab,kNavFm,kNavAdsb,kNavAll};
constexpr bool valid() {
  if(!kBrand.inside()||!kHeaderStatus.inside()||!kDrawer.inside()||
     !kBackdrop.inside()||!kDrawer.disjoint(kBackdrop)) return false;
  for(auto r:kBasePanels)if(!r.inside())return false;
  if(!kSpectrumPanel.contains(kSpectrumPlot)||!kWaterfallPanel.contains(kWaterfallPlot)||
     !kReadoutPanel.contains(kFrequencyTouch)||!kReadoutPanel.contains(kMode)||
     !kReadoutPanel.contains(kPrimaryStep)||!kReadoutPanel.contains(kTuneDown)||!kReadoutPanel.contains(kTuneUp))return false;
  for(auto r:kControls)if(!r.inside()||r.w<44||r.h<44)return false;
  for(auto r:{kSpan,kStep,kGain,kFilter,kPalette})
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
  return kMenuHit.disjoint(kDrawer)&&kBrand.disjoint(kDrawer)&&kBadgeHit.disjoint(kDrawer)&&kHeaderStatus.disjoint(kDrawer)&&
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
