#include "digital_voice_dashboard.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace orcsdr::digital_voice_ui {
namespace {
using layout::Rect;
// Existing OrcSDR RGB565 palette, taken from main's AIS/Dead Air source.
constexpr uint16_t kBg=TFT_BLACK, kPanel=0x0841, kEdge=0x2945, kCyan=0x2e7f,
                   kGreen=0x6fe8, kMuted=0x8c71, kAmber=0xfd20, kRed=0xf800,
                   kWhite=TFT_WHITE, kSelected=0x1264;
constexpr const char* kTabs[]={"SCAN","LIVE","LAST HEARD","SYSTEMS","SIGNAL","SETUP"};
static_assert(sizeof(kTabs)/sizeof(kTabs[0]) == layout::kTabCount);

struct Paint {
  lgfx::LovyanGFX& g;
  void text(const char* s, int x, int y, uint16_t color=kWhite, int size=2,
            Rect clip={20,104,1240,616}) {
    if (!s) return;
    g.setClipRect(clip.x,clip.y,clip.w,clip.h);
    g.setFont(nullptr);
    g.setTextSize(size);
    g.setTextDatum(middle_left);
    g.setTextColor(color);
    g.drawString(s,x,y);
    g.clearClipRect();
  }
  void card(Rect r) {
    g.fillRoundRect(r.x,r.y,r.w,r.h,9,kPanel);
    g.drawRoundRect(r.x,r.y,r.w,r.h,9,kEdge);
  }
  void label(const char* s,int x,int y,Rect c={20,104,1240,616}) {
    text(s,x,y,kMuted,2,c);
  }
  void title(const char* s,int x,int y,Rect c={20,104,1240,616}) {
    text(s,x,y,kCyan,2,c);
  }
  void hr(int x,int y,int width) {g.drawFastHLine(x,y,width,kEdge);}
  void button(const char* label,Rect r,bool enabled,bool selected=false) {
    g.fillRoundRect(r.x,r.y,r.w,r.h,7,selected?kSelected:kPanel);
    g.drawRoundRect(r.x,r.y,r.w,r.h,7,!enabled?kEdge:selected?kGreen:kCyan);
    text(label,r.x+10,r.y+r.h/2,!enabled?kMuted:selected?kGreen:kWhite,2,r);
  }
  void fact(const char* key,const char* value,int x,int y,Rect clip,uint16_t value_color=kWhite) {
    text(key,x,y,kMuted,2,clip);
    text(value,x+190,y,value_color,2,clip);
  }
};

void freq(char (&out)[32], const Snapshot& s) {
  const bool current_scan=s.can_scan && s.scan.current_frequency_valid;
  if (!s.has_frequency && !current_scan) {std::snprintf(out,sizeof(out),"FREQUENCY UNKNOWN");return;}
  const uint32_t hz=s.has_frequency?s.frequency_hz:s.scan.current_frequency_hz;
  std::snprintf(out,sizeof(out),"%lu.%06lu MHz",
    static_cast<unsigned long>(hz/1000000u),
    static_cast<unsigned long>(hz%1000000u));
}
void number(char (&out)[36],uint32_t n) {std::snprintf(out,sizeof(out),"%lu",static_cast<unsigned long>(n));}
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
  p.g.fillRoundRect(r.x,r.y,r.w,r.h,6,kPanel);
  p.g.drawRoundRect(r.x,r.y,r.w,r.h,6,kEdge);
  p.text("DIGITAL VOICE  /  RECEIVE ONLY",r.x+14,r.y+r.h/2,kCyan,2,r);
  p.text(mode==Density::discovery_first?"DISCOVERY FIRST":"ANALYSIS FIRST",r.x+540,r.y+r.h/2,kGreen,2,r);
  p.text(origin_name(s.origin),r.x+864,r.y+r.h/2,
         s.origin==Origin::live?kWhite:kAmber,2,r);
}
void nav(Paint& p,const UiState& state) {
  p.g.fillRect(0,642,1280,78,kBg);
  for(int i=0;i<layout::kTabCount;++i)
    p.button(kTabs[i],layout::tab(i),true,state.view==static_cast<View>(i));
}
bool scan_enabled(const Snapshot& s) {
  return s.can_scan && (s.receiver==Receiver::ready || s.receiver==Receiver::receiving);
}
void scan_hits(Paint& p,const Snapshot& s,const UiState& u,Rect b) {
  p.title("DETECTED / RECENT SIGNALS",b.x+18,b.y+32,b);
  if (!s.scan_hit_count) {
    p.text("NO VERIFIED SCAN HITS",b.x+18,b.y+90,kMuted,2,b);
  }
  const unsigned count=std::min<unsigned>(s.scan_hit_count,kScanHits);
  for(unsigned i=0;i<count;++i) {
    const auto& h=s.scan_hits[i];
    const int yy=b.y+87+static_cast<int>(i)*72;
    if(i==u.selected_scan_hit)p.g.fillRoundRect(b.x+10,yy-15,b.w-20,65,4,kSelected);
    p.text(protocol_name(h.protocol),b.x+18,yy,
           h.evidence>=Evidence::integrity_valid?kGreen:kAmber,2,b);
    char f[42];
    if(h.has_frequency)
      std::snprintf(f,sizeof(f),"%lu.%06lu MHz",static_cast<unsigned long>(h.frequency_hz/1000000),
                    static_cast<unsigned long>(h.frequency_hz%1000000));
    else std::snprintf(f,sizeof(f),"FREQUENCY UNKNOWN");
    p.label(f,b.x+18,yy+27,b);
    p.hr(b.x+18,yy+46,b.w-36);
  }
  p.label("Candidate != decoded identity",b.x+18,b.y+421,b);
}
void explore_discovery(Paint& p,const Snapshot& s,const UiState& u) {
  const auto a=layout::kBasicHero,b=layout::kBasicFacts;
  p.card(a);p.card(b);
  p.title("SCAN / DISCOVER",a.x+18,a.y+32,a);
  char f[32];freq(f,s);
  p.text(f,a.x+24,a.y+108,s.has_frequency?kWhite:kAmber,3,a);
  p.text(receiver_name(s.receiver),a.x+24,a.y+166,
         s.receiver==Receiver::error?kRed:kMuted,2,a);
  p.hr(a.x+20,a.y+203,a.w-40);
  p.text(evidence_name(s.evidence),a.x+24,a.y+256,
         s.evidence==Evidence::identity_valid?kGreen:s.evidence==Evidence::none?kMuted:kAmber,3,a);
  p.label(s.evidence==Evidence::identity_valid?"A valid protocol event was received":"A signal is not an identity until validated",a.x+24,a.y+316,a);
  const bool scan_ok=scan_enabled(s);
  p.text(scan_ok?scan_phase_name(s.scan.phase):"DIGITAL VOICE SCANNER NOT CONNECTED",
         a.x+24,a.y+382,scan_ok?kCyan:kAmber,2,a);
  if(scan_ok && s.scan.progress_valid && s.scan.planned) {
    char progress[52];
    std::snprintf(progress,sizeof(progress),"CHANNEL %u / %u  VERIFIED %lu",
                  static_cast<unsigned>(s.scan.visited),
                  static_cast<unsigned>(s.scan.planned),
                  static_cast<unsigned long>(s.scan.validated_hits));
    p.label(progress,a.x+395,a.y+382,a);
  }
  p.button(scan_ok && s.scan.phase==ScanPhase::scanning?"STOP SCAN":"START SCAN",
           {a.x+24,a.y+419,170,44},scan_ok);
  p.button(s.scan_on_hold?"RESUME":"HOLD",{a.x+202,a.y+419,132,44},scan_ok&&s.can_hold);
  p.button("SKIP",{a.x+342,a.y+419,106,44},scan_ok&&s.can_skip);
  p.button("SAVE",{a.x+456,a.y+419,112,44},s.can_save_channel);

  scan_hits(p,s,u,b);
}
void explore_analysis(Paint& p,const Snapshot& s) {
  const auto a=layout::kAdvancedMain,b=layout::kAdvancedFacts;
  p.card(a);p.card(b);
  p.title("SCAN / ACQUISITION",a.x+18,a.y+32,a);
  char f[32];freq(f,s);
  p.text(f,a.x+18,a.y+90,s.has_frequency?kWhite:kAmber,3,a);
  p.fact("RECEIVER",receiver_name(s.receiver),a.x+18,a.y+153,a);
  p.fact("DETECTION",evidence_name(s.evidence),a.x+18,a.y+197,a,
      s.evidence==Evidence::identity_valid?kGreen:kAmber);
  p.fact("PROTOCOL",protocol_name(s.protocol),a.x+18,a.y+241,a);
  p.hr(a.x+18,a.y+282,a.w-36);
  p.fact("SCANNER",scan_enabled(s)?scan_phase_name(s.scan.phase):"NOT CONNECTED",a.x+18,a.y+328,a);
  if (s.scan.progress_valid && s.scan.planned) {
    char progress[48];std::snprintf(progress,sizeof(progress),"%u / %u  HITS %lu",
        static_cast<unsigned>(s.scan.visited),static_cast<unsigned>(s.scan.planned),
        static_cast<unsigned long>(s.scan.validated_hits));
    p.label(progress,a.x+18,a.y+375,a);
  }
  const bool ok=scan_enabled(s);
  p.button(ok&&s.scan.phase==ScanPhase::scanning?"STOP SCAN":"START SCAN",
           {a.x+18,a.y+396,170,44},ok);
  p.button(s.scan_on_hold?"RESUME":"HOLD",{a.x+196,a.y+396,122,44},ok&&s.can_hold);
  p.button("SKIP",{a.x+326,a.y+396,98,44},ok&&s.can_skip);
  p.button("SAVE",{a.x+432,a.y+396,110,44},s.can_save_channel);

  p.title("EVIDENCE / SERVICE STATUS",b.x+18,b.y+32,b);
  p.text(integration_name(s.protocol),b.x+18,b.y+78,kAmber,2,b);
  p.fact("ORIGIN",origin_name(s.origin),b.x+18,b.y+133,b,s.origin==Origin::live?kWhite:kAmber);
  p.fact("CRC / FEC",s.diagnostics_valid?"CHECK METRICS BELOW":"NOT MEASURED",b.x+18,b.y+185,b);
  p.fact("VOICE",audio_name(s.audio),b.x+18,b.y+237,b,kAmber);
  p.hr(b.x+18,b.y+281,b.w-36);
  p.label("Integrity-checked metadata only",b.x+18,b.y+324,b);
  p.label("Unsupported decoder remains unavailable",b.x+18,b.y+368,b);
  p.label("RECEIVE ONLY  |  PROVENANCE FIRST",b.x+18,b.y+419,b);
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
  p.text(audio_name(s.audio),a.x+20,a.y+407,
         s.audio==Audio::clear_available?kGreen:kAmber,2,a);
  p.text(origin_name(s.origin),a.x+20,a.y+443,s.origin==Origin::live?kMuted:kAmber,2,a);

  protocol_facts(p,s,b,b.x+18,b.y+32);
  p.hr(b.x+18,b.y+218,b.w-36);
  p.label("ENC / PRIVACY",b.x+18,b.y+271,b);
  p.text(s.encryption_known?(s.is_encrypted?"ENCRYPTED":"CLEAR FLAG"):
         "UNKNOWN",b.x+18,b.y+310,
         s.encryption_known?(s.is_encrypted?kRed:kGreen):kAmber,2,b);
  p.label("No inference from missing encryption bits",b.x+18,b.y+416,b);
}
void live_analysis(Paint& p,const Snapshot& s) {
  const auto a=layout::kAdvancedMain,b=layout::kAdvancedFacts;
  p.card(a);p.card(b);
  call_identity(p,s,a,a.x+18,a.y+32);
  p.hr(a.x+18,a.y+203,a.w-36);
  p.fact("PROTOCOL",protocol_name(s.protocol),a.x+18,a.y+245,a);
  p.fact("VOICE",audio_name(s.audio),a.x+18,a.y+292,a,kAmber);
  p.fact("ENC",s.encryption_known?(s.is_encrypted?"ENCRYPTED":"CLEAR FLAG"):"UNKNOWN",
         a.x+18,a.y+339,a,s.encryption_known?(s.is_encrypted?kRed:kGreen):kAmber);
  p.label(origin_name(s.origin),a.x+18,a.y+426,a);
  protocol_facts(p,s,b,b.x+18,b.y+32);
  p.hr(b.x+18,b.y+215,b.w-36);
  p.title("INTEGRITY",b.x+18,b.y+252,b);
  p.fact("IDENTITY GATE",trusted_identity(s)?"PASSED":"NOT MET",b.x+18,b.y+302,b,
         trusted_identity(s)?kGreen:kAmber);
  p.fact("TAB5",integration_name(s.protocol),b.x+18,b.y+348,b,kAmber);
  p.label(s.protocol==Protocol::nxdn48||s.protocol==Protocol::nxdn96?
          "RAN alone is not proof of a site":"Unknown slot/identity stays unknown",b.x+18,b.y+424,b);
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
      char v[94];std::snprintf(v,sizeof(v),"%s  %s  %s",h.has_time?h.time_utc:"TIME ?",protocol_name(h.protocol),
         h.has_source&&h.evidence==Evidence::identity_valid?"ID VERIFIED":"ID UNKNOWN");
      if(row+offset==selected)p.g.fillRoundRect(a.x+12,y-17,a.w-24,40,4,kSelected);
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
    if ((h.protocol==Protocol::nxdn48 || h.protocol==Protocol::nxdn96) &&
        h.ran_valid && h.evidence>=Evidence::integrity_valid) {
      std::snprintf(v,sizeof(v),"%u",h.ran);
      p.fact("RAN",v,b.x+18,b.y+352,b,kGreen);
    }
  }
  if(s.history_count>6 && s.last_heard_available) {
    p.button("PREV",{a.x+18,a.y+418,94,42},offset>0);
    p.button("NEXT",{a.x+124,a.y+418,94,42},static_cast<size_t>(offset)+6<s.history_count);
  }
  p.label("No guessed times / aliases / IDs",b.x+18,b.y+421,b);
}
void systems(Paint& p,const Snapshot& s,const UiState& u) {
  const auto mode=u.density;
  const auto a=mode==Density::discovery_first?layout::kBasicHero:layout::kAdvancedMain;
  const auto b=mode==Density::discovery_first?layout::kBasicFacts:layout::kAdvancedFacts;
  p.card(a);p.card(b);
  p.title("OBSERVED SYSTEMS",a.x+18,a.y+32,a);
  const unsigned count=s.systems_available?std::min<unsigned>(s.observed_system_count,kObservedSystems):0;
  if (!s.systems_available) p.text("SYSTEM LEARNING NOT INTEGRATED",a.x+18,a.y+117,kAmber,2,a);
  else if (count==0) p.text("NO SYSTEMS OBSERVED",a.x+18,a.y+117,kMuted,2,a);
  for(unsigned i=0;i<count;++i) {
    const auto& sys=s.observed_systems[i];
    const int yy=a.y+93+static_cast<int>(i)*76;
    if(i==u.selected_system)p.g.fillRoundRect(a.x+11,yy-17,a.w-22,70,4,kSelected);
    p.text(protocol_name(sys.protocol),a.x+18,yy,
           sys.evidence>=Evidence::integrity_valid?kGreen:kAmber,2,a);
    const bool verified=sys.evidence>=Evidence::integrity_valid;
    const char* top=sys.topology_valid&&verified?nxdn_topology_name(sys.topology):"SYSTEM TYPE UNKNOWN";
    p.label(top,a.x+240,yy,a);
    char val[44];
    if(sys.has_frequency)std::snprintf(val,sizeof(val),"%lu.%06lu MHz",
        static_cast<unsigned long>(sys.frequency_hz/1000000),static_cast<unsigned long>(sys.frequency_hz%1000000));
    else std::snprintf(val,sizeof(val),"FREQUENCY UNKNOWN");
    p.label(val,a.x+18,yy+31,a);
    p.hr(a.x+18,yy+51,a.w-36);
  }
  p.label("Directory matches never become RF evidence",a.x+18,a.y+432,a);
  p.title("SAVE / DISCOVERY",b.x+18,b.y+32,b);
  p.fact("RF LOCK",evidence_name(s.evidence),b.x+18,b.y+110,b);
  p.fact("DIRECTORY","NOT LINKED",b.x+18,b.y+156,b,kAmber);
  p.fact("PROFILE","PREVIEW NOT INTEGRATED",b.x+18,b.y+202,b,kAmber);
  if(count) {
    const auto& sys=s.observed_systems[std::min<unsigned>(u.selected_system,count-1)];
    if(sys.ran_valid && sys.evidence>=Evidence::integrity_valid) {
      char value[20];std::snprintf(value,sizeof(value),"%u",sys.ran);
      p.fact("OBSERVED RAN",value,b.x+18,b.y+258,b,kGreen);
    }
  }
  p.button("SAVE CHANNEL",{b.x+18,b.y+334,242,48},s.can_save_channel);
  p.label("No guessed repeater locations",b.x+18,b.y+422,b);
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
void setup(Paint& p,const Snapshot& s,Density mode) {
  const auto a=mode==Density::discovery_first?layout::kBasicHero:layout::kAdvancedMain;
  const auto b=mode==Density::discovery_first?layout::kBasicFacts:layout::kAdvancedFacts;
  p.card(a);p.card(b);
  p.title("RECEIVE CONTROLS",a.x+18,a.y+32,a);
  p.fact("FREQUENCY","EXTERNAL RECEIVER OWNER",a.x+18,a.y+102,a);
  p.fact("PROTOCOL","SELECTOR NOT INTEGRATED",a.x+18,a.y+155,a,kAmber);
  p.fact("SCAN PLAN",s.can_scan?"AVAILABLE":"NOT AVAILABLE",a.x+18,a.y+208,a,s.can_scan?kGreen:kAmber);
  p.fact("HOLD / FILTER",s.can_filter?"AVAILABLE":"NOT AVAILABLE",a.x+18,a.y+261,a,s.can_filter?kGreen:kAmber);
  p.button(s.can_scan?"SCAN PLAN: READY":"SCAN PLAN: NOT CONNECTED",
           {a.x+18,a.y+322,370,44},false);
  p.button(mode==Density::discovery_first?"DENSITY: DISCOVERY":"DENSITY: ANALYSIS",
           {a.x+18,a.y+376,370,44},true);
  p.label("No new USB/tuning or audio owner",a.x+408,a.y+398,a);
  p.title("FEATURE READINESS",b.x+18,b.y+32,b);
  p.label("P25-I: native receive and voice",b.x+18,b.y+98,b);
  p.label("DMR: host data / no Tab5 IQ",b.x+18,b.y+140,b);
  p.label("D-STAR / YSF: host bits only",b.x+18,b.y+182,b);
  p.label("P25-II: no voice",b.x+18,b.y+224,b);
  p.label("NXDN48/96: host synthetic IQ",b.x+18,b.y+266,b);
  p.label("Shared speaker / volume via header",b.x+18,b.y+340,b);
  p.text("RX ONLY  /  NO TX",b.x+18,b.y+420,kCyan,2,b);
}
void page(Paint& p,const Snapshot& s,const UiState& u) {
  switch(u.view) {
    case View::explore:
      if(u.density==Density::discovery_first)explore_discovery(p,s,u);
      else explore_analysis(p,s);
      break;
    case View::live:
      if(u.density==Density::discovery_first)live_discovery(p,s);
      else live_analysis(p,s);
      break;
    case View::last_heard:last_heard(p,s,u.density,u.history_offset,u.selected_history);break;
    case View::systems:systems(p,s,u);break;
    case View::signal:signal(p,s,u.density);break;
    case View::setup:setup(p,s,u.density);break;
    default:break;
  }
}
} // namespace

bool Dashboard::present(lgfx::LovyanGFX& display,const Snapshot& s,
                        const UiState& state,bool owns_screen,bool overlay_open) {
  if(!owns_screen||overlay_open||state.view>=View::count)return false;
  const bool entry = !ready_ || last_state_.view!=state.view || last_state_.density!=state.density;
  if(!entry && old_.revision==s.revision &&
     last_state_.history_offset==state.history_offset && last_state_.selected_history==state.selected_history &&
     last_state_.selected_scan_hit==state.selected_scan_hit && last_state_.selected_system==state.selected_system)
    return true; // idle means zero drawing
  Paint p{display};
  if(entry) {
    // Screen owner already painted shared audio/header chrome. Never touch y < 104.
    display.fillRect(0,104,1280,616,kBg);
    nav(p,state);
  }
  // Context strip and content-card regions only; no full-screen clear on data updates.
  context(p,s,state.density);
  page(p,s,state);
  last_state_=state;
  old_=s;
  ready_=true;
  return true;
}
Action Dashboard::handle_touch(int32_t x,int32_t y,const Snapshot& s,
                               const UiState& state) const {
  if (!ready_)return {};
  for (int i=0;i<layout::kTabCount;++i)
    if(layout::tab(i).contains(x,y))return {ActionKind::switch_view,static_cast<uint8_t>(i)};
  if(state.view==View::explore || state.view==View::systems) {
    const auto b=state.density==Density::discovery_first?layout::kBasicFacts:layout::kAdvancedFacts;
    if(state.view==View::explore && state.density==Density::analysis_first) {
      const auto a=layout::kAdvancedMain;
      if(Rect{a.x+18,a.y+396,170,44}.contains(x,y) && scan_enabled(s))return {ActionKind::scan};
      if(Rect{a.x+196,a.y+396,122,44}.contains(x,y) && scan_enabled(s) && s.can_hold)return {ActionKind::toggle_hold};
      if(Rect{a.x+326,a.y+396,98,44}.contains(x,y) && scan_enabled(s) && s.can_skip)return {ActionKind::skip_frequency};
      if(Rect{a.x+432,a.y+396,110,44}.contains(x,y) && s.can_save_channel)return {ActionKind::save_channel};
    }
    if(state.view==View::explore && state.density==Density::discovery_first) {
      if(Rect{44,577,170,44}.contains(x,y) && scan_enabled(s))return {ActionKind::scan};
      if(Rect{222,577,132,44}.contains(x,y) && scan_enabled(s) && s.can_hold)return {ActionKind::toggle_hold};
      if(Rect{362,577,106,44}.contains(x,y) && scan_enabled(s) && s.can_skip)return {ActionKind::skip_frequency};
      if(Rect{476,577,112,44}.contains(x,y) && s.can_save_channel)return {ActionKind::save_channel};
      const Rect b=layout::kBasicFacts;
      const unsigned n=std::min<unsigned>(s.scan_hit_count,kScanHits);
      for(unsigned i=0;i<n;++i)
        if(Rect{b.x+12,b.y+61+static_cast<int>(i)*72,b.w-24,62}.contains(x,y))
          return {ActionKind::select_scan_hit,static_cast<uint8_t>(i)};
    }
    if(state.view==View::systems&&Rect{b.x+18,b.y+334,242,48}.contains(x,y)&&s.can_save_channel)
      return {ActionKind::save_channel};
  }
  if(state.view==View::setup) {
    const auto a=state.density==Density::discovery_first?layout::kBasicHero:layout::kAdvancedMain;
    if(Rect{a.x+18,a.y+376,370,44}.contains(x,y))return {ActionKind::toggle_density};
  }
  if(state.view==View::systems && s.systems_available) {
    const auto a=state.density==Density::discovery_first?layout::kBasicHero:layout::kAdvancedMain;
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
