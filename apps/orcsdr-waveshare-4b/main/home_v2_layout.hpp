#pragma once
#include <initializer_list>
// OrcSDR 720-square HOME v2 VISUAL PROPOSAL ONLY. Not wired into production UI.
// Source-of-truth visual references: Wiki Home 2026-09-29; current Tab5 Home
// and dashboard_audio_control. All rectangles are native 720x720 coordinates.
namespace orcsdr::waveshare4b::home_v2 {
struct Rect {
  int x,y,w,h;
  constexpr bool inside() const {return x>=0 && y>=0 && w>0 && h>0 && x+w<=720 && y+h<=720;}
  constexpr bool disjoint(Rect b) const {
    return x+w<=b.x || b.x+b.w<=x || y+h<=b.y || b.y+b.h<=y;
  }
  constexpr bool contains(Rect b) const {
    return b.x>=x && b.y>=y && b.x+b.w<=x+w && b.y+b.h<=y+h;
  }
};
constexpr Rect kHeaderBadge{14,10,64,67};
constexpr Rect kHeaderBrand{89,10,168,67};
constexpr Rect kHeaderStatus{260,10,257,67};
constexpr Rect kHeaderHome{526,18,55,51};
constexpr Rect kHeaderMute{590,18,55,51};
constexpr Rect kHeaderSettings{654,18,52,51};
constexpr Rect kRecentPanel{14,88,172,579};
constexpr Rect kSpectrumPanel{197,88,509,185};
constexpr Rect kSpectrumPlot{209,119,485,142};
constexpr Rect kWaterfallPanel{197,282,509,185};
constexpr Rect kWaterfallPlot{209,314,485,141};
constexpr Rect kReadoutPanel{197,476,509,73};
constexpr Rect kFrequencyTouch{204,495,239,48};
constexpr Rect kMode{451,499,114,43};
constexpr Rect kFilterReadout{575,499,118,43};
constexpr Rect kControlsPanel{197,559,509,107};
constexpr Rect kSpan{205,566,117,43};
constexpr Rect kStep{328,566,117,43};
constexpr Rect kTuneDown{453,566,118,43};
constexpr Rect kTuneUp{579,566,118,43};
constexpr Rect kGain{205,615,117,43};
constexpr Rect kBias{328,615,117,43};
constexpr Rect kPalette{453,615,118,43};
constexpr Rect kMore{579,615,118,43};
constexpr Rect kFooter{14,675,692,36};
constexpr Rect kNavHome{24,124,152,49};
constexpr Rect kNavP25{24,182,152,49};
constexpr Rect kNavAm{24,240,152,49};
constexpr Rect kNavSettings{24,298,152,49};
constexpr Rect kNavRFLab{24,356,152,49};
constexpr Rect kNavFM{24,414,152,49};
constexpr Rect kNavAdsb{24,472,152,49};
constexpr Rect kNavAll{24,592,152,56};
constexpr Rect kPanels[]={kRecentPanel,kSpectrumPanel,kWaterfallPanel,kReadoutPanel,kControlsPanel,kFooter};
constexpr Rect kNav[]={kNavHome,kNavP25,kNavAm,kNavSettings,kNavRFLab,kNavFM,kNavAdsb,kNavAll};
constexpr Rect kActions[]={kHeaderHome,kHeaderMute,kHeaderSettings,kFrequencyTouch,
                            kMode,kFilterReadout,kSpan,kStep,kTuneDown,kTuneUp,
                            kGain,kBias,kPalette,kMore};
constexpr bool valid() {
  for (auto r:kPanels) if(!r.inside())return false;
  for (auto r:kNav) if(!r.inside()||!kRecentPanel.contains(r))return false;
  for (auto r:kActions) if(!r.inside()||r.w<44||r.h<43)return false;
  if(!kSpectrumPanel.contains(kSpectrumPlot)||!kWaterfallPanel.contains(kWaterfallPlot))return false;
  if(!kReadoutPanel.contains(kFrequencyTouch)||!kReadoutPanel.contains(kMode)
     ||!kReadoutPanel.contains(kFilterReadout))return false;
  for (auto r:{kSpan,kStep,kTuneDown,kTuneUp,kGain,kBias,kPalette,kMore})
      if(!kControlsPanel.contains(r))return false;
  for (unsigned i=0;i<sizeof(kPanels)/sizeof(Rect);++i)
      for (unsigned j=i+1;j<sizeof(kPanels)/sizeof(Rect);++j)
          if(!kPanels[i].disjoint(kPanels[j]))return false;
  for (unsigned i=0;i<sizeof(kActions)/sizeof(Rect);++i)
      for (unsigned j=i+1;j<sizeof(kActions)/sizeof(Rect);++j)
          if(!kActions[i].disjoint(kActions[j]))return false;
  for (unsigned i=0;i<sizeof(kNav)/sizeof(Rect);++i)
      for (unsigned j=i+1;j<sizeof(kNav)/sizeof(Rect);++j)
          if(!kNav[i].disjoint(kNav[j]))return false;
  return kHeaderBadge.inside()&&kHeaderBrand.inside()&&kHeaderStatus.inside();
}
static_assert(valid(), "OrcSDR 720-square Home design must fit without overlapping targets");
} // namespace orcsdr::waveshare4b::home_v2
