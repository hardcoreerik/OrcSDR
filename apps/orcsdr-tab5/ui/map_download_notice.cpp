#include "map_download_notice.hpp"
#include "dashboard_audio_control.hpp"
#include "focus_nav.hpp"
#include <M5Unified.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace orcsdr::map_download_notice {
namespace {
bool dirty=true,drawn=false;
screens::Id owner=screens::Id::none;
maps::downloads::Phase previous=maps::downloads::Phase::idle;
int previous_percent=-2;
TuneWarning previous_warning{};
bool eligible(screens::Id screen) {
  return screen!=screens::Id::none&&screen!=screens::Id::maps&&
      screen!=screens::Id::settings&&screen!=screens::Id::documentation&&
      screen!=screens::Id::visualizer&&screens::owns(screen);
}
bool visible(maps::downloads::Phase phase) {
  using P=maps::downloads::Phase;
  return phase==P::downloading||phase==P::verifying||phase==P::complete||
      phase==P::cancelled||phase==P::failed;
}
static_assert(x>=128+6*6*5&&x+width<audio_header::kStatusBarX);
static_assert(x+width<audio_header::kTitleDividerX&&x+width<audio_header::kTitleTextX);
}
void invalidate(){dirty=true;}
bool hit(int32_t px,int32_t py,screens::Id screen,bool blocked) {
  return !blocked&&eligible(screen)&&drawn&&visible(previous)&&owner==screen&&
      px>=x&&px<x+width&&py>=y&&py<y+height;
}
void update(const maps::downloads::Snapshot& s,screens::Id screen,bool blocked,TuneWarning warning) {
  if(blocked||!eligible(screen)){drawn=false;owner=screens::Id::none;dirty=true;return;}
  using P=maps::downloads::Phase;
  const auto total=s.phase==P::verifying?s.verify_total_bytes:s.total_bytes;
  const auto used=s.phase==P::verifying?s.verified_bytes:s.completed_bytes;
  const int percent=total?static_cast<int>(std::min(used,total)*100/total):-1;
  if(!dirty&&owner==screen&&previous==s.phase&&previous_percent==percent&&
     previous_warning.frequency_hz==warning.frequency_hz&&
     previous_warning.nooelec_xtr==warning.nooelec_xtr)return;
  dirty=false;previous=s.phase;previous_percent=percent;previous_warning=warning;
  const bool map_visible = visible(s.phase);
  const char* title=s.phase==P::verifying?"MAP CK":s.phase==P::downloading?"MAP DL":"MAP";
  char value[12]{};
  if(s.phase==P::complete)std::snprintf(value,sizeof(value),"READY");
  else if(s.phase==P::cancelled)std::snprintf(value,sizeof(value),"PAUSED");
  else if(s.phase==P::failed)std::snprintf(value,sizeof(value),"ERROR");
  else if(percent<0)std::snprintf(value,sizeof(value),"...");
  else std::snprintf(value,sizeof(value),"%d%%",percent);
  if(warning.frequency_hz) {
    const uint16_t bg=0x0841;
    M5.Display.fillRoundRect(x,y,width,height,6,bg);
    M5.Display.drawRoundRect(x,y,width,height,6,TFT_ORANGE);
    M5.Display.setFont(nullptr);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextColor(TFT_ORANGE,bg);
    M5.Display.setTextSize(1);
    M5.Display.drawString(warning.nooelec_xtr?"Nooelec XTR":"E4000",x+width/2,y+9);
    char frequency[16];
    std::snprintf(frequency,sizeof(frequency),"%.3f MHz",warning.frequency_hz/1e6);
    M5.Display.drawString(frequency,x+width/2,y+21);
    M5.Display.drawString("Out of range",x+width/2,y+33);
    const char* footer = warning.frequency_hz < 64000000u ? "Use upconverter" : "64-1700 MHz";
    char map_status[20];
    if (map_visible) {
      std::snprintf(map_status, sizeof(map_status), "%s %s", title, value);
      footer = map_status;
      focus_nav::note(x,y,width,height);
    }
    M5.Display.drawString(footer,x+width/2,y+45);
    drawn=true;owner=screen;return;
  }
  if(!map_visible) {
    if(drawn&&owner==screen)M5.Display.fillRect(x,y,width,height,TFT_BLACK);
    drawn=false;owner=screen;return;
  }
  const uint16_t color=s.phase==P::failed?TFT_ORANGE:s.phase==P::complete?0x6fe8:0x2e7f;
  M5.Display.fillRoundRect(x,y,width,height,6,0x0841);
  M5.Display.drawRoundRect(x,y,width,height,6,color);
  M5.Display.setFont(nullptr);M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(color,0x0841);M5.Display.setTextSize(2);
  M5.Display.drawString(title,x+width/2,y+13);
  M5.Display.setTextSize(std::strlen(value)>4?2:3);
  M5.Display.drawString(value,x+width/2,y+38);
  focus_nav::note(x,y,width,height);
  drawn=true;owner=screen;
}
}
