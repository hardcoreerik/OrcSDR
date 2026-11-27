#pragma once
// UI-only candidate; existing Digital Voice app remains the owner of all data/actions.
#include "digital_voice_dashboard_model.hpp"
#include "digital_voice_dashboard_paint.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <cstring>

namespace orcsdr::digital_voice_ui::live_monitor {
using layout::Rect;
using namespace paint;
constexpr Rect listening{20,158,636,220}, diagnostics{668,158,592,220};
constexpr Rect spectrum{20,390,636,246}, decoder{668,390,592,70}, controls{668,472,592,164};
constexpr Rect trace{78,414,564,94}, waterfall{78,518,564,82}, symbols{1090,193,156,168};
constexpr Rect hold{680,501,144,50}, skip{834,501,144,50}, record{988,501,260,50};
constexpr Rect audio{680,565,134,56}, filter{824,565,134,56}, tune{968,565,134,56}, options{1112,565,136,56};
constexpr Rect buttons[]={hold,skip,record,audio,filter,tune,options};
constexpr bool geometry_ok() {
  for(unsigned i=0;i<7;++i) {
    if(!layout::inside(buttons[i],controls)||buttons[i].h<48)return false;
    for(unsigned j=i+1;j<7;++j)if(buttons[i].intersects(buttons[j]))return false;
  }
  return layout::inside(listening,layout::kContent)&&layout::inside(diagnostics,layout::kContent)
    &&layout::inside(spectrum,layout::kContent)&&layout::inside(decoder,layout::kContent)
    &&layout::inside(controls,layout::kContent)&&!listening.intersects(diagnostics)
    &&!listening.intersects(spectrum)&&!diagnostics.intersects(decoder)&&!decoder.intersects(controls);
}
static_assert(geometry_ok(),"Live layout");
constexpr unsigned bins=141, rows=41, points=96;
struct Metric {bool valid=false;int32_t tenths=0;};
// Optional presentation fields: fill only from existing qualified producers.
// No synthetic fallbacks. These fields do not alter the app Snapshot.
struct PlotData {
  uint32_t epoch=0, now_ms=0, spectrum_epoch=0, spectrum_ms=0, spectrum_seq=0;
  uint32_t center_hz=0, span_hz=0;
  bool spectrum_valid=false, calibrated_dbm=false;
  int16_t floor=-1000, ceiling=0; // tenths of declared units
  std::array<int16_t,bins> power{}; // low to high frequency
  uint32_t symbol_epoch=0,symbol_ms=0,symbol_seq=0;
  Protocol symbol_protocol=Protocol::unknown;
  bool symbol_valid=false;
  int32_t extent_hz=0;
  uint8_t symbol_count=0, reference_count=0;
  std::array<int16_t,points> deviation{}; // actual ordered frequency discriminator samples
  std::array<int16_t,4> reference{};
  uint32_t metric_epoch=0,metric_ms=0;
  Metric rssi{},snr{},ber{},dev{}; // dBm, dB, %, Hz respectively, all tenths
  bool directory_valid=false;
  uint32_t directory_epoch=0,directory_hz=0;
  char directory_hint[36]{},directory_source[24]{};
};
inline bool fresh(uint32_t now,uint32_t then) {return uint32_t(now-then)<=500;}
inline bool spectrum_ok(const PlotData& d) {
  return d.spectrum_valid&&d.epoch==d.spectrum_epoch&&fresh(d.now_ms,d.spectrum_ms)&&d.span_hz&&d.ceiling>d.floor;
}
inline bool symbols_ok(const PlotData& d,Protocol p) {
  return d.symbol_valid&&d.epoch==d.symbol_epoch&&fresh(d.now_ms,d.symbol_ms)&&d.symbol_protocol==p
    &&(p==Protocol::dmr||p==Protocol::nxdn48||p==Protocol::nxdn96)
    &&d.extent_hz>0&&d.symbol_count&&d.symbol_count<=points&&d.reference_count<=4;
}
inline bool hold_ok(const Snapshot& s){return s.can_hold&&!s.follow_active&&!s.discover_running;}
inline bool skip_ok(const Snapshot& s){return s.can_skip&&!s.follow_active&&!s.discover_running&&!s.scan_on_hold;}
enum class Popup:uint8_t {none,audio,filter,tune,options};
struct Intent {Action action{};Popup popup=Popup::none;};
inline Intent touch(int x,int y,const Snapshot& s,bool blocked=false) {
  if(blocked)return {};
  if(hold.contains(x,y)&&hold_ok(s))return {{ActionKind::toggle_hold,0}};
  if(skip.contains(x,y)&&skip_ok(s))return {{ActionKind::skip_frequency,0}};
  if(record.contains(x,y)&&s.can_save_speech&&s.origin==Origin::live)return {{ActionKind::toggle_save_speech,0}};
  if(audio.contains(x,y))return {{},Popup::audio};
  if(filter.contains(x,y))return {{},Popup::filter};
  if(tune.contains(x,y))return {{},Popup::tune};
  if(options.contains(x,y))return {{},Popup::options};
  return {};
}
inline const char* mod(Protocol p) {
  switch(p){case Protocol::dmr:case Protocol::nxdn48:case Protocol::nxdn96:return "4FSK";
    case Protocol::dstar:return "GMSK";default:return "--";}
}
inline void fmt(char* b,size_t n,int32_t v,const char* unit) {
  const int64_t w=v;const uint64_t a=w<0?uint64_t(-w):uint64_t(w);
  std::snprintf(b,n,"%s%lu.%lu %s",w<0?"-":"",static_cast<unsigned long>(a/10),static_cast<unsigned long>(a%10),unit);
}
inline void metric(Paint& p,const char* name,Metric m,const char* unit,int y,bool valid) {
  char b[32]="--"; if(valid&&m.valid)fmt(b,sizeof(b),m.tenths,unit);
  p.fact(name,b,680,y,{680,193,400,168},valid&&m.valid?kWhite:kMuted,104);
}
inline const char* phase(const Snapshot& s) {
  if(s.receiver==Receiver::disconnected||s.receiver==Receiver::error||s.receiver==Receiver::unavailable)return receiver_name(s.receiver);
  if(s.voice.available)return voice_phase_name(s.voice.phase);
  return s.scan_on_hold?"HOLD / MONITOR":"MONITORING";
}
// Paint the small text/control widgets via the existing Paint diff mechanism.
// Caller owns slot selection and structural repaint. Graphs are independent.
inline void panel(Paint& p,const Snapshot& s,const PlotData& d,unsigned id) {
  char a[72]{},b[48]{};
  const bool integrity=s.evidence>=Evidence::integrity_valid;
  switch(id) {
  case 0:
    p.card(listening);p.text("NOW LISTENING",32,178,kGreen,2,listening);
    p.text(protocol_name(s.protocol),450,178,integrity?kGreen:kMuted,2,listening);
    if(s.has_frequency)std::snprintf(a,sizeof(a),"%lu.%06lu MHz",(unsigned long)(s.frequency_hz/1000000),(unsigned long)(s.frequency_hz%1000000));
    else std::snprintf(a,sizeof(a),"-- MHz");
    p.text(a,32,220,kWhite,4,listening);
    if(s.protocol==Protocol::dmr) {
      if(integrity&&s.colour_code_valid)std::snprintf(a,sizeof(a),"CC %u",unsigned(s.colour_code));else std::snprintf(a,sizeof(a),"CC --");
      if(integrity&&s.timeslot_valid&&(s.timeslot==1||s.timeslot==2))std::snprintf(b,sizeof(b),"TS %u",unsigned(s.timeslot));else std::snprintf(b,sizeof(b),"TS --");
    }else if(s.protocol==Protocol::nxdn48||s.protocol==Protocol::nxdn96) {
      if(integrity&&s.ran_valid)std::snprintf(a,sizeof(a),"RAN %u",unsigned(s.ran));else std::snprintf(a,sizeof(a),"RAN --");
      std::snprintf(b,sizeof(b),"%s",s.protocol==Protocol::nxdn48?"NXDN48":"NXDN96");
    }else if(s.protocol==Protocol::p25_phase1||s.protocol==Protocol::p25_phase2) {
      if(integrity&&s.nac_valid)std::snprintf(a,sizeof(a),"NAC %03X",unsigned(s.nac));else std::snprintf(a,sizeof(a),"NAC --");
      std::snprintf(b,sizeof(b),"%s",s.protocol==Protocol::p25_phase1?"PHASE I":"PHASE II");
    }else if(s.protocol==Protocol::dstar&&integrity&&s.dstar_header_valid) {
      std::snprintf(a,sizeof(a),"MY %.11s",s.dstar_my);std::snprintf(b,sizeof(b),"UR %.11s",s.dstar_ur);
    }else if(s.protocol==Protocol::ysf&&integrity&&s.ysf_fich_valid)std::snprintf(a,sizeof(a),"FICH %08lX",(unsigned long)s.ysf_fich_raw);
    else std::snprintf(a,sizeof(a),"MODE NOT VERIFIED");
    p.text(a,32,256,kCyan,2,listening);p.text(b,330,256,kCyan,2,listening);
    if(trusted_identity(s)&&s.has_source)std::snprintf(a,sizeof(a),"%lu",(unsigned long)s.source_id);else std::snprintf(a,sizeof(a),"UNKNOWN");
    if(trusted_identity(s)&&s.has_destination)std::snprintf(b,sizeof(b),"%lu",(unsigned long)s.destination_id);else std::snprintf(b,sizeof(b),"UNKNOWN");
    p.fact("SOURCE",a,32,282,listening,kWhite,90);p.fact(s.group_call?"TG":"DEST",b,330,282,listening,kWhite,66);
    p.text(d.directory_valid&&d.directory_epoch==d.epoch&&s.has_frequency&&d.directory_hz==s.frequency_hz&&d.directory_source[0]?"LIKELY SYSTEM (DIR)":"REPEATER UNKNOWN",32,308,kAmber,2,listening);
    std::snprintf(a,sizeof(a),"%.35s [%.23s]",d.directory_hint,d.directory_source);
    p.text(d.directory_valid&&d.directory_epoch==d.epoch&&s.has_frequency&&d.directory_hz==s.frequency_hz&&d.directory_source[0]?a:"",32,330,kMuted,2,listening);
    p.text(phase(s),32,356,s.voice.available&&s.voice.phase==VoicePhase::decoding?kGreen:kCyan,2,listening);
    break;
  case 1: {
    p.card(diagnostics);p.title("SIGNAL DIAGNOSTICS",680,178,diagnostics);
    const bool f=d.metric_epoch==d.epoch&&fresh(d.now_ms,d.metric_ms);
    if(!(f&&d.rssi.valid)&&s.relative_level_valid)metric(p,"RF REL",{true,s.relative_level_tenths_db},"dB",210,true);
    else metric(p,"RSSI",d.rssi,"dBm",210,f);
    metric(p,"SNR",d.snr,"dB",238,f);metric(p,"BER",d.ber,"%",266,f);
    p.fact("MOD",integrity?mod(s.protocol):"--",680,294,{680,193,400,168},kGreen,104);
    metric(p,"DEV",d.dev,"Hz",322,f);
    if(s.diagnostics_valid)std::snprintf(a,sizeof(a),"CRC FAIL %lu",(unsigned long)s.crc_failures);else std::snprintf(a,sizeof(a),"CRC FAIL --");
    p.text(a,680,350,kMuted,2,diagnostics);
    break;}
  case 2:
    p.card(spectrum);p.title("SPECTRUM / WATERFALL",32,402,spectrum);
    if(spectrum_ok(d))std::snprintf(a,sizeof(a),"CENTER %lu.%06lu   SPAN %lu Hz",(unsigned long)(d.center_hz/1000000),(unsigned long)(d.center_hz%1000000),(unsigned long)d.span_hz);
    else std::snprintf(a,sizeof(a),"SPECTRUM DATA UNAVAILABLE");
    p.text(a,32,620,kMuted,2,spectrum);
    break;
  case 3:
    p.card(decoder);p.title("DECODER STATUS",680,402,decoder);
    // Status indicators, never protocol selectors. Preserve both NXDN variants and YSF.
    {constexpr Protocol ps[]={Protocol::dmr,Protocol::nxdn48,Protocol::nxdn96,Protocol::p25_phase1,Protocol::p25_phase2,Protocol::dstar,Protocol::ysf};
    constexpr const char* names[]={"DMR","N48","N96","P25 I","P25 II","D-STAR","YSF"};
    constexpr int widths[]={58,58,58,82,82,82,58};int x=680;
    for(unsigned i=0;i<7;++i){const Rect r{x,416,widths[i],32};const bool active=s.protocol==ps[i]&&integrity;
      p.fillRoundRect(r.x,r.y,r.w,r.h,4,active?kSelected:kPanel);p.drawRoundRect(r.x,r.y,r.w,r.h,4,active?kGreen:kEdge);
      p.text(names[i],r.x+4,r.y+16,active?kGreen:kMuted,2,r);x+=widths[i]+6;}}
    break;
  case 4:
    p.card(controls);p.title("RECEIVER CONTROLS",680,486,controls);
    p.button(s.scan_on_hold?"RELEASE":"HOLD",hold,hold_ok(s),s.scan_on_hold);
    p.button("SKIP",skip,skip_ok(s));
    p.button(s.save_speech?"VOICE REC [ON]":"VOICE REC [OFF]",record,s.can_save_speech&&s.origin==Origin::live,s.save_speech);
    p.button("AUDIO",audio,true);p.button("FILTER",filter,true);p.button("TUNE",tune,true);p.button("OPTIONS",options,true);
    break;
  }
}
inline uint16_t heat(int v) {
  v=std::clamp(v,0,255);
  if(v<64)return uint16_t(v*31/64);
  if(v<128)return uint16_t(31|((v-64)*63/64<<5));
  if(v<192)return uint16_t((63<<5)|(31-(v-128)*31/64)|((v-128)*31/64<<11));
  return uint16_t((31<<11)|((255-v)*63/63<<5));
}
inline int intensity(const PlotData& d,int v) {
  return std::clamp((v-int(d.floor))*255/(int(d.ceiling)-int(d.floor)),0,255);
}
// Fixed small history; no framebuffer, no allocations, no blocking operations.
class Graphs {
  std::array<std::array<uint8_t,bins>,rows> history_{};
  unsigned count_=0,head_=0;uint32_t epoch_=0,seq_=0,sym_seq_=0,center_=0,span_=0;
  int16_t floor_=0,ceiling_=0;bool calibrated_=false,have_=false,have_symbols_=false,initialized_=false;
  static void label(lgfx::LovyanGFX& g,const char* t,Rect r) {
    g.setFont(nullptr);g.setTextSize(2);g.setTextDatum(middle_left);g.setTextColor(kMuted);g.setClipRect(r.x,r.y,r.w,r.h);g.drawString(t,r.x+4,r.y+r.h/2);g.clearClipRect();
  }
  void clear_history(){count_=head_=0;}
public:
  void reset(){clear_history();initialized_=have_=have_symbols_=false;}
  // Call on UI owner at bounded cadence. force=true after entry, overlay dismissal or graph panel redraw.
  void draw(lgfx::LovyanGFX& g,const Snapshot& s,const PlotData& d,bool force=false) {
    const bool ok=spectrum_ok(d);
    const bool change=!initialized_||epoch_!=d.epoch||center_!=d.center_hz||span_!=d.span_hz||floor_!=d.floor||ceiling_!=d.ceiling||calibrated_!=d.calibrated_dbm;
    if(change||!ok){clear_history();have_=false;}
    if(ok&&(!have_||seq_!=d.spectrum_seq)){
      for(unsigned i=0;i<bins;++i)history_[head_][i]=uint8_t(intensity(d,d.power[i]));
      head_=(head_+1)%rows;count_=std::min(count_+1,rows);seq_=d.spectrum_seq;
      force=true;
    }
    if(force||change||(!ok&&initialized_&&previous_ok_)){
      g.fillRect(trace.x,trace.y,trace.w,trace.h,kBg);g.fillRect(waterfall.x,waterfall.y,waterfall.w,waterfall.h,kBg);
      if(ok) {
        for(int i=0;i<5;++i)g.drawFastHLine(trace.x,trace.y+i*(trace.h-1)/4,trace.w,kEdge);
        for(int i=0;i<9;++i)g.drawFastVLine(trace.x+i*(trace.w-1)/8,trace.y,trace.h,kEdge);
        for(unsigned i=0;i<bins;++i) {
          int h=intensity(d,d.power[i])*(trace.h-1)/255;
          g.fillRect(trace.x+int(i)*4,trace.y+trace.h-1-h,3,h+1,kGreen);
        }
        if(s.has_frequency) {
          int64_t pos=(int64_t(s.frequency_hz)-d.center_hz)*trace.w/d.span_hz+trace.w/2;
          if(pos>=0&&pos<trace.w)g.drawFastVLine(trace.x+int(pos),trace.y,trace.h,kAmber);
        }
        for(unsigned r=0;r<count_;++r){const auto& row=history_[(head_+rows-1-r)%rows];
          for(unsigned i=0;i<bins;++i)g.fillRect(waterfall.x+int(i)*4,waterfall.y+int(r)*2,4,2,heat(row[i]));}
        g.fillRect(32,trace.y,44,trace.h,kPanel);
        char unit[16];std::snprintf(unit,sizeof(unit),"%s",d.calibrated_dbm?"dBm":"rel");label(g,unit,{32,trace.y,44,24});
      } else {
        g.fillRect(32,trace.y,44,trace.h,kPanel);label(g,"NO SPECTRUM",trace);label(g,"NO WATERFALL",waterfall);
      }
    }
    const bool so=symbols_ok(d,s.protocol);
    if(force||change||so!=have_symbols_||(so&&sym_seq_!=d.symbol_seq)){
      g.fillRect(symbols.x,symbols.y,symbols.w,symbols.h,kBg);g.drawRect(symbols.x,symbols.y,symbols.w,symbols.h,kEdge);
      if(so){
        auto y=[&](int v){const int64_t bounded=std::clamp<int64_t>(v,-int64_t(d.extent_hz),int64_t(d.extent_hz));
          return symbols.y+22+int((int64_t(d.extent_hz)-bounded)*(symbols.h-26)/(2*int64_t(d.extent_hz)));};
        label(g,"FSK LEVELS",{symbols.x+2,symbols.y+2,symbols.w-4,18});
        for(unsigned i=0;i<d.reference_count;++i)g.drawFastHLine(symbols.x+2,y(d.reference[i]),symbols.w-4,kEdge);
        for(unsigned i=0;i<d.symbol_count;++i)g.fillRect(symbols.x+2+int(i)*(symbols.w-5)/int(points-1),y(d.deviation[i]),2,2,kGreen);
      }else label(g,"NO SYMBOL DATA",symbols);
    }
    sym_seq_=d.symbol_seq;have_symbols_=so;have_=ok;previous_ok_=ok;initialized_=true;
    epoch_=d.epoch;center_=d.center_hz;span_=d.span_hz;floor_=d.floor;ceiling_=d.ceiling;calibrated_=d.calibrated_dbm;
  }
private:bool previous_ok_=false;
};
} // namespace orcsdr::digital_voice_ui::live_monitor
