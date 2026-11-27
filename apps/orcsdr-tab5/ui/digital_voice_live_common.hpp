#pragma once
// Shared by the LIVE page and the SCAN view: what the scan is doing, in words, and the RF level strip chart. Header-only, draw-only.
#include "digital_voice_dashboard_model.hpp"
#include "digital_voice_dashboard_paint.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace orcsdr::digital_voice_ui::live_common {
using namespace paint;
using layout::Rect;

struct ModeInfo {const char* name;uint16_t color;char detail[96];const char* state;};
inline bool scan_is_running(const Snapshot& s) {
  return s.scan.phase==ScanPhase::settling||s.scan.phase==ScanPhase::scanning||s.scan.phase==ScanPhase::holding||s.scan.phase==ScanPhase::paused;
}
inline ModeInfo scan_mode_info(const Snapshot& s) {
  ModeInfo m{"SCAN STOPPED",kMuted,{},"Stopped"};
  const unsigned idx=s.scan.channel_index,n=s.scan.planned;
  if(s.follow_active) {
    m.name="FOLLOWING";m.color=kCyan;m.state="Following one channel";
    std::snprintf(m.detail,sizeof(m.detail),s.follow_lost?"receiver lost, trying again":"one channel, the scan is paused");
  } else if(s.discover_running) {
    m.name="DISCOVERY";m.color=kCyan;m.state="Discovering";
    if(s.discover_phase==1)std::snprintf(m.detail,sizeof(m.detail),"surveying the bands, pass %u, %u signals so far",static_cast<unsigned>(s.survey_passes+1),static_cast<unsigned>(s.survey_hot));
    else if(s.discover_phase==2)std::snprintf(m.detail,sizeof(m.detail),"decoding channel %u of %u, found %u",idx,n,static_cast<unsigned>(s.discover_found));
    else std::snprintf(m.detail,sizeof(m.detail),"sweeping your bands, found %u",static_cast<unsigned>(s.discover_found));
  } else if(s.scan.stage==ScanStage::error) {
    m.name="RECEIVER PROBLEM";m.color=kAmber;m.state="Receiver problem";
    std::snprintf(m.detail,sizeof(m.detail),"check the USB cable and the RTL-SDR");
  } else if(!scan_is_running(s)) {
    std::snprintf(m.detail,sizeof(m.detail),s.can_scan?"press START on the SCAN screen":"no receiver connected");
  } else {
    const bool monitor=std::strncmp(s.scan.plan_name,"MONITOR",7)==0;
    const bool saved=std::strncmp(s.scan.plan_name,"SAVED LIST",10)==0;
    if(s.scan_on_hold) {
      m.name="HOLD";m.color=kAmber;m.state="Holding this channel";
      std::snprintf(m.detail,sizeof(m.detail),"you paused the %s scan here: press RELEASE to go on",monitor?"MONITOR":saved?"saved list":"sweep");
    } else if(monitor) {
      m.name="MONITOR";m.color=kGreen;m.state=s.scan.phase==ScanPhase::holding?"Staying for a call":"Scanning";
      std::snprintf(m.detail,sizeof(m.detail),"%u systems, channel %u of %u%s",n,idx,n,s.scan.phase==ScanPhase::holding?", staying for a call":"");
    } else if(saved) {
      m.name="SAVED LIST";m.color=kCyan;m.state=s.scan.phase==ScanPhase::holding?"Staying for a call":"Scanning";
      std::snprintf(m.detail,sizeof(m.detail),"channel %u of %u",idx,n);
    } else {
      m.name="SWEEP";m.color=kAmber;m.state=s.scan.phase==ScanPhase::holding?"Staying for a call":"Sweeping";
      std::snprintf(m.detail,sizeof(m.detail),"%s, channel %u of %u",s.scan.plan_name,idx,n);
    }
  }
  return m;
}

// The receiver's own RF level as a strip chart (see Snapshot::follow_trace). Scaled to the window so a quiet channel still shows that it is
// being read; a sample 4 dB or more above the typical level is a signal and is green. Returns true when a signal is in the window.
inline bool rf_trace(Paint& p,const Snapshot& s,Rect tr,bool on) {
  const int bw=tr.w/static_cast<int>(Snapshot::kFollowTrace);
  uint8_t lo=255,hi=0,sorted[Snapshot::kFollowTrace];
  for(size_t i=0;i<Snapshot::kFollowTrace;++i) {sorted[i]=s.follow_trace[i];lo=std::min(lo,s.follow_trace[i]);hi=std::max(hi,s.follow_trace[i]);}
  std::sort(sorted,sorted+Snapshot::kFollowTrace);
  const int med=sorted[Snapshot::kFollowTrace/2];
  const int span=std::max<int>(hi-lo,25);
  bool signal=false;
  for(size_t i=0;i<Snapshot::kFollowTrace;++i) {
    const bool strong=on&&s.follow_trace[i]>=med+17;
    signal=signal||strong;
    const int h=on&&s.follow_trace[i]?std::max(2,(static_cast<int>(s.follow_trace[i])-lo)*(tr.h-4)/span+2):0;
    const int x=tr.x+static_cast<int>(i)*bw;
    p.fillRect(x,tr.y,bw-1,tr.h-h,kBg);
    p.fillRect(x,tr.y+tr.h-h,bw-1,h,strong?kGreen:kCyan);
  }
  return signal;
}

}  // namespace orcsdr::digital_voice_ui::live_common
