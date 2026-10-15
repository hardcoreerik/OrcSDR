#pragma once
#include <cstdint>
#include <cstddef>
namespace orcsdr::waveshare4b::fm {
struct Rect {
 int x,y,w,h;
 constexpr bool inside() const {return x>=0 && y>=0 && w>0 && h>0 && x+w<=720 && y+h<=720;}
 constexpr bool contains(Rect b) const {return b.x>=x&&b.y>=y&&b.x+b.w<=x+w&&b.y+b.h<=y+h;}
 constexpr bool disjoint(Rect r) const {return x+w<=r.x||r.x+r.w<=x||y+h<=r.y||r.y+r.h<=y;}
 constexpr bool hit(int px,int py) const {return px>=x&&px<x+w&&py>=y&&py<y+h;}
};
enum class Tab:uint8_t {listen,spectrum,station_rds,rf_health,settings,count};
constexpr unsigned kTabs=static_cast<unsigned>(Tab::count);
constexpr const char* kTabNames[kTabs]={"LISTEN","SPECTRUM","STATION / RDS","RF HEALTH","SETTINGS"};
constexpr const char* kTabRefs[kTabs]={"fm-listen.png","fm-spectrum.png","fm-station-rds.png","fm-rf-health.png","fm-settings.png"};
constexpr Rect kBadge{14,10,64,67};
constexpr Rect kBrand{86,10,155,67};
constexpr Rect kStatus{246,10,250,67};
constexpr Rect kHome{503,18,46,51};
constexpr Rect kMute{555,18,46,51};
constexpr Rect kVisual{607,18,46,51};
constexpr Rect kGear{659,18,47,51};
constexpr Rect kPageTitle{14,90,692,70};
constexpr Rect kBody{14,169,692,456};
constexpr Rect kTabsBar{14,636,692,70};
constexpr Rect kTabsRect[kTabs]={{14,636,132,70},{154,636,132,70},{294,636,132,70},{434,636,132,70},{574,636,132,70}};
constexpr Rect kListenFrequency{20,175,680,95};
constexpr Rect kListenSignal{20,280,330,132};
constexpr Rect kListenPreset{362,280,338,132};
constexpr Rect kListenAudio{20,422,680,94};
constexpr Rect kListenActions{20,527,680,88};
constexpr Rect kSpectrumSummary{20,175,680,78};
constexpr Rect kSpectrumPlot{27,276,666,138};
constexpr Rect kWaterfallPlot{27,426,666,139};
constexpr Rect kSpectrumActions{20,576,680,43};
constexpr Rect kStationSummary{20,175,680,78};
constexpr Rect kStationPS{20,266,330,92};
constexpr Rect kStationPI{362,266,338,92};
constexpr Rect kStationPTY{20,372,330,92};
constexpr Rect kStationStereo{362,372,338,92};
constexpr Rect kStationRT{20,479,680,136};
constexpr Rect kHealthCards[8]={
 {20,175,330,96},{362,175,338,96},{20,282,330,96},{362,282,338,96},
 {20,389,330,96},{362,389,338,96},{20,496,330,96},{362,496,338,96}};
constexpr Rect kHealthFooter{20,597,680,25};
constexpr Rect kSettingsRadio{20,175,330,440};
constexpr Rect kSettingsAudio{362,175,338,440};
constexpr uint32_t kMinHz=76000000,kMaxHz=108000000;
constexpr bool in_fm_band(uint32_t hz){return hz>=kMinHz&&hz<=kMaxHz;}
struct Features {
 bool iq=false,demod=false,stereo=false,rds=false,audio=false,record=false,presets=false;
 constexpr bool valid() const {return (!rds||demod)&&(!stereo||demod)&&(!audio||demod)&&(!record||audio);}
};
constexpr Features kIqOnly{true,false,false,false,false,false,false};
constexpr bool valid(){
 if(kTabs!=5||!kBadge.inside()||!kBrand.inside()||!kStatus.inside()||
    !kHome.inside()||!kPageTitle.inside()||!kBody.inside()||!kTabsBar.inside())return false;
 const Rect hdr[]={kBadge,kBrand,kStatus,kHome,kMute,kVisual,kGear};
 for(unsigned i=0;i<sizeof(hdr)/sizeof(Rect);++i)
  for(unsigned j=i+1;j<sizeof(hdr)/sizeof(Rect);++j)
   if(!hdr[i].disjoint(hdr[j]))return false;
 for(unsigned i=0;i<kTabs;++i){
  if(!kTabsBar.contains(kTabsRect[i])||kTabsRect[i].w<44||kTabsRect[i].h<44)return false;
  for(unsigned j=i+1;j<kTabs;++j)if(!kTabsRect[i].disjoint(kTabsRect[j]))return false;
 }
 const Rect listen[]={kListenFrequency,kListenSignal,kListenPreset,kListenAudio,kListenActions};
 const Rect station[]={kStationSummary,kStationPS,kStationPI,kStationPTY,kStationStereo,kStationRT};
 for(auto r:listen)if(!kBody.contains(r))return false;
 for(auto r:station)if(!kBody.contains(r))return false;
 for(auto r:kHealthCards)if(!kBody.contains(r))return false;
 for(unsigned i=0;i<8;++i)for(unsigned j=i+1;j<8;++j)
  if(!kHealthCards[i].disjoint(kHealthCards[j]))return false;
 if(!kBody.contains(kHealthFooter)||!kBody.contains(kSettingsRadio)||
    !kBody.contains(kSettingsAudio)||!kSettingsRadio.disjoint(kSettingsAudio))return false;
 return kBody.contains(kSpectrumSummary)&&kBody.contains(kSpectrumPlot)&&
        kBody.contains(kWaterfallPlot)&&kBody.contains(kSpectrumActions)&&
        kSpectrumPlot.w==666&&kWaterfallPlot.w==666&&kIqOnly.valid();
}
static_assert(valid(),"FM five-tab square layout failed");
constexpr Tab from_index(unsigned i){return i<kTabs?static_cast<Tab>(i):Tab::listen;}
constexpr unsigned index(Tab t){return static_cast<unsigned>(t);}
struct Model {
 Tab selected=Tab::listen;
 Features features=kIqOnly;
 constexpr bool select(unsigned i){if(i>=kTabs)return false;selected=from_index(i);return true;}
 constexpr bool can_show_audio() const{return features.audio&&features.demod;}
 constexpr bool can_show_rds() const{return features.rds&&features.demod;}
 constexpr bool can_record() const{return features.record&&can_show_audio();}
};
}
