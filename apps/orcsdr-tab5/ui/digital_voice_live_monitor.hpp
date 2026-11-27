#pragma once
// The LIVE receiver monitor (default density): five panels, two graphs and four popups, draw-only.
// Layout and visual design: the approved two-column radio monitor (first staged as a candidate by ChatGPT, commit 41e01438).
// Integration: Dashboard owns the lifecycle (retained widgets, dirty painting, popups, graph cadence); the app owns all data and actions.
// Rule for everything shown here: it is a real receiver or decoder value, or it is a dash. Nothing is invented.
#include "digital_voice_dashboard_model.hpp"
#include "digital_voice_dashboard_paint.hpp"
#include "digital_voice_live_common.hpp"
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
constexpr Rect popup_area{668,158,592,478};
constexpr Rect popup_close{1080,560,168,56};
constexpr bool geometry_ok() {
  for(unsigned i=0;i<7;++i) {
    if(!layout::inside(buttons[i],controls)||buttons[i].h<48)return false;
    for(unsigned j=i+1;j<7;++j)if(buttons[i].intersects(buttons[j]))return false;
  }
  return layout::inside(listening,layout::kContent)&&layout::inside(diagnostics,layout::kContent)
    &&layout::inside(spectrum,layout::kContent)&&layout::inside(decoder,layout::kContent)
    &&layout::inside(controls,layout::kContent)&&!listening.intersects(diagnostics)
    &&!listening.intersects(spectrum)&&!diagnostics.intersects(decoder)&&!decoder.intersects(controls)
    &&layout::inside(popup_area,layout::kContent)&&layout::inside(popup_close,popup_area)&&popup_close.h>=48
    &&layout::inside(trace,spectrum)&&layout::inside(waterfall,spectrum)&&!trace.intersects(waterfall);
}
static_assert(geometry_ok(),"Live layout");
constexpr unsigned bins=141, rows=41, points=96;
struct Metric {bool valid=false;int32_t tenths=0;};
// Optional presentation fields: fill only from existing qualified producers. No synthetic fallbacks.
// Today NO producer feeds the spectrum, the symbol levels or the SNR / BER / deviation / calibrated RSSI fields: they stay invalid and
// the panels say so (see docs/digital-voice/LIVE_MONITOR_INTEGRATION.md for the producers that would fill them).
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
inline bool running(const Snapshot& s) {return live_common::scan_is_running(s)||s.follow_active||s.discover_running;}
inline bool hold_ok(const Snapshot& s){return s.can_hold&&!s.follow_active&&!s.discover_running;}
inline bool skip_ok(const Snapshot& s){return s.can_skip&&!s.follow_active&&!s.discover_running&&!s.scan_on_hold;}
inline bool record_ok(const Snapshot& s){return s.can_save_speech&&s.origin==Origin::live;}
// Popups are presentation state (UiState::popup holds one of these); only the controls inside them return receiver actions.
enum class Popup:uint8_t {none=0,audio,filter,tune,options};
constexpr Rect popup_button(Popup p,unsigned i) {
  // audio: 2x2 listen modes. filter / tune / options: full-width rows.
  if(p==Popup::audio)return {680+int(i%2)*296,214+int(i/2)*68,272,56};
  if(p==Popup::tune&&i<2)return {680+int(i)*296,214,272,56};
  return {680,214+int(i)*68,568,56};
}
inline const char* popup_name(Popup p) {
  switch(p){case Popup::audio:return "AUDIO";case Popup::filter:return "FILTER";case Popup::tune:return "TUNE";case Popup::options:return "OPTIONS";default:return "";}
}
// Touch: returns the action for a point, or none. `popup` is UiState::popup. With a popup open only it reacts.
inline Action touch(int x,int y,const Snapshot& s,uint8_t popup_id) {
  const Popup open=static_cast<Popup>(popup_id);
  auto launch=[](Popup p){return Action{ActionKind::toggle_popup,static_cast<uint8_t>(p)};};
  if(open!=Popup::none) {
    if(popup_close.contains(x,y))return {ActionKind::toggle_popup,0};
    switch(open) {
      case Popup::audio:
        for(unsigned i=0;i<4;++i)if(popup_button(open,i).contains(x,y)&&s.voice.available)return {ActionKind::set_listen,static_cast<uint8_t>(i)};
        break;
      case Popup::filter:
        if(popup_button(open,0).contains(x,y)&&s.can_scan)return {ActionKind::monitor_filter,0};
        break;
      case Popup::tune:
        if(popup_button(open,0).contains(x,y)&&action_state(ActionKind::plan_prev,s,UiState{}).enabled)return {ActionKind::plan_prev,0};
        if(popup_button(open,1).contains(x,y)&&action_state(ActionKind::plan_next,s,UiState{}).enabled)return {ActionKind::plan_next,0};
        if(Rect{680,282,568,56}.contains(x,y)&&action_state(ActionKind::scan,s,UiState{}).enabled)return {ActionKind::scan,0};
        break;
      case Popup::options:
        if(popup_button(open,0).contains(x,y)&&s.can_save_speech)return {ActionKind::toggle_log_data,0};
        if(popup_button(open,1).contains(x,y))return {ActionKind::toggle_density,0};
        break;
      default:break;
    }
    return {};
  }
  if(hold.contains(x,y)&&s.follow_active)return {ActionKind::follow_stop,0};           // while following, the left button leaves the channel
  if(hold.contains(x,y)&&hold_ok(s))return {ActionKind::toggle_hold,0};
  if(skip.contains(x,y)&&skip_ok(s))return {ActionKind::skip_frequency,0};
  if(record.contains(x,y)&&record_ok(s))return {ActionKind::toggle_save_speech,0};
  if(audio.contains(x,y))return launch(Popup::audio);
  if(filter.contains(x,y))return launch(Popup::filter);
  if(tune.contains(x,y))return launch(Popup::tune);
  if(options.contains(x,y))return launch(Popup::options);
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
// What the voice path is doing, in words: the voice phase while the voice service is up, otherwise what the scan does.
inline const char* phase(const Snapshot& s) {
  if(s.receiver==Receiver::disconnected||s.receiver==Receiver::error||s.receiver==Receiver::unavailable)return receiver_name(s.receiver);
  if(!running(s))return "NOT LISTENING";
  if(s.voice.available&&s.voice.phase!=VoicePhase::unavailable)return voice_phase_name(s.voice.phase);
  return s.scan_on_hold?"HOLD / MONITOR":"MONITORING";
}
enum Panel : unsigned {kListening=0,kDiagnostics,kSpectrumCard,kDecoder,kControls,kRfFallback,kPopup,kPanelCount};
// Paint one panel through `p` (the caller picks the slot and diff). Graphs are separate (see Graphs).
inline void panel(Paint& p,const Snapshot& s,const PlotData& d,const UiState& u,unsigned id) {
  char a[72]{},b[48]{};
  const bool integrity=s.evidence>=Evidence::integrity_valid;
  const bool run=running(s);
  switch(id) {
  case kListening: {
    const live_common::ModeInfo mode=live_common::scan_mode_info(s);
    const uint32_t hz=s.follow_active?s.follow_frequency_hz:(s.has_frequency?s.frequency_hz:0);
    const Protocol proto=s.follow_active&&s.protocol==Protocol::unknown?s.follow_protocol:s.protocol;
    p.card(listening);p.text("NOW LISTENING",32,178,kGreen,2,listening);
    p.text(mode.name,230,178,mode.color,2,listening);
    p.text(run&&proto!=Protocol::unknown?protocol_name(proto):"",450,178,integrity?kGreen:kMuted,2,listening);
    if(run&&hz)std::snprintf(a,sizeof(a),"%lu.%06lu MHz",(unsigned long)(hz/1000000),(unsigned long)(hz%1000000));
    else std::snprintf(a,sizeof(a),"-- MHz");
    p.text(a,32,220,run&&hz?kWhite:kMuted,4,listening);
    a[0]=b[0]=0;
    if(!run) {std::snprintf(a,sizeof(a),"NO CHANNEL TUNED");}
    else if(s.protocol==Protocol::dmr) {
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
    else std::snprintf(a,sizeof(a),"LISTENING FOR A SYSTEM");
    p.text(a,32,256,run?kCyan:kMuted,2,listening);p.text(b,330,256,kCyan,2,listening);
    // Who is talking: the call being played (voice service) while one is on the air, otherwise the last identity-validated event.
    const VoiceAudioView& v=s.voice;
    const bool on_air=v.available&&(v.phase==VoicePhase::decoding||v.phase==VoicePhase::acquiring||v.phase==VoicePhase::buffering||
                                    v.phase==VoicePhase::degraded||v.phase==VoicePhase::silence_frame||v.phase==VoicePhase::call_ended);
    bool have=false,grp=false;uint32_t src=0,dst=0;
    if(run&&on_air&&v.source_id) {have=true;src=v.source_id;dst=v.destination_id;grp=v.group_call;}
    else if(run&&trusted_identity(s)&&s.has_source&&s.has_destination) {have=true;src=s.source_id;dst=s.destination_id;grp=s.group_call;}
    if(have) {std::snprintf(a,sizeof(a),"%lu",(unsigned long)src);std::snprintf(b,sizeof(b),"%lu",(unsigned long)dst);}
    else {std::snprintf(a,sizeof(a),"UNKNOWN");std::snprintf(b,sizeof(b),"UNKNOWN");}
    p.fact("SOURCE",a,32,282,listening,have?kWhite:kMuted,90);p.fact(grp?"TG":"DEST",b,330,282,listening,have?kWhite:kMuted,66);
    // Directory hint: a suggestion from a local or imported list, never over-air evidence. Shown only for the tuned frequency in this epoch.
    const bool hint=d.directory_valid&&d.directory_epoch==d.epoch&&hz&&d.directory_hz==hz&&d.directory_source[0];
    p.text(hint?"LIKELY SYSTEM (DIRECTORY)":"REPEATER UNKNOWN",32,308,kAmber,2,listening);
    if(hint)std::snprintf(a,sizeof(a),"%.35s [%.23s]",d.directory_hint,d.directory_source);else a[0]=0;
    p.text(a,32,330,kMuted,2,listening);
    p.text(phase(s),32,356,s.voice.available&&s.voice.phase==VoicePhase::decoding?kGreen:kCyan,2,listening);
    p.text(mode.state,330,356,mode.color,2,listening);
    break;}
  case kDiagnostics: {
    p.card(diagnostics);p.title("SIGNAL DIAGNOSTICS",680,178,diagnostics);
    const bool f=d.metric_epoch==d.epoch&&fresh(d.now_ms,d.metric_ms);
    if(!(f&&d.rssi.valid)&&s.relative_level_valid&&run)metric(p,"RF REL",{true,s.relative_level_tenths_db},"dB",210,true);
    else metric(p,"RSSI",d.rssi,"dBm",210,f);
    metric(p,"SNR",d.snr,"dB",238,f);metric(p,"BER",d.ber,"%",266,f);
    p.fact("MOD",run&&integrity?mod(s.protocol):"--",680,294,{680,193,400,168},run&&integrity?kGreen:kMuted,104);
    metric(p,"DEV",d.dev,"Hz",322,f);
    if(s.diagnostics_valid)std::snprintf(a,sizeof(a),"CRC FAIL %lu",(unsigned long)s.crc_failures);else std::snprintf(a,sizeof(a),"CRC FAIL --");
    p.text(a,680,350,kMuted,2,diagnostics);
    break;}
  case kSpectrumCard:
    p.card(spectrum);
    p.title(spectrum_ok(d)?"SPECTRUM / WATERFALL":"RF LEVEL / WATERFALL",32,402,spectrum);
    if(spectrum_ok(d))std::snprintf(a,sizeof(a),"CENTER %lu.%06lu   SPAN %lu Hz",(unsigned long)(d.center_hz/1000000),(unsigned long)(d.center_hz%1000000),(unsigned long)d.span_hz);
    else std::snprintf(a,sizeof(a),"SPECTRUM DATA UNAVAILABLE");
    p.text(a,32,620,kMuted,2,spectrum);
    break;
  case kRfFallback:
    // No spectrum producer: the one real, existing level measurement is shown instead (the receiver's relative RF level, last 6 s).
    live_common::rf_trace(p,s,trace,s.rf_trace_on&&run);
    break;
  case kDecoder:
    p.card(decoder);p.title("DECODER STATUS",680,402,decoder);
    // Passive indicators of the protocol the receiver verified on the tuned channel. Never selectors, never "installed" claims.
    {constexpr Protocol ps[]={Protocol::dmr,Protocol::nxdn48,Protocol::nxdn96,Protocol::p25_phase1,Protocol::p25_phase2,Protocol::dstar,Protocol::ysf};
    constexpr const char* names[]={"DMR","N48","N96","P25 I","P25 II","D-STAR","YSF"};
    constexpr int widths[]={58,58,58,82,82,82,58};int x=680;
    for(unsigned i=0;i<7;++i){const Rect r{x,416,widths[i],32};const bool active=run&&s.protocol==ps[i]&&integrity;
      p.fillRoundRect(r.x,r.y,r.w,r.h,4,active?kSelected:kPanel);p.drawRoundRect(r.x,r.y,r.w,r.h,4,active?kGreen:kEdge);
      p.text(names[i],r.x+4,r.y+16,active?kGreen:kMuted,2,r);x+=widths[i]+6;}}
    break;
  case kControls:
    p.card(controls);p.title("RECEIVER CONTROLS",680,486,controls);
    if(s.follow_active)p.button("UNFOLLOW",hold,true,true);
    else p.button(s.scan_on_hold?"RELEASE":"HOLD",hold,hold_ok(s),s.scan_on_hold);
    p.button("SKIP",skip,skip_ok(s));
    p.button(s.save_speech?"RECORD VOICE [ON]":"RECORD VOICE [OFF]",record,record_ok(s),s.save_speech);
    p.button("AUDIO",audio,true,u.popup==static_cast<uint8_t>(Popup::audio));p.button("FILTER",filter,true,u.popup==static_cast<uint8_t>(Popup::filter));
    p.button("TUNE",tune,true,u.popup==static_cast<uint8_t>(Popup::tune));p.button("OPTIONS",options,true,u.popup==static_cast<uint8_t>(Popup::options));
    break;
  case kPopup: {
    const Popup open=static_cast<Popup>(u.popup);
    p.card(popup_area);p.title(popup_name(open),680,178,popup_area);
    p.button("CLOSE",popup_close,true);
    switch(open) {
    case Popup::audio:
      for(unsigned i=0;i<4;++i)p.button(i==0?"AUTO":i==1?"SLOT 1":i==2?"SLOT 2":"MUTE",popup_button(open,i),s.voice.available,s.voice.available&&static_cast<int>(s.voice.listen)==int(i));
      p.label("AUTO plays whichever time slot is talking.",680,364,popup_area);
      p.label("SLOT 1 or SLOT 2 locks to one. MUTE is silent.",680,392,popup_area);
      p.label("Volume: the speaker icon at the top.",680,432,popup_area);
      if(!s.voice.available)p.text("Voice audio is not running.",680,472,kAmber,2,popup_area);
      break;
    case Popup::filter: {
      const char* pn=s.monitor_proto==1?"DMR":s.monitor_proto==2?"NXDN48":s.monitor_proto==3?"NXDN96":"ALL";
      std::snprintf(a,sizeof(a),"MONITOR FILTER: %s",pn);
      p.button(a,popup_button(open,0),s.can_scan);
      p.label("Chooses which systems the MONITOR scan visits.",680,296,popup_area);
      p.label("All decoders keep listening; this only picks channels.",680,324,popup_area);
      p.label("Tap to step ALL, DMR, NXDN48, NXDN96. START it on SCAN.",680,352,popup_area);
      break;}
    case Popup::tune: {
      const ActionState pv=action_state(ActionKind::plan_prev,s,u),nx=action_state(ActionKind::plan_next,s,u),sc=action_state(ActionKind::scan,s,u);
      p.button("< PLAN",popup_button(open,0),pv.enabled);p.button("PLAN >",popup_button(open,1),nx.enabled);
      p.button(live_common::scan_is_running(s)?"STOP SCAN":"START SCAN",Rect{680,282,568,56},sc.enabled,live_common::scan_is_running(s));
      std::snprintf(a,sizeof(a),"PLAN: %.40s",s.scan.plan_name[0]?s.scan.plan_name:"-");
      p.text(a,680,366,kWhite,2,popup_area);
      p.label("Plans change only while the scan is stopped.",680,396,popup_area);
      p.label("MANUAL FREQUENCY ENTRY: NOT AVAILABLE YET",680,436,popup_area);
      break;}
    case Popup::options:
      if(!s.can_save_speech)std::snprintf(a,sizeof(a),"LOG DATA: N/A");
      else if(s.save_log)std::snprintf(a,sizeof(a),"LOG DATA: ON (%lu)",(unsigned long)s.log_lines);
      else std::snprintf(a,sizeof(a),"LOG DATA: OFF");
      p.button(a,popup_button(open,0),s.can_save_speech,s.save_log);
      p.button("DETAILED LIVE SCREEN",popup_button(open,1),true);
      p.label("LOG DATA saves every verified event to the SD card.",680,364,popup_area);
      p.label("DETAILED shows the voice engine counters.",680,392,popup_area);
      p.label("Switch back in SETUP > screen density.",680,420,popup_area);
      break;
    default:break;
    }
    break;}
  default:break;
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
// Fixed small history; no framebuffer, no allocations after construction, no blocking operations. The object is ~5.8 KiB: the Dashboard
// allocates it in PSRAM on first use.
class Graphs {
  std::array<std::array<uint8_t,bins>,rows> history_{};
  unsigned count_=0,head_=0;uint32_t epoch_=0,seq_=0,sym_seq_=0,center_=0,span_=0;
  int16_t floor_=0,ceiling_=0;bool calibrated_=false,have_=false,have_symbols_=false,initialized_=false,previous_ok_=false;
  static void label(lgfx::LovyanGFX& g,const char* t,Rect r) {
    g.setFont(nullptr);g.setTextSize(2);g.setTextDatum(middle_left);g.setTextColor(kMuted);g.setClipRect(r.x,r.y,r.w,r.h);g.drawString(t,r.x+4,r.y+r.h/2);g.clearClipRect();
  }
  void clear_history(){count_=head_=0;}
 public:
  void reset(){clear_history();initialized_=have_=have_symbols_=previous_ok_=false;}
  // True when the spectrum trace area is drawn by the graphs (valid spectrum). When false the Dashboard fills it with the RF level strip.
  bool owns_trace() const {return previous_ok_;}
  // Call on the UI owner at a bounded cadence. force=true after entry, popup dismissal or a card repaint (the panels fill over the graph areas).
  void draw(lgfx::LovyanGFX& g,const Snapshot& s,const PlotData& d,bool force=false,bool symbols_visible=true) {
    const bool ok=spectrum_ok(d);
    const bool change=!initialized_||epoch_!=d.epoch||center_!=d.center_hz||span_!=d.span_hz||floor_!=d.floor||ceiling_!=d.ceiling||calibrated_!=d.calibrated_dbm;
    if(change||!ok){clear_history();have_=false;}
    if(ok&&(!have_||seq_!=d.spectrum_seq)){
      for(unsigned i=0;i<bins;++i)history_[head_][i]=uint8_t(intensity(d,d.power[i]));
      head_=(head_+1)%rows;count_=std::min(count_+1,rows);seq_=d.spectrum_seq;
      force=true;
    }
    if(force||change||(!ok&&initialized_&&previous_ok_)){
      g.fillRect(waterfall.x,waterfall.y,waterfall.w,waterfall.h,kBg);
      if(ok) {
        g.fillRect(trace.x,trace.y,trace.w,trace.h,kBg);
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
        // One row of the waterfall is a 564 x 1 strip pushed twice: 82 small transfers instead of thousands of tiny fills.
        uint16_t line[bins*4];
        for(unsigned r=0;r<count_;++r){const auto& row=history_[(head_+rows-1-r)%rows];
          for(unsigned i=0;i<bins;++i){const uint16_t c=heat(row[i]);for(unsigned k=0;k<4;++k)line[i*4+k]=c;}
          g.pushImage(waterfall.x,waterfall.y+int(r)*2,int(bins)*4,1,line);
          g.pushImage(waterfall.x,waterfall.y+int(r)*2+1,int(bins)*4,1,line);}
        g.fillRect(32,trace.y,44,trace.h,kPanel);
        label(g,d.calibrated_dbm?"dBm":"rel",{32,trace.y,44,24});
      } else {
        label(g,"NO WATERFALL",waterfall);
      }
    }
    const bool so=symbols_ok(d,s.protocol);
    if(symbols_visible&&(force||change||so!=have_symbols_||(so&&sym_seq_!=d.symbol_seq))){
      g.fillRect(symbols.x,symbols.y,symbols.w,symbols.h,kBg);g.drawRect(symbols.x,symbols.y,symbols.w,symbols.h,kEdge);
      if(so){
        auto y=[&](int v){const int64_t bounded=std::clamp<int64_t>(v,-int64_t(d.extent_hz),int64_t(d.extent_hz));
          return symbols.y+22+int((int64_t(d.extent_hz)-bounded)*(symbols.h-26)/(2*int64_t(d.extent_hz)));};
        label(g,"FSK LEVELS",{symbols.x+2,symbols.y+2,symbols.w-4,18});
        for(unsigned i=0;i<d.reference_count;++i)g.drawFastHLine(symbols.x+2,y(d.reference[i]),symbols.w-4,kEdge);
        for(unsigned i=0;i<d.symbol_count;++i)g.fillRect(symbols.x+2+int(i)*(symbols.w-5)/int(points-1),y(d.deviation[i]),2,2,kGreen);
      }else label(g,"NO SYMBOLS",symbols);
    }
    if(symbols_visible){sym_seq_=d.symbol_seq;have_symbols_=so;}
    have_=ok;previous_ok_=ok;initialized_=true;
    epoch_=d.epoch;center_=d.center_hz;span_=d.span_hz;floor_=d.floor;ceiling_=d.ceiling;calibrated_=d.calibrated_dbm;
  }
};
} // namespace orcsdr::digital_voice_ui::live_monitor
