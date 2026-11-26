#include "digital_voice_dashboard.hpp"
#include "digital_voice_dashboard_paint.hpp"
#include "dv_bandplan.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace orcsdr::digital_voice_ui {
namespace {
using layout::Rect;
using namespace paint;
constexpr const char* kTabs[]={"SCAN","LIVE","LAST HEARD","SYSTEMS","SIGNAL","SETUP"};
static_assert(sizeof(kTabs)/sizeof(kTabs[0]) == layout::kTabCount);

void number(char (&out)[36],uint32_t n) {std::snprintf(out,sizeof(out),"%lu",static_cast<unsigned long>(n));}
// Draw one widget with diff painting: only the calls that changed since its last paint reach the screen. When its structure changed
// (a different number of calls) it is drawn once more in full.
enum Slot : unsigned {kSlotVoice=16,kSlotPage=0,kSlotSysFrame=1,kSlotSysTail=2,kSlotSysRow=3,kSlotSysFacts=7,kSlotDiscFrame=8,kSlotDiscProgress=9,kSlotDiscResult=10,kSlotDiscButtons=11,kSlotDiscRow=12};
template<class F> void widget(Paint& p,unsigned id,uint16_t bg,F fn) {
  p.use(id,bg);fn();
  if(p.done()) {p.use(id,bg);fn();p.done();}
}
// The protocol's area code, shown with the same widget for every protocol: DMR colour code, NXDN RAN (RAN 0 is valid).
void area_text(char (&out)[16],Protocol pr,uint8_t cc,bool ccv,uint8_t ran,bool ranv) {
  out[0]=0;
  if(pr==Protocol::dmr&&ccv)std::snprintf(out,sizeof(out),"CC%u",static_cast<unsigned>(cc));
  else if((pr==Protocol::nxdn48||pr==Protocol::nxdn96)&&ranv)std::snprintf(out,sizeof(out),"RAN %u",static_cast<unsigned>(ran));
}
void call_identity(Paint& p,const Snapshot& s,Rect card,int x,int y) {
  char v[36]{};
  p.title("CALL / IDENTITY",x,y,card);
  if (!trusted_identity(s) || !s.has_source) {
    p.fact("SOURCE","UNKNOWN",x,y+52,card,kAmber);
  } else {
    number(v,s.source_id);p.fact("SOURCE",v,x,y+52,card,kGreen);
  }
  if (!trusted_identity(s) || !s.has_destination) {
    p.fact("DEST / TG","UNKNOWN",x,y+92,card,kAmber);
  } else {
    number(v,s.destination_id);p.fact(s.group_call?"TALKGROUP":"DESTINATION",v,x,y+92,card,kGreen);
  }
  p.fact("CALL TYPE",(!trusted_identity(s)||!s.has_destination)?"UNKNOWN":s.group_call?"GROUP":"PRIVATE",x,y+132,card);
}
void protocol_facts(Paint& p,const Snapshot& s,Rect card,int x,int y) {
  char v[36]{};
  p.title("PROTOCOL FIELDS",x,y,card);
  if (s.protocol == Protocol::dmr) {
    if(s.colour_code_valid){std::snprintf(v,sizeof(v),"%u",s.colour_code);p.fact("COLOR CODE",v,x,y+52,card,kGreen);}
    else p.fact("COLOR CODE","UNKNOWN",x,y+52,card,kAmber);
    if(s.timeslot_valid && (s.timeslot==1||s.timeslot==2)){
      std::snprintf(v,sizeof(v),"TS%u",s.timeslot);p.fact("TIME SLOT",v,x,y+92,card,kGreen);
    } else p.fact("TIME SLOT","UNKNOWN",x,y+92,card,kAmber);
  } else if (s.protocol==Protocol::p25_phase1 || s.protocol==Protocol::p25_phase2) {
    if (s.nac_valid){std::snprintf(v,sizeof(v),"%03X",s.nac);p.fact("NAC",v,x,y+52,card,kGreen);}
    else p.fact("NAC","UNKNOWN",x,y+52,card,kAmber);
    if(s.protocol==Protocol::p25_phase2)p.fact("PHASE II","VOICE UNSUPPORTED",x,y+92,card,kAmber);
  } else if (s.protocol==Protocol::nxdn48 || s.protocol==Protocol::nxdn96) {
    const bool ran=s.ran_valid && s.evidence>=Evidence::integrity_valid;
    if(ran){std::snprintf(v,sizeof(v),"%u",s.ran);p.fact("RAN",v,x,y+52,card,kGreen);}
    else p.fact("RAN","UNKNOWN",x,y+52,card,kAmber);
    p.fact("CHANNEL",s.nxdn.channel_valid && s.evidence>=Evidence::integrity_valid?
           nxdn_channel_name(s.nxdn.channel):"UNKNOWN",x,y+92,card,kAmber);
    p.fact("SYSTEM",s.nxdn.topology_valid && s.evidence>=Evidence::integrity_valid?
           nxdn_topology_name(s.nxdn.topology):"UNKNOWN",x,y+132,card,kAmber);
    if(s.nxdn.channel_number_valid && s.evidence>=Evidence::integrity_valid) {
      std::snprintf(v,sizeof(v),"%u",s.nxdn.channel_number);
      p.fact("CHANNEL #",v,x,y+172,card,kGreen);
    }
  } else if(s.protocol==Protocol::dstar) {
    p.fact("MY CALL",s.dstar_header_valid&&s.dstar_my[0]?s.dstar_my:"UNKNOWN",x,y+52,card,kAmber);
    p.fact("UR CALL",s.dstar_header_valid&&s.dstar_ur[0]?s.dstar_ur:"UNKNOWN",x,y+92,card,kAmber);
  } else if (s.protocol==Protocol::ysf) {
    if (s.ysf_fich_valid){std::snprintf(v,sizeof(v),"%08lX",static_cast<unsigned long>(s.ysf_fich_raw));p.fact("FICH RAW",v,x,y+52,card,kGreen);}
    else p.fact("FICH RAW","UNKNOWN",x,y+52,card,kAmber);
    p.fact("CALLSIGN","NOT DECODED",x,y+92,card,kAmber);
  } else {
    p.fact("MODE","NOT VERIFIED",x,y+52,card,kAmber);
  }
}
void context(Paint& p,const Snapshot& s,Density mode) {
  const Rect r=layout::kContext;
  p.fillRoundRect(r.x,r.y,r.w,r.h,6,kPanel);
  p.drawRoundRect(r.x,r.y,r.w,r.h,6,kEdge);
  p.text("DIGITAL VOICE  /  RECEIVE ONLY",r.x+14,r.y+r.h/2,kCyan,2,r);
  p.text(mode==Density::discovery_first?"DISCOVERY FIRST":"ANALYSIS FIRST",r.x+540,r.y+r.h/2,kGreen,2,r);
  p.text(origin_name(s.origin),r.x+864,r.y+r.h/2,
         s.origin==Origin::live?kWhite:kAmber,2,r);
}
void nav(Paint& p,const UiState& state) {
  p.fillRect(0,642,1280,78,kBg);
  for(int i=0;i<layout::kTabCount;++i)
    p.button(kTabs[i],layout::tab(i),true,state.view==static_cast<View>(i)||(i==0&&state.view==View::discover));
}
// ---- Voice audio panel (LIVE page). Retained widget: repainted alone, inside its own rectangle, when audio_revision moves.
Rect voice_rect(Density d) {
  const auto a=layout::kBasicHero,b=layout::kAdvancedFacts;
  return d==Density::discovery_first?Rect{a.x+12,a.y+372,a.w-24,100}:Rect{b.x+12,b.y+330,b.w-24,100};
}
constexpr int kListenBtnW=96,kListenBtnStep=100;
Rect listen_button(Rect r,int i) {return {r.x+r.w-4*kListenBtnStep+i*kListenBtnStep,r.y+8,kListenBtnW,40};}
uint16_t phase_color(VoicePhase ph) {
  switch(ph) {
    case VoicePhase::decoding:return kGreen;
    case VoicePhase::silence_frame:case VoicePhase::buffering:case VoicePhase::acquiring:case VoicePhase::call_ended:return kCyan;
    case VoicePhase::degraded:case VoicePhase::privacy_unknown:case VoicePhase::muted:return kAmber;
    case VoicePhase::protected_voice:return kRed;
    case VoicePhase::listening:return kWhite;
    default:return kMuted;
  }
}
void voice_panel_body(Paint& p,const Snapshot& s,Density d) {
  const Rect r=voice_rect(d);
  const VoiceAudioView& v=s.voice;
  p.fillRect(r.x,r.y,r.w,r.h,kPanel);
  p.drawFastHLine(r.x+6,r.y-2,r.w-12,kEdge);
  p.text(voice_phase_name(v.phase),r.x+8,r.y+28,phase_color(v.phase),2,r);
  char t[80];
  if(v.active_slot&&v.phase!=VoicePhase::protected_voice&&v.source_id)
    std::snprintf(t,sizeof(t),"SLOT %u  %lu>%lu  %s",static_cast<unsigned>(v.active_slot),static_cast<unsigned long>(v.source_id),
                  static_cast<unsigned long>(v.destination_id),v.group_call?"GROUP":"PRIVATE");
  else if(v.privacy_ts1==2||v.privacy_ts2==2)std::snprintf(t,sizeof(t),"AUDIO WITHHELD - NO DECODE");
  else std::snprintf(t,sizeof(t),"%s  SLOT1 %s  SLOT2 %s",listen_mode_name(v.listen),v.privacy_ts1==1?"CLEAR":"-",v.privacy_ts2==1?"CLEAR":"-");
  p.label(t,r.x+8,r.y+58,r);
  std::snprintf(t,sizeof(t),"SP %lu SIL %lu ERR %lu BUF %ums",static_cast<unsigned long>(v.speech_frames),
                static_cast<unsigned long>(v.silence_frames),static_cast<unsigned long>(v.error_frames),static_cast<unsigned>(v.buffered_ms));
  p.label(t,r.x+8,r.y+86,r);
  for(int i=0;i<4;++i) {
    const bool sel=static_cast<int>(v.listen)==i;
    p.button(i==0?"AUTO":i==1?"SLOT 1":i==2?"SLOT 2":"MUTE",listen_button(r,i),v.available,sel);
  }
  // 8-segment level meter under the buttons (coarse: one repaint per segment change at most)
  const int bx=r.x+r.w-4*kListenBtnStep,by=r.y+62;
  for(int i=0;i<8;++i)
    p.fillRect(bx+i*24,by,20,26,i<v.level?(i<6?kGreen:kAmber):kEdge);
}
// The voice panel has its own slot: the page's calls never shift when the panel's phase makes more or fewer calls.
void voice_panel(Paint& p,const Snapshot& s,Density d) {
  Paint::Saved outer;p.save(outer);
  widget(p,kSlotVoice,kPanel,[&]{voice_panel_body(p,s,d);});
  p.restore(outer);
}
void live_discovery(Paint& p,const Snapshot& s) {
  const auto a=layout::kBasicHero,b=layout::kBasicFacts;
  p.card(a);p.card(b);
  p.title("CURRENT ACTIVITY",a.x+20,a.y+32,a);
  p.text(protocol_name(s.protocol),a.x+20,a.y+79,kCyan,3,a);
  p.label(integration_name(s.protocol),a.x+20,a.y+126,a);
  p.hr(a.x+18,a.y+160,a.w-36);
  call_identity(p,s,a,a.x+20,a.y+197);
  p.hr(a.x+18,a.y+368,a.w-36);
  voice_panel(p,s,Density::discovery_first);

  protocol_facts(p,s,b,b.x+18,b.y+32);
  p.hr(b.x+18,b.y+218,b.w-36);
  p.label("ENC / PRIVACY",b.x+18,b.y+271,b);
  p.text(s.encryption_known?(s.is_encrypted?"ENCRYPTED":"CLEAR FLAG"):
         "UNKNOWN",b.x+18,b.y+310,
         s.encryption_known?(s.is_encrypted?kRed:kGreen):kAmber,2,b);
  p.label("No inference from missing bits",b.x+18,b.y+416,b);   // must fit the 426 px panel at 12 px per character
}
void live_analysis(Paint& p,const Snapshot& s) {
  const auto a=layout::kAdvancedMain,b=layout::kAdvancedFacts;
  p.card(a);p.card(b);
  call_identity(p,s,a,a.x+18,a.y+32);
  p.hr(a.x+18,a.y+203,a.w-36);
  p.fact("PROTOCOL",protocol_name(s.protocol),a.x+18,a.y+245,a);
  p.fact("VOICE",voice_phase_name(s.voice.phase),a.x+18,a.y+292,a,phase_color(s.voice.phase));
  p.fact("ENC",s.encryption_known?(s.is_encrypted?"ENCRYPTED":"CLEAR FLAG"):"UNKNOWN",
         a.x+18,a.y+339,a,s.encryption_known?(s.is_encrypted?kRed:kGreen):kAmber);
  p.label(origin_name(s.origin),a.x+18,a.y+426,a);
  protocol_facts(p,s,b,b.x+18,b.y+32);
  p.hr(b.x+18,b.y+215,b.w-36);
  p.title("INTEGRITY",b.x+18,b.y+252,b);
  p.fact("IDENTITY GATE",trusted_identity(s)?"PASSED":"NOT MET",b.x+18,b.y+302,b,
         trusted_identity(s)?kGreen:kAmber);
  voice_panel(p,s,Density::analysis_first);
}
void last_heard(Paint& p,const Snapshot& s,Density mode,uint8_t offset,uint8_t selected) {
  const auto a=mode==Density::discovery_first?layout::kBasicHero:layout::kAdvancedMain;
  const auto b=mode==Density::discovery_first?layout::kBasicFacts:layout::kAdvancedFacts;
  p.card(a);p.card(b);
  p.title("LAST HEARD / VALIDATED EVENTS",a.x+18,a.y+32,a);
  p.label("CLOCK   PROTOCOL   SOURCE   DESTINATION",a.x+18,a.y+68,a);
  if (!s.last_heard_available) {
    p.text("HISTORY NOT INTEGRATED",a.x+18,a.y+146,kAmber,2,a);
  } else if(s.history_count==0) {
    p.text("NO RECORDED EVENTS",a.x+18,a.y+146,kMuted,2,a);
  } else {
    const size_t size=std::min<size_t>(s.history_count,kHistory);
    for(size_t row=0;row<6&&row+offset<size;++row) {
      const Heard& h=s.history[row+offset];
      int y=a.y+113+int(row)*52;
      char who[40],area[16],slot[8]{};
      if(h.evidence==Evidence::identity_valid&&h.has_source&&h.has_destination)
        std::snprintf(who,sizeof(who),"%lu>%lu",static_cast<unsigned long>(h.source_id),static_cast<unsigned long>(h.destination_id));
      else if(h.evidence==Evidence::identity_valid&&h.has_source)std::snprintf(who,sizeof(who),"%lu",static_cast<unsigned long>(h.source_id));
      else std::snprintf(who,sizeof(who),"ID UNKNOWN");
      area_text(area,h.protocol,h.colour_code,h.colour_code_valid,h.ran,h.ran_valid);
      if(h.timeslot_valid&&(h.timeslot==1||h.timeslot==2))std::snprintf(slot,sizeof(slot),"TS%u",static_cast<unsigned>(h.timeslot));
      char v[96];std::snprintf(v,sizeof(v),"%s  %s  %s  %s %s",h.has_time?h.time_utc:"TIME ?",protocol_name(h.protocol),who,area,slot);
      if(row+offset==selected)p.fillRoundRect(a.x+12,y-17,a.w-24,40,4,kSelected);
      p.text(v,a.x+20,y,(h.evidence==Evidence::identity_valid)?kGreen:kMuted,2,a);
      p.hr(a.x+18,y+24,a.w-36);
    }
  }
  p.title("SELECTED EVENT",b.x+18,b.y+32,b);
  if(!s.last_heard_available||selected>=s.history_count) {
    p.label("SELECT AN OBSERVED EVENT",b.x+18,b.y+103,b);
  } else {
    const Heard& h=s.history[selected];
    p.text(protocol_name(h.protocol),b.x+18,b.y+105,kWhite,2,b);
    p.fact("ORIGIN",origin_name(h.origin),b.x+18,b.y+153,b,kAmber);
    char v[36];
    if(h.evidence==Evidence::identity_valid && h.has_source) {
      number(v,h.source_id);p.fact("SOURCE",v,b.x+18,b.y+206,b,kGreen);
    } else p.fact("SOURCE","UNKNOWN",b.x+18,b.y+206,b,kAmber);
    if(h.evidence==Evidence::identity_valid && h.has_destination) {
      number(v,h.destination_id);p.fact("DEST",v,b.x+18,b.y+252,b,kGreen);
    } else p.fact("DEST","UNKNOWN",b.x+18,b.y+252,b,kAmber);
    p.fact("EVIDENCE",evidence_name(h.evidence),b.x+18,b.y+301,b);
    char area[16];area_text(area,h.protocol,h.colour_code,h.colour_code_valid,h.ran,h.ran_valid);
    if(area[0]&&h.evidence>=Evidence::integrity_valid) {
      char slot[48];
      if(h.timeslot_valid&&(h.timeslot==1||h.timeslot==2))
        std::snprintf(slot,sizeof(slot),"%s  TS%u  %s",area,static_cast<unsigned>(h.timeslot),h.has_destination?(h.group_call?"GROUP":"PRIVATE"):"");
      else std::snprintf(slot,sizeof(slot),"%s",area);
      p.fact(h.protocol==Protocol::dmr?"COLOUR":"AREA",slot,b.x+18,b.y+352,b,kGreen);
    }
  }
  if(s.history_count>6 && s.last_heard_available) {
    p.button("PREV",{a.x+18,a.y+418,94,42},offset>0);
    p.button("NEXT",{a.x+124,a.y+418,94,42},static_cast<size_t>(offset)+6<s.history_count);
  }
  p.label("No guessed times / aliases / IDs",b.x+18,b.y+421,b);
}
unsigned sys_count(const Snapshot& s) {
  return s.systems_available?std::min<unsigned>(s.observed_system_count,kObservedSystems):0;
}
uint32_t fnv_bytes(uint32_t h,const void* d,size_t n) {
  const auto* b=static_cast<const uint8_t*>(d);
  for(size_t i=0;i<n;++i){h^=b[i];h*=16777619u;}
  return h;
}
template<class T> uint32_t fnv_val(uint32_t h,const T& v) {return fnv_bytes(h,&v,sizeof(v));}
// What one Systems row shows, as a number: the row is repainted only when this changes (no page repaint on a bare revision).
uint32_t sys_row_sig(const ObservedSystem& y,bool selected) {
  uint32_t h=2166136261u;
  h=fnv_val(h,y.protocol);h=fnv_val(h,y.evidence);h=fnv_val(h,y.colour_code);h=fnv_val(h,y.colour_code_valid);
  h=fnv_val(h,y.ran);h=fnv_val(h,y.ran_valid);h=fnv_val(h,y.topology);h=fnv_val(h,y.topology_valid);
  h=fnv_val(h,y.has_frequency);h=fnv_val(h,y.frequency_hz);h=fnv_val(h,y.talkgroup_count);
  h=fnv_val(h,y.subscriber_count);h=fnv_val(h,y.heard_count);h=fnv_bytes(h,y.last_time,sizeof(y.last_time));
  return fnv_val(h,selected);
}
// What the right-hand panel shows (database totals, the selected system's talkgroups and units, the save button).
uint32_t sys_facts_sig(const Snapshot& s,const UiState& u,unsigned count) {
  uint32_t h=2166136261u;
  h=fnv_val(h,s.evidence);h=fnv_val(h,s.db_available);h=fnv_val(h,s.db_systems);h=fnv_val(h,s.db_talkgroups);
  h=fnv_val(h,s.db_subscribers);h=fnv_val(h,s.can_save_channel);h=fnv_val(h,count);
  if(count) {
    const auto& y=s.observed_systems[std::min<unsigned>(u.selected_system,count-1)];
    h=fnv_val(h,y.talkgroup_count);h=fnv_val(h,y.subscriber_count);h=fnv_val(h,y.top_talkgroup_n);h=fnv_val(h,y.top_subscriber_n);
    for(unsigned i=0;i<kTopIds;++i){h=fnv_val(h,y.top_talkgroups[i]);h=fnv_val(h,y.top_subscribers[i]);}
  }
  return h;
}
void systems_row(Paint& p,const Snapshot& s,const UiState& u,Rect a,unsigned i) {
  const auto& sys=s.observed_systems[i];
  const int yy=a.y+93+static_cast<int>(i)*76;
  p.fillRoundRect(a.x+11,yy-17,a.w-22,70,4,i==u.selected_system?kSelected:kPanel);   // this row only, painted over itself
  p.text(protocol_name(sys.protocol),a.x+18,yy,
         sys.evidence>=Evidence::integrity_valid?kGreen:kAmber,2,a);
  const bool verified=sys.evidence>=Evidence::integrity_valid;
  char area[16];area_text(area,sys.protocol,sys.colour_code,sys.colour_code_valid,sys.ran,sys.ran_valid);
  if(area[0]&&verified)p.text(area,a.x+240,yy,kWhite,2,a);
  if(sys.topology_valid&&verified)p.label(nxdn_topology_name(sys.topology),a.x+(area[0]&&verified?400:240),yy,a);
  else if(!(area[0]&&verified))p.label("SYSTEM TYPE UNKNOWN",a.x+240,yy,a);
  char val[160];
  if(sys.has_frequency)std::snprintf(val,sizeof(val),"%lu.%06lu MHz  TG %u  UNITS %u  HEARD %lu%s%s",
      static_cast<unsigned long>(sys.frequency_hz/1000000),static_cast<unsigned long>(sys.frequency_hz%1000000),
      static_cast<unsigned>(sys.talkgroup_count),static_cast<unsigned>(sys.subscriber_count),
      static_cast<unsigned long>(sys.heard_count),sys.last_time[0]?"  ":"",sys.last_time);
  else std::snprintf(val,sizeof(val),"FREQUENCY UNKNOWN");
  p.label(val,a.x+18,yy+31,a);
  p.hr(a.x+18,yy+51,a.w-36);
}
void systems_facts(Paint& p,const Snapshot& s,const UiState& u,Rect b,unsigned count) {
  constexpr int kDx=140;   // value column: wide enough for "INTEGRITY VALIDATED" and "10 SYS  3 TG  5 UNITS" inside the panel
  p.fillRect(b.x+10,b.y+90,b.w-20,300,kPanel);
  p.fact("RF LOCK",evidence_name(s.evidence),b.x+18,b.y+110,b,kWhite,kDx);
  if(s.db_available) {
    char t[40];std::snprintf(t,sizeof(t),"%u SYS  %u TG  %u UNITS",static_cast<unsigned>(s.db_systems),
                             static_cast<unsigned>(s.db_talkgroups),static_cast<unsigned>(s.db_subscribers));
    p.fact("DATABASE",t,b.x+18,b.y+156,b,kGreen,kDx);
  } else p.fact("DATABASE","NOT AVAILABLE",b.x+18,b.y+156,b,kAmber,kDx);
  if(count) {
    const auto& sys=s.observed_systems[std::min<unsigned>(u.selected_system,count-1)];
    char t[24],ids[64];
    std::snprintf(t,sizeof(t),"%u",static_cast<unsigned>(sys.talkgroup_count));
    p.fact("TALKGROUPS",t,b.x+18,b.y+202,b,sys.talkgroup_count?kGreen:kMuted,kDx);
    ids[0]=0;
    for(unsigned i=0;i<sys.top_talkgroup_n&&i<kTopIds;++i){
      char one[16];std::snprintf(one,sizeof(one),"%s%lu",i?"  ":"",static_cast<unsigned long>(sys.top_talkgroups[i]));
      std::strncat(ids,one,sizeof(ids)-std::strlen(ids)-1);}
    if(ids[0])p.label(ids,b.x+18,b.y+228,b);
    std::snprintf(t,sizeof(t),"%u",static_cast<unsigned>(sys.subscriber_count));
    p.fact("UNITS",t,b.x+18,b.y+266,b,sys.subscriber_count?kGreen:kMuted,kDx);
    ids[0]=0;
    for(unsigned i=0;i<sys.top_subscriber_n&&i<kTopIds;++i){
      char one[16];std::snprintf(one,sizeof(one),"%s%lu",i?"  ":"",static_cast<unsigned long>(sys.top_subscribers[i]));
      std::strncat(ids,one,sizeof(ids)-std::strlen(ids)-1);}
    if(ids[0])p.label(ids,b.x+18,b.y+292,b);
  }
  p.button("SAVE CHANNEL",{b.x+18,b.y+334,242,48},s.can_save_channel);
}
void systems(Paint& p,const Snapshot& s,const UiState& u) {
  const auto mode=u.density;
  const auto a=mode==Density::discovery_first?layout::kBasicHero:layout::kAdvancedMain;
  const auto b=mode==Density::discovery_first?layout::kBasicFacts:layout::kAdvancedFacts;
  const unsigned count=sys_count(s);
  widget(p,kSlotSysFrame,kBg,[&]{
    p.card(a);p.card(b);
    p.title("OBSERVED SYSTEMS",a.x+18,a.y+32,a);
    if (!s.systems_available) p.text("SYSTEM LEARNING NOT INTEGRATED",a.x+18,a.y+117,kAmber,2,a);
    else if (count==0) p.text("NO SYSTEMS OBSERVED",a.x+18,a.y+117,kMuted,2,a);
  });
  for(unsigned i=0;i<kObservedSystems;++i) {
    if(i<count) widget(p,kSlotSysRow+i,kPanel,[&]{systems_row(p,s,u,a,i);});
    else if(p.diff&&p.diff->used[kSlotSysRow+i]) {          // a row that went away: clear it, forget it
      p.g.fillRect(a.x+11,a.y+93+static_cast<int>(i)*76-17,a.w-22,70,kPanel);p.diff->reset_slot(kSlotSysRow+i);
    }
  }
  widget(p,kSlotSysTail,kPanel,[&]{
  if(s.systems_available && s.db_systems>kObservedSystems) {
    const unsigned first=u.system_offset;
    p.button("PREV",{a.x+18,a.y+384,94,40},first>0);
    p.button("NEXT",{a.x+124,a.y+384,94,40},static_cast<size_t>(first)+kObservedSystems<s.db_systems);
    char r[40];std::snprintf(r,sizeof(r),"%u-%u OF %u",first+1,first+count,static_cast<unsigned>(s.db_systems));
    p.label(r,a.x+236,a.y+404,a);
  }
  p.label("Directory matches never become RF evidence",a.x+18,a.y+432,a);
  p.title("SAVE / DISCOVERY",b.x+18,b.y+32,b);
  p.label("No guessed repeater locations",b.x+18,b.y+422,b);
  });
  widget(p,kSlotSysFacts,kPanel,[&]{systems_facts(p,s,u,b,count);});
}
uint32_t sys_shape(const Snapshot& s,const UiState& u,unsigned n) {
  return static_cast<uint32_t>(n)|(s.systems_available?0x10u:0u)|(static_cast<uint32_t>(u.system_offset)<<8)|
         (static_cast<uint32_t>(s.db_systems)<<16);
}
void systems_sigs(const Snapshot& s,const UiState& u,uint32_t* rows,uint32_t& facts,uint32_t& shape) {
  const unsigned n=sys_count(s);
  shape=sys_shape(s,u,n);
  for(unsigned i=0;i<kObservedSystems;++i) rows[i]=i<n?sys_row_sig(s.observed_systems[i],i==u.selected_system):0;
  facts=sys_facts_sig(s,u,n);
}
// Systems page after the first paint: repaint only the rows and the right panel whose content changed.
void systems_update(Paint& p,const Snapshot& s,const UiState& u,uint32_t* rows,uint32_t& facts,uint32_t& shape) {
  const unsigned n=sys_count(s);
  const uint32_t now_shape=sys_shape(s,u,n);
  if(now_shape!=shape) {   // rows appeared or went away: the structure changed, repaint the page once
    systems(p,s,u);
    systems_sigs(s,u,rows,facts,shape);
    return;
  }
  const auto a=u.density==Density::discovery_first?layout::kBasicHero:layout::kAdvancedMain;
  const auto b=u.density==Density::discovery_first?layout::kBasicFacts:layout::kAdvancedFacts;
  for(unsigned i=0;i<n;++i) {
    const uint32_t sig=sys_row_sig(s.observed_systems[i],i==u.selected_system);
    if(sig!=rows[i]) {widget(p,kSlotSysRow+i,kPanel,[&]{systems_row(p,s,u,a,i);});rows[i]=sig;}
  }
  const uint32_t fs=sys_facts_sig(s,u,n);
  if(fs!=facts) {widget(p,kSlotSysFacts,kPanel,[&]{systems_facts(p,s,u,b,n);});facts=fs;}
}
// ---- DISCOVER page (reached from SCAN): the bands that will be swept, progress, what was found, SAVE TO LIST ----
struct DiscGeom {Rect a,b,prog,result,start,save,back;};
DiscGeom disc_geom(Density d) {
  DiscGeom g{};
  g.a=d==Density::discovery_first?layout::kBasicHero:layout::kAdvancedMain;
  g.b=d==Density::discovery_first?layout::kBasicFacts:layout::kAdvancedFacts;
  g.prog={g.a.x+14,g.a.y+222,g.a.w-28,146};
  g.result={g.a.x+14,g.a.y+372,g.a.w-28,34};
  g.start={g.a.x+24,g.a.y+419,230,44};
  g.save={g.a.x+264,g.a.y+419,200,44};
  g.back={g.a.x+474,g.a.y+419,120,44};
  return g;
}
void disc_mhz(char (&out)[32],uint32_t hz) {
  std::snprintf(out,sizeof(out),"%lu.%06lu MHz",static_cast<unsigned long>(hz/1000000u),static_cast<unsigned long>(hz%1000000u));
}
bool disc_can_save(const Snapshot& s) {return !s.discover_running && s.discover_found>0 && s.can_save_channel;}
void disc_progress(Paint& p,const Snapshot& s,const DiscGeom& g) {
  const Rect r=g.prog;
  p.fillRect(r.x,r.y,r.w,r.h,kPanel);
  const bool run=s.discover_running;
  const bool sv=run&&s.discover_phase==1;
  const bool dec=run&&s.discover_phase==2;
  p.text(sv?"SURVEYING":dec?"DECODING":run?"DISCOVERING":"READY",r.x+6,r.y+20,run?kCyan:kWhite,3,r);
  char f[48];
  if(sv&&s.survey_center_hz){char m[32];disc_mhz(m,s.survey_center_hz);std::snprintf(f,sizeof(f),"NOW  %s",m);}
  else if(run&&s.scan.current_frequency_valid){char m[32];disc_mhz(m,s.scan.current_frequency_hz);std::snprintf(f,sizeof(f),"NOW  %s",m);}
  else std::snprintf(f,sizeof(f),run?"NOW  TUNING":"NOW  -");
  p.text(f,r.x+6,r.y+58,kWhite,2,r);
  const Rect bar{r.x+6,r.y+80,r.w-12,16};
  p.fillRect(bar.x,bar.y,bar.w,bar.h,kEdge);
  p.fillRect(bar.x+1,bar.y+1,bar.w-2,bar.h-2,kBg);
  const long num=sv?static_cast<long>(s.survey_done):(run&&s.scan.planned)?static_cast<long>(s.scan.channel_index):0;
  const long den=sv?static_cast<long>(s.survey_total):static_cast<long>(s.scan.planned);
  if(num>0&&den>0) {
    const long w=num*(bar.w-2)/den;
    if(w>0)p.fillRect(bar.x+1,bar.y+1,static_cast<int>(std::min<long>(w,bar.w-2)),bar.h-2,kCyan);
  }
  char t[96];
  if(sv)std::snprintf(t,sizeof(t),"WINDOW %u / %u   %u.%02u MS/S   PASS %u",static_cast<unsigned>(s.survey_done),static_cast<unsigned>(s.survey_total),
                      static_cast<unsigned>(s.survey_rate_khz/1000u),static_cast<unsigned>((s.survey_rate_khz%1000u)/10u),static_cast<unsigned>(s.survey_passes+1));
  else if(dec)std::snprintf(t,sizeof(t),"CH %u / %u OF THE OCCUPIED   PASS %u",static_cast<unsigned>(s.scan.channel_index),static_cast<unsigned>(s.scan.planned),static_cast<unsigned>(s.survey_passes));
  else if(run&&s.scan.planned)std::snprintf(t,sizeof(t),"CH %u / %u   PASS %u   PLAIN SWEEP",static_cast<unsigned>(s.scan.channel_index),static_cast<unsigned>(s.scan.planned),static_cast<unsigned>(s.scan.passes));
  else std::snprintf(t,sizeof(t),"%u CH IN THE PICKED BANDS",static_cast<unsigned>(s.band_channels));
  p.label(t,r.x+6,r.y+112,r);
  if(sv||dec)std::snprintf(t,sizeof(t),"SIGNALS %u   FOUND %u   CANDIDATES %u",static_cast<unsigned>(s.survey_hot),static_cast<unsigned>(s.discover_found),static_cast<unsigned>(s.discover_candidates));
  else std::snprintf(t,sizeof(t),"FOUND %u   CANDIDATES %u",static_cast<unsigned>(s.discover_found),static_cast<unsigned>(s.discover_candidates));
  p.text(t,r.x+6,r.y+134,s.discover_found?kGreen:kMuted,2,r);
}
void disc_result(Paint& p,const Snapshot& s,const DiscGeom& g) {
  const Rect r=g.result;
  p.fillRect(r.x,r.y,r.w,r.h,kPanel);
  char t[96];
  if(s.band_mask==0)p.text("NO BANDS PICKED: CHOOSE THEM IN SETUP",r.x+6,r.y+r.h/2,kAmber,2,r);
  else if(s.discover_saved||s.discover_skipped) {
    std::snprintf(t,sizeof(t),"SAVED %u TO LIST%s",static_cast<unsigned>(s.discover_saved),s.discover_skipped?"   LIST FULL: SOME SKIPPED":"");
    p.text(t,r.x+6,r.y+r.h/2,s.discover_skipped?kAmber:kGreen,2,r);
  } else p.label("A channel counts as found after 3 sightings",r.x+6,r.y+r.h/2,r);
}
void disc_buttons(Paint& p,const Snapshot& s,const DiscGeom& g) {
  const bool run=s.discover_running;
  p.button(run?"STOP DISCOVERY":"START DISCOVERY",g.start,run||s.discover_can_start);
  p.button("SAVE TO LIST",g.save,disc_can_save(s));
  p.button("BACK",g.back,true);
}
void disc_row(Paint& p,const Snapshot& s,const DiscGeom& g,unsigned i) {
  const Rect rr{g.b.x+10,g.b.y+54+static_cast<int>(i)*76,g.b.w-20,70};
  p.fillRoundRect(rr.x,rr.y,rr.w,rr.h,5,kPanel);
  if(!s.systems_available||i>=sys_count(s)) {
    if(i==0)p.label("NO SYSTEMS YET",rr.x+10,rr.y+rr.h/2,rr);
    return;
  }
  const auto& sys=s.observed_systems[i];
  const bool verified=sys.evidence>=Evidence::integrity_valid;
  char area[16];area_text(area,sys.protocol,sys.colour_code,sys.colour_code_valid,sys.ran,sys.ran_valid);
  char t[64];
  std::snprintf(t,sizeof(t),"%s  %s",protocol_name(sys.protocol),(area[0]&&verified)?area:"CC?");
  p.text(t,rr.x+10,rr.y+18,verified?kGreen:kAmber,2,rr);
  if(sys.has_frequency){char m[32];disc_mhz(m,sys.frequency_hz);std::snprintf(t,sizeof(t),"%s  HEARD %lu",m,static_cast<unsigned long>(sys.heard_count));}
  else std::snprintf(t,sizeof(t),"FREQUENCY UNKNOWN");
  p.label(t,rr.x+10,rr.y+46,rr);
}
uint32_t disc_frame_sig(const Snapshot& s) {
  uint32_t h=2166136261u;
  h=fnv_val(h,s.band_mask);h=fnv_val(h,s.band_channels);h=fnv_val(h,s.band_minutes);
  return h;
}
uint32_t disc_progress_sig(const Snapshot& s) {
  uint32_t h=2166136261u;
  h=fnv_val(h,s.discover_running);h=fnv_val(h,s.scan.current_frequency_valid);h=fnv_val(h,s.scan.current_frequency_hz);
  h=fnv_val(h,s.scan.channel_index);h=fnv_val(h,s.scan.planned);h=fnv_val(h,s.scan.passes);
  h=fnv_val(h,s.discover_found);h=fnv_val(h,s.discover_candidates);
  h=fnv_val(h,s.discover_phase);h=fnv_val(h,s.survey_done);h=fnv_val(h,s.survey_total);h=fnv_val(h,s.survey_hot);
  h=fnv_val(h,s.survey_passes);h=fnv_val(h,s.survey_rate_khz);h=fnv_val(h,s.survey_center_hz);
  return h;
}
uint32_t disc_button_sig(const Snapshot& s) {
  uint32_t h=2166136261u;
  h=fnv_val(h,s.discover_running);h=fnv_val(h,s.discover_can_start);h=fnv_val(h,disc_can_save(s));
  h=fnv_val(h,s.discover_saved);h=fnv_val(h,s.discover_skipped);h=fnv_val(h,s.band_mask);
  return h;
}
void discover(Paint& p,const Snapshot& s,const UiState& u) {
  const DiscGeom g=disc_geom(u.density);
  widget(p,kSlotDiscFrame,kBg,[&]{
    p.card(g.a);p.card(g.b);
    p.title("DISCOVER SYSTEMS",g.a.x+18,g.a.y+32,g.a);
    p.label("BANDS (CHANGE THEM IN SETUP)",g.a.x+18,g.a.y+66,g.a);
    for(unsigned i=0;i<dv::kBandPresetCount;++i) {
      const bool on=(s.band_mask>>i)&1U;
      char t[40];std::snprintf(t,sizeof(t),"[%c] %s",on?'X':' ',dv::kBandPresets[i].label);
      p.text(t,g.a.x+18+static_cast<int>(i%3)*260,g.a.y+98+static_cast<int>(i/3)*30,on?kGreen:kMuted,2,g.a);
    }
    p.title("NEWEST SYSTEMS",g.b.x+18,g.b.y+32,g.b);
  });
  widget(p,kSlotDiscProgress,kPanel,[&]{disc_progress(p,s,g);});
  widget(p,kSlotDiscResult,kPanel,[&]{disc_result(p,s,g);});
  widget(p,kSlotDiscButtons,kBg,[&]{disc_buttons(p,s,g);});
  for(unsigned i=0;i<kObservedSystems;++i)widget(p,kSlotDiscRow+i,kPanel,[&]{disc_row(p,s,g,i);});
}
void discover_sigs(const Snapshot& s,uint32_t* sig) {
  sig[0]=disc_frame_sig(s);sig[1]=disc_progress_sig(s);sig[2]=disc_button_sig(s);
  const unsigned n=sys_count(s);
  for(unsigned i=0;i<kObservedSystems;++i)sig[3+i]=i<n?sys_row_sig(s.observed_systems[i],false):0;
}
void discover_update(Paint& p,const Snapshot& s,const UiState& u,uint32_t* sig) {
  uint32_t now[7];discover_sigs(s,now);
  if(now[0]!=sig[0]) {discover(p,s,u);std::memcpy(sig,now,sizeof(now));return;}   // the band list changed: repaint the page once
  const DiscGeom g=disc_geom(u.density);
  if(now[1]!=sig[1]) {widget(p,kSlotDiscProgress,kPanel,[&]{disc_progress(p,s,g);});sig[1]=now[1];}
  if(now[2]!=sig[2]) {widget(p,kSlotDiscResult,kPanel,[&]{disc_result(p,s,g);});widget(p,kSlotDiscButtons,kBg,[&]{disc_buttons(p,s,g);});sig[2]=now[2];}
  for(unsigned i=0;i<kObservedSystems;++i)
    if(now[3+i]!=sig[3+i]) {widget(p,kSlotDiscRow+i,kPanel,[&]{disc_row(p,s,g,i);});sig[3+i]=now[3+i];}
}
void signal(Paint& p,const Snapshot& s,Density mode) {
  const auto a=mode==Density::discovery_first?layout::kBasicHero:layout::kAdvancedMain;
  const auto b=mode==Density::discovery_first?layout::kBasicFacts:layout::kAdvancedFacts;
  p.card(a);p.card(b);
  p.title("DECODER / IQ DIAGNOSTICS",a.x+18,a.y+32,a);
  if (!s.diagnostics_valid) {
    p.text("NO VERIFIED MEASUREMENTS",a.x+18,a.y+135,kAmber,2,a);
    p.label("No fabricated SNR / BER / dBm",a.x+18,a.y+184,a);
  } else {
    char v[36];
    number(v,s.sync_count);p.fact("SYNC EVENTS",v,a.x+18,a.y+108,a);
    number(v,s.integrity_frames);p.fact("INTEGRITY FRAMES",v,a.x+18,a.y+161,a);
    number(v,s.fec_corrected);p.fact("FEC CORRECTIONS",v,a.x+18,a.y+214,a);
    number(v,s.crc_failures);p.fact("CRC FAILURES",v,a.x+18,a.y+267,a);
    number(v,s.iq_drops);p.fact("IQ DISCONTINUITY",v,a.x+18,a.y+320,a,s.iq_drops?kAmber:kGreen);
  }
  p.title("RF LEVEL",b.x+18,b.y+32,b);
  if(s.relative_level_valid) {
    char v[36];std::snprintf(v,sizeof(v),"%ld.%01ld REL dB",static_cast<long>(s.relative_level_tenths_db/10),
      static_cast<long>(s.relative_level_tenths_db<0?-s.relative_level_tenths_db%10:s.relative_level_tenths_db%10));
    p.text(v,b.x+18,b.y+108,kWhite,2,b);
    p.label("UNCALIBRATED / RELATIVE ONLY",b.x+18,b.y+153,b);
  } else {
    p.text("RF LEVEL NOT MEASURED",b.x+18,b.y+108,kAmber,2,b);
  }
  p.hr(b.x+18,b.y+200,b.w-36);
  p.label("No synthetic waterfall",b.x+18,b.y+250,b);
  p.label("Unknown values remain unknown",b.x+18,b.y+293,b);
  if((s.protocol==Protocol::nxdn48||s.protocol==Protocol::nxdn96) && s.nxdn.cac_metrics_valid) {
    char v[36];number(v,s.nxdn.cac_crc_valid);
    p.fact("CAC CRC OK",v,b.x+18,b.y+354,b,kGreen);
  }
  p.label(origin_name(s.origin),b.x+18,b.y+420,b);
}
Rect band_button(Rect b,unsigned i) {return {b.x+14+static_cast<int>(i%2)*206,b.y+60+static_cast<int>(i/2)*52,198,44};}
void setup(Paint& p,const Snapshot& s,Density mode) {
  const auto a=mode==Density::discovery_first?layout::kBasicHero:layout::kAdvancedMain;
  const auto b=mode==Density::discovery_first?layout::kBasicFacts:layout::kAdvancedFacts;
  p.card(a);p.card(b);
  p.title("RECEIVE CONTROLS",a.x+18,a.y+32,a);
  p.fact("FREQUENCY","EXTERNAL RECEIVER OWNER",a.x+18,a.y+102,a);
  p.fact("PROTOCOL","SELECTOR NOT INTEGRATED",a.x+18,a.y+155,a,kAmber);
  p.fact("SCAN PLAN",s.can_scan?"AVAILABLE":"NOT AVAILABLE",a.x+18,a.y+208,a,s.can_scan?kGreen:kAmber);
  p.fact("HOLD / FILTER",s.can_filter?"AVAILABLE":"NOT AVAILABLE",a.x+18,a.y+261,a,s.can_filter?kGreen:kAmber);
  {
    char t[48];
    const char* pn=s.monitor_proto==1?"DMR":s.monitor_proto==2?"NXDN48":s.monitor_proto==3?"NXDN96":"ALL";
    if(!s.can_scan)std::snprintf(t,sizeof(t),"MONITOR: NOT CONNECTED");
    else std::snprintf(t,sizeof(t),"MONITOR: %s (%u)",pn,static_cast<unsigned>(s.monitor_entries));
    p.button(t,{a.x+18,a.y+322,370,44},s.can_scan,s.monitor_selected);
  }
  p.label("Tap to cycle; START on SCAN",a.x+18,a.y+440,a);
  p.button(mode==Density::discovery_first?"DENSITY: DISCOVERY":"DENSITY: ANALYSIS",
           {a.x+18,a.y+376,370,44},true);
  {
    char t[48];
    if(!s.can_save_speech)std::snprintf(t,sizeof(t),"SAVE DMR SPEECH: N/A");
    else if(s.save_speech)std::snprintf(t,sizeof(t),"SAVE DMR SPEECH: ON (%u)",static_cast<unsigned>(s.speech_files));
    else std::snprintf(t,sizeof(t),"SAVE DMR SPEECH: OFF");
    p.button(t,{a.x+408,a.y+322,376,44},s.can_save_speech,s.save_speech);
  }
  {
    char t[48];
    if(!s.can_save_speech)std::snprintf(t,sizeof(t),"SAVE DMR IQ (DEV): N/A");
    else if(s.save_iq)std::snprintf(t,sizeof(t),"SAVE DMR IQ (DEV): ON (%u)",static_cast<unsigned>(s.iq_files));
    else std::snprintf(t,sizeof(t),"SAVE DMR IQ (DEV): OFF");
    p.button(t,{a.x+408,a.y+376,376,44},s.can_save_speech,s.save_iq);
  }
  p.label("Speech: decoded clear calls only",a.x+408,a.y+440,a);
  p.title("SCAN BANDS (MY BANDS)",b.x+18,b.y+32,b);
  for(unsigned i=0;i<dv::kBandPresetCount;++i)
    p.button(dv::kBandPresets[i].label,band_button(b,i),true,(s.band_mask>>i)&1U);
  {
    char t[64];
    std::snprintf(t,sizeof(t),"%u CH  ~%u MIN PER PASS",static_cast<unsigned>(s.band_channels),static_cast<unsigned>(s.band_minutes));
    p.label(t,b.x+18,b.y+346,b);
    p.label("SCAN > PLAN > MY BANDS",b.x+18,b.y+384,b);
  }
  p.text("RX ONLY  /  NO TX",b.x+18,b.y+420,kCyan,2,b);
}
void page(Paint& p,const Snapshot& s,const UiState& u) {
  switch(u.view) {
    case View::explore:break;   // the SCAN view is retained widgets (ScanView), never repainted as a page
    case View::live:
      if(u.density==Density::discovery_first)live_discovery(p,s);
      else live_analysis(p,s);
      break;
    case View::last_heard:last_heard(p,s,u.density,u.history_offset,u.selected_history);break;
    case View::systems:systems(p,s,u);break;
    case View::discover:discover(p,s,u);break;
    case View::signal:signal(p,s,u.density);break;
    case View::setup:setup(p,s,u.density);break;
    default:break;
  }
}
} // namespace

void Dashboard::PageBuffers::release() {valid=false;}
bool Dashboard::PageBuffers::ensure_diff() {
  if(diff.h) return true;
  const uint16_t cap=static_cast<uint16_t>(paint::kSlots*paint::kSlotOps);
#if defined(ESP_PLATFORM)
  auto mem=[](size_t n)->void* {return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);};
#else
  auto mem=[](size_t n)->void* {return std::malloc(n);};
#endif
  auto* h=static_cast<uint32_t*>(mem(cap*sizeof(uint32_t)));
  auto* w=static_cast<uint16_t*>(mem(cap*sizeof(uint16_t)));
  auto* xs=static_cast<int16_t*>(mem(cap*sizeof(int16_t)));
  auto* ys=static_cast<int16_t*>(mem(cap*sizeof(int16_t)));
  if(!h||!w||!xs||!ys) {std::free(h);std::free(w);std::free(xs);std::free(ys);return false;}
  diff.h=h;diff.w=w;diff.xs=xs;diff.ys=ys;diff.cap=cap;diff.reset();
  return true;
}
void Dashboard::PageBuffers::free_diff() {
  std::free(diff.h);std::free(diff.w);std::free(diff.xs);std::free(diff.ys);
  diff=paint::Diff{};
}
// Pages other than SCAN and SYSTEMS: a dry run of the page folds everything it would draw into a signature. A revision that
// changed nothing this page shows (a scan stage tick, a counter on another page) repaints nothing at all; a real change repaints
// the page. (An off-screen sprite with a changed-band push was tried first: on the Tab5 it cost 70 to 180 ms per repaint and
// 1.8 s for the first one, so it was dropped.)
void Dashboard::present_page(lgfx::LovyanGFX& display,const Snapshot& s,const UiState& state) {
  uint32_t sig=2166136261u;
  {
    Paint dry{display,&sig};
    page(dry,s,state);
  }
  if(pb_.valid && sig==pb_.sig) return;
  Paint p{display};
  if(pb_.ensure_diff()) p.diff=&pb_.diff;               // the page repaints only what changed since it was last drawn
  widget(p,kSlotPage,kBg,[&]{page(p,s,state);});
  pb_.sig=sig;
  pb_.valid=true;
}

bool Dashboard::present(lgfx::LovyanGFX& display,const Snapshot& s,
                        const UiState& state,bool owns_screen,bool overlay_open) {
  if(!owns_screen||overlay_open||state.view>=View::count)return false;
  const bool entry = !ready_ || last_state_.view!=state.view || last_state_.density!=state.density;
  // A page other than SCAN whose only news is a faster-than-readable value change (a heard counter, a level) repaints at most every 250 ms.
  // Anything the user did (a view, a selection, a page) changes the UI state and is drawn at once.
  if(!entry && state.view!=View::explore && old_.revision!=s.revision &&
     last_state_.history_offset==state.history_offset && last_state_.selected_history==state.selected_history &&
     last_state_.selected_scan_hit==state.selected_scan_hit && last_state_.selected_system==state.selected_system &&
     last_state_.system_offset==state.system_offset && static_cast<uint32_t>(millis()-last_paint_ms_)<250u) return true;
  if(!entry && old_.revision==s.revision &&
     last_state_.history_offset==state.history_offset && last_state_.selected_history==state.selected_history &&
     last_state_.selected_scan_hit==state.selected_scan_hit && last_state_.selected_system==state.selected_system &&
     last_state_.system_offset==state.system_offset) {
    if(old_.audio_revision!=s.audio_revision) {
      if(static_cast<uint32_t>(millis()-last_paint_ms_)<120u) return true;   // a fast counter or level: at most about 8 repaints a second
      last_paint_ms_=millis();
      // Only the voice panel changed: the LIVE page repaints, and its diff painting draws only that panel's changed values.
      if(state.view==View::live) present_page(display,s,state);
      old_.voice=s.voice;old_.audio_revision=s.audio_revision;
    }
    return true; // idle means zero drawing
  }
  Paint p{display};
  if(pb_.ensure_diff()) p.diff=&pb_.diff;       // every widget below draws only what changed since it was last drawn
  if(entry) {
    // Screen owner already painted shared audio/header chrome. Never touch y < 104.
    display.fillRect(0,104,1280,616,kBg);
    nav(p,state);
    scan_.reset();
    sys_ready_=false;
    disc_ready_=false;
    pb_.valid=false;                // the screen was just cleared: the next page push covers everything
    pb_.diff.reset();               // ... and nothing that was drawn before is on it any more
  }
  // The context strip shows only the origin and the density: repaint it when one of those changed, not on every revision.
  const uint32_t ctx=static_cast<uint32_t>(s.origin)|(static_cast<uint32_t>(state.density)<<8)|0x10000u;
  if(entry||ctx!=ctx_sig_) {context(p,s,state.density);ctx_sig_=ctx;}
  if(state.view==View::explore) {
    if(entry)scan_.enter(display,s,state);
    else scan_.update(display,s,state);          // changed widgets only, each inside its own rectangle
  } else if(state.view==View::discover && disc_ready_) {
    discover_update(p,s,state,disc_sig_);                               // changed parts only
  } else if(state.view==View::systems && sys_ready_) {
    systems_update(p,s,state,sys_row_sig_,sys_facts_sig_,sys_shape_);   // changed rows / panel only
  } else {
    if(state.view==View::systems||state.view==View::discover) page(p,s,state);   // first paint; later changes go through systems_update() / discover_update()
    else present_page(display,s,state);              // Live, Last Heard, Signal, Setup: only when what they show changes
    if(state.view==View::systems) {systems_sigs(s,state,sys_row_sig_,sys_facts_sig_,sys_shape_);sys_ready_=true;}
    if(state.view==View::discover) {discover_sigs(s,disc_sig_);disc_ready_=true;}
  }
  last_state_=state;
  old_=s;
  ready_=true;
  last_paint_ms_=millis();
  return true;
}
Action Dashboard::handle_touch(int32_t x,int32_t y,const Snapshot& s,
                               const UiState& state) const {
  if (!ready_)return {};
  for (int i=0;i<layout::kTabCount;++i)
    if(layout::tab(i).contains(x,y))return {ActionKind::switch_view,static_cast<uint8_t>(i)};
  if(state.view==View::explore)return scan_.touch(x,y,s,state);
  if(state.view==View::live&&s.voice.available) {
    const Rect vr=voice_rect(state.density);
    for(int i=0;i<4;++i)
      if(listen_button(vr,i).contains(x,y))return {ActionKind::set_listen,static_cast<uint8_t>(i)};
  }
  if(state.view==View::systems) {
    const auto b=state.density==Density::discovery_first?layout::kBasicFacts:layout::kAdvancedFacts;
    if(Rect{b.x+18,b.y+334,242,48}.contains(x,y)&&s.can_save_channel)
      return {ActionKind::save_channel};
  }
  if(state.view==View::setup) {
    const auto a=state.density==Density::discovery_first?layout::kBasicHero:layout::kAdvancedMain;
    if(Rect{a.x+18,a.y+376,370,44}.contains(x,y))return {ActionKind::toggle_density};
    if(s.can_scan&&Rect{a.x+18,a.y+322,370,44}.contains(x,y))return {ActionKind::monitor_filter};
    if(s.can_save_speech&&Rect{a.x+408,a.y+322,376,44}.contains(x,y))return {ActionKind::toggle_save_speech};
    if(s.can_save_speech&&Rect{a.x+408,a.y+376,376,44}.contains(x,y))return {ActionKind::toggle_save_iq};
    {
      const auto b=state.density==Density::discovery_first?layout::kBasicFacts:layout::kAdvancedFacts;
      for(unsigned i=0;i<dv::kBandPresetCount;++i)
        if(band_button(b,i).contains(x,y))return {ActionKind::toggle_band,static_cast<uint8_t>(i)};
    }
  }
  if(state.view==View::discover) {
    const DiscGeom g=disc_geom(state.density);
    if(g.start.contains(x,y)&&(s.discover_running||s.discover_can_start))
      return {s.discover_running?ActionKind::discover_stop:ActionKind::discover_start,0};
    if(g.save.contains(x,y)&&disc_can_save(s))return {ActionKind::discover_save,0};
    if(g.back.contains(x,y))return {ActionKind::close_discover,0};
    return {};
  }
  if(state.view==View::systems && s.systems_available) {
    const auto a=state.density==Density::discovery_first?layout::kBasicHero:layout::kAdvancedMain;
    if(s.db_systems>kObservedSystems) {
      if(Rect{a.x+18,a.y+384,94,40}.contains(x,y) && state.system_offset>0)
        return {ActionKind::page_systems,0};
      if(Rect{a.x+124,a.y+384,94,40}.contains(x,y) &&
         static_cast<size_t>(state.system_offset)+kObservedSystems<s.db_systems)
        return {ActionKind::page_systems,1};
    }
    const unsigned n=std::min<unsigned>(s.observed_system_count,kObservedSystems);
    for(unsigned i=0;i<n;++i)
      if(Rect{a.x+12,a.y+66+static_cast<int>(i)*76,a.w-24,68}.contains(x,y))
        return {ActionKind::select_system,static_cast<uint8_t>(i)};
  }
  if(state.view==View::last_heard && s.last_heard_available) {
    const Rect a=state.density==Density::discovery_first?layout::kBasicHero:layout::kAdvancedMain;
    if (s.history_count>6) {
      if(Rect{a.x+18,a.y+418,94,42}.contains(x,y) && state.history_offset>0)
        return {ActionKind::page_history,0};
      if(Rect{a.x+124,a.y+418,94,42}.contains(x,y) &&
         static_cast<size_t>(state.history_offset)+6<s.history_count)
        return {ActionKind::page_history,1};
    }
    for(int i=0;i<6;++i) {
      if(static_cast<int>(state.history_offset)+i>=s.history_count)break;
      if(Rect{a.x+12,a.y+96+i*52,a.w-24,46}.contains(x,y))
        return {ActionKind::selected_history,static_cast<uint8_t>(state.history_offset+i)};
    }
  }
  return {};
}
} // namespace orcsdr::digital_voice_ui
