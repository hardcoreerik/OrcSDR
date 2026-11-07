// Native, snapshot-only AIS dashboard for the M5Stack Tab5.
// Receiver integration is intentionally separate: a valid app snapshot is the only source of displayed AIS facts.
// This renderer is UI-task-owned and requires screen_controller to grant screens::Id::ais.
#include "ais_dashboard.hpp"
#include "dashboard_audio_control.hpp"
#include "screen_controller.hpp"
#include "focus_nav.hpp"
#include "acars_dirty_rects.hpp"
#if defined(ESP_PLATFORM)
#include "map_view.hpp"
#endif
#include <M5Unified.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <utility>

namespace orcsdr::ais_ui {
namespace {
using namespace layout;
constexpr uint16_t kBg=TFT_BLACK,kPanel=0x0841,kEdge=0x2945,kCyan=0x2e7f,kGreen=0x6fe8,
                   kMuted=0x8c71,kAmber=0xfd20,kRed=0xf800,kBlue=0x04ff,kWhite=TFT_WHITE;
constexpr int kBodyHeight=522; // y114..635, does not touch header or bottom tab strip
constexpr int kFullThreshold=55;
constexpr size_t kPushRectCapacity=64;
static constexpr const char* kTabs[]={"LIVE MAP","TARGETS","MESSAGES","PROTOCOL","SIGNAL","EVENTS","SETUP"};
static constexpr const char* kTypes[]={
  "Class A position (scheduled)","Class A position (assigned)","Class A position (response)",
  "Base station report","Static and voyage","Addressed binary","Binary acknowledge",
  "Broadcast binary","SAR aircraft position","UTC/date inquiry","UTC/date response",
  "Addressed safety text","Safety acknowledgement","Broadcast safety text",
  "Interrogation","Assignment mode command","GNSS correction","Class B position",
  "Extended Class B position","Data link management","Aid to Navigation",
  "Channel management","Group assignment","Class B static parts A/B",
  "Single-slot binary","Multi-slot binary","Long-range AIS","Single-slot AtoN (2026)"
};
static_assert(sizeof(kTypes)/sizeof(kTypes[0])==28);
struct Renderer {
  lgfx::LovyanGFX* screen=&M5.Display;
  int offset=0;
  void set(lgfx::LovyanGFX* s,int y) { screen=s;offset=y; }
  void fill(int x,int y,int w,int h,uint16_t c) { screen->fillRect(x,y-offset,w,h,c); }
  void line(int x,int y,int xx,int yy,uint16_t c) { screen->drawLine(x,y-offset,xx,yy-offset,c); }
  void hline(int x,int y,int w,uint16_t c) { screen->drawFastHLine(x,y-offset,w,c); }
  void round(int x,int y,int w,int h,int rad,uint16_t c) { screen->drawRoundRect(x,y-offset,w,h,rad,c); }
  void fillround(int x,int y,int w,int h,int rad,uint16_t c) { screen->fillRoundRect(x,y-offset,w,h,rad,c); }
  void circle(int x,int y,int r,uint16_t c) { screen->drawCircle(x,y-offset,r,c); }
  void fillcircle(int x,int y,int r,uint16_t c) { screen->fillCircle(x,y-offset,r,c); }
  void text(const char* s,int x,int y,uint16_t c=0xffff,uint8_t size=2) {
    screen->setFont(nullptr); screen->setTextSize(size); screen->setTextDatum(middle_left);
    screen->setTextColor(c);screen->drawString(s?s:"",x,y-offset);
  }
};
Renderer d;
Snapshot* g_data=nullptr; // PSRAM-capable heap (never allocate these large arrays in static internal .bss)
M5Canvas *g_canvas=nullptr,*g_front=nullptr;
bool g_front_valid=false,g_active=false;
View g_view=View::map;
uint32_t g_selected_mmsi=0;
uint8_t g_selected_message=0,g_protocol=1;
uint16_t g_range_nm=25;
uint8_t g_target_offset=0,g_message_offset=0,g_event_offset=0,g_protocol_offset=0;
uint32_t g_last_revision=0;

bool hit(Rect r,int x,int y) { return r.contains(x,y); }
void card(Rect r,uint16_t border=kEdge) { d.fillround(r.x,r.y,r.w,r.h,9,kPanel); d.round(r.x,r.y,r.w,r.h,9,border); }
void label(const char* s,int x,int y,uint16_t c=kMuted) { d.text(s,x,y,c,2); }
void title(const char* s,int x,int y) { d.text(s,x,y,kCyan,2); }
void button(const char* s,Rect r,bool selected=false,bool enabled=true) {
  d.fillround(r.x,r.y,r.w,r.h,7,kPanel);
  d.round(r.x,r.y,r.w,r.h,7,selected?kGreen:enabled?kCyan:kEdge);
  d.text(s,r.x+12,r.y+r.h/2,selected?kGreen:enabled?kWhite:kMuted,2);
  focus_nav::note(r.x,r.y,r.w,r.h);
}
const char* station_name(Station s) {
  switch(s){
    case Station::vessel_a: return "CLASS A";
    case Station::vessel_b: return "CLASS B";
    case Station::base_station: return "BASE STATION";
    case Station::aid_physical: return "PHYSICAL AtoN";
    case Station::aid_virtual: return "VIRTUAL AtoN";
    case Station::sar_aircraft: return "SAR AIRCRAFT";
    default: return "OTHER STATION";
  }
}
uint16_t station_color(Station s) {
  return s==Station::aid_virtual?kAmber:s==Station::aid_physical?kCyan:
         s==Station::sar_aircraft?kRed:s==Station::base_station?kBlue:kGreen;
}
const char* receiver_label() {
  if(!g_data) return "NO RECEIVER";
  if(g_data->receiver==ReceiveState::disconnected)return "RECEIVER DISCONNECTED";
  if(g_data->receiver==ReceiveState::unavailable)return "RECEIVER UNAVAILABLE";
  return g_data->receiver_settled?"RECEIVING":"TUNING / SETTLING";
}
uint8_t bounded_targets(){return g_data?std::min<size_t>(g_data->target_count,kTargets):0;}
uint8_t bounded_messages(){return g_data?std::min<size_t>(g_data->message_count,kMessages):0;}
uint8_t bounded_events(){return g_data?std::min<size_t>(g_data->event_count,kEvents):0;}
const Target* selected_target() {
  if(!g_data)return nullptr;
  for(size_t i=0;i<bounded_targets();++i)if(g_data->targets[i].mmsi==g_selected_mmsi)return &g_data->targets[i];
  return nullptr; // NEVER substitute another vessel's fields after old target disappears
}
const Message* selected_message() {
  return g_data&&g_selected_message<bounded_messages()?&g_data->messages[g_selected_message]:nullptr;
}
void countlabel(const char* name,uint32_t n,int x,int y,uint16_t c=kGreen) {
  char buf[48];std::snprintf(buf,sizeof(buf),"%s %lu",name,static_cast<unsigned long>(n));
  label(buf,x,y,c);
}
void detailrow(const char* key,const char* value,int x,int y,int xval) {
  label(key,x,y,kMuted);d.text(value?value:"--",xval,y,kWhite,2);
}
void titlebar(const char* name,const char* sub) {
  d.fillround(20,114,1240,34,6,kPanel);
  d.text(name,34,131,kGreen,2);
  d.text(sub,260,131,kMuted,2);
}
void map_page() {
  const Snapshot& s=*g_data;
  card(mapMain);card(mapSummary);card(mapDetail);
  title("LOCAL MARITIME MAP",40,183);
  d.text("25/50/100 NM",504,183,kMuted,2);
  button("-",{718,166,42,44});button("+",{772,166,42,44});
  // Preserve genuine OrcMaps tiles and their attribution when a valid receiver location exists.
#if defined(ESP_PLATFORM)
  maps::RegionalView mv{};
  if(s.coordinates_valid) {
    mv=maps::draw_region(*d.screen,s.receiver_lat_e7,s.receiver_lon_e7,
       g_range_nm,40,212-d.offset,768,394,kPanel,kEdge,kMuted,nullptr,true);
  } else {
    d.fill(40,212,768,394,kBg);
    d.text("SET RECEIVER LOCATION TO POSITION AIS TARGETS",70,356,kAmber,2);
  }
  if(mv.valid) for(size_t i=0;i<bounded_targets();++i) {
    const Target& t=s.targets[i];if(!t.has_position||t.position_stale)continue;
    int x=0,y=0;
    if(!maps::project(mv,t.lat_e7,t.lon_e7,&x,&y))continue;
    if(x<40||x>=808||y<0||y>=394)continue;
    y+=212; // map render coordinates are canvas-relative
    d.fillcircle(x,y,t.mmsi==g_selected_mmsi?10:7,station_color(t.station));
    if(t.mmsi==g_selected_mmsi)d.circle(x,y,15,kWhite);
  }
#else
  d.fill(40,212,768,394,kBg);
  d.text("LOCAL MAP: REQUIRES ORCMAPS ON DEVICE",78,350,kMuted,2);
#endif
  title("LIVE ACTIVITY",858,184);
  countlabel("TARGETS",bounded_targets(),858,226);
  countlabel("MESSAGES",s.crc_ok[0]+s.crc_ok[1],858,262);
  countlabel("AIS 1 CRC",s.crc_ok[0],858,306,kCyan);
  countlabel("AIS 2 CRC",s.crc_ok[1],858,340,kCyan);
  title("SELECTED TARGET",858,414);
  const Target* t=selected_target();
  if(t) {
    d.text(t->name[0]?t->name:"NAME NOT TRANSMITTED",858,452,kWhite,2);
    char buf[90];std::snprintf(buf,sizeof(buf),"MMSI %09lu",(unsigned long)t->mmsi);
    label(buf,858,488);
    detailrow("TYPE",station_name(t->station),858,520,962);
    if(t->has_speed){std::snprintf(buf,sizeof(buf),"%u.%u kn",t->speed_tenths_kn/10,t->speed_tenths_kn%10);detailrow("SOG",buf,858,551,962);}
    else detailrow("SOG","--",858,551,962);
    std::snprintf(buf,sizeof(buf),"%lus ago",(unsigned long)t->heard_age_s);
    detailrow("LAST RX",buf,858,583,962);
  } else {
    label("SELECT A VESSEL OR AIS STATION",858,470);
    label(s.receiver_settled?"NO TARGET SELECTED":receiver_label(),858,507);
  }
}
void targets_page() {
  card(targetsTable);card(targetsDetail);
  title("AIS STATIONS",38,186);
  countlabel("VISIBLE",bounded_targets(),500,186);
  label("NAME / MMSI",38,221); label("TYPE",400,221);label("AGE",650,221);
  d.hline(32,241,693,kEdge);
  for(size_t j=0;j<8;++j) {
    const size_t i=g_target_offset+j;if(i>=bounded_targets())break;
    const Target& t=g_data->targets[i];
    const int y=265+static_cast<int>(j)*43;
    if(t.mmsi==g_selected_mmsi)d.fillround(32,y-18,692,38,4,0x1062);
    d.text(t.name[0]?t.name:"UNNAMED AIS STATION",44,y,station_color(t.station),2);
    char buf[40];std::snprintf(buf,sizeof(buf),"%09lu",(unsigned long)t.mmsi);label(buf,276,y);
    label(station_name(t.station),405,y);
    std::snprintf(buf,sizeof(buf),"%lus",(unsigned long)t.heard_age_s);label(buf,660,y);
    d.hline(36,y+22,680,kEdge);
  }
  if(!bounded_targets())label("NO AIS STATIONS DECODED",44,294);
  button("UP",{34,574,126,44});button("DOWN",{170,574,126,44});
  title("STATION DETAILS",770,187);
  const Target* t=selected_target();
  if(!t) {label("TAP A ROW TO INSPECT",770,263);return;}
  char buf[88];d.text(t->name[0]?t->name:"UNNAMED AIS STATION",770,231,kWhite,2);
  std::snprintf(buf,sizeof(buf),"%09lu",(unsigned long)t->mmsi);
  detailrow("MMSI",buf,770,272,970); detailrow("CLASS",station_name(t->station),770,310,970);
  detailrow("CALLSIGN",t->callsign[0]?t->callsign:"--",770,349,970);
  detailrow("NAV",t->nav_status[0]?t->nav_status:"--",770,388,970);
  if(t->has_position) {
    std::snprintf(buf,sizeof(buf),"%.5f, %.5f",t->lat_e7/1e7,t->lon_e7/1e7);
    detailrow("POSITION",buf,770,430,916);
  } else detailrow("POSITION","NOT TRANSMITTED",770,430,916);
  if(t->has_course)std::snprintf(buf,sizeof(buf),"%u.%u deg",t->course_tenths_deg/10,t->course_tenths_deg%10);
  detailrow("COURSE",t->has_course?buf:"--",770,471,970);
  detailrow("DEST",t->destination[0]?t->destination:"--",770,512,970);
  std::snprintf(buf,sizeof(buf),"%lus (POSITION: %lus)",(unsigned long)t->heard_age_s,(unsigned long)t->position_age_s);
  detailrow("LAST RX",buf,770,553,890);
  if(t->position_stale)label("POSITION IS STALE",770,590,kAmber);
}
void messages_page() {
  card(messagesTable);card(messagesDetail);
  title("LIVE AIS MESSAGE JOURNAL",38,186);
  label("AGE",38,220);label("CH",140,220);label("TYPE",214,220);label("MMSI",346,220);
  label("DECODED SUMMARY",502,220);d.hline(32,241,798,kEdge);
  for(size_t j=0;j<9;++j) {
    size_t i=g_message_offset+j;if(i>=bounded_messages())break;
    const Message& m=g_data->messages[i];int y=265+static_cast<int>(j)*38;
    if(i==g_selected_message)d.fillround(30,y-17,800,34,4,0x1062);
    char v[36];std::snprintf(v,sizeof(v),"%lus",(unsigned long)(m.age_ms/1000));label(v,38,y);
    std::snprintf(v,sizeof(v),"%u",(unsigned)m.channel);d.text(v,149,y,kCyan,2);
    std::snprintf(v,sizeof(v),"%u",(unsigned)m.type);d.text(v,225,y,kWhite,2);
    std::snprintf(v,sizeof(v),"%09lu",(unsigned long)m.mmsi);label(v,330,y);
    char summary[26]{};std::snprintf(summary,sizeof(summary),"%.25s",m.summary);
    d.text(summary,508,y,m.crc_ok?kGreen:kAmber,2);
    d.hline(36,y+19,790,kEdge);
  }
  if(!bounded_messages())label("NO VALIDATED MESSAGES YET",40,280);
  button("UP",{36,579,118,44});button("DOWN",{164,579,125,44});
  title("PACKET INSPECTOR",875,187);
  const Message* m=selected_message();
  if(!m){label("SELECT A MESSAGE",875,280);return;}
  char v[70];std::snprintf(v,sizeof(v),"TYPE %u / AIS %u",(unsigned)m->type,(unsigned)m->channel);
  d.text(v,874,231,kWhite,2);
  std::snprintf(v,sizeof(v),"MMSI %09lu",(unsigned long)m->mmsi);label(v,874,268);
  label(m->crc_ok?"RF CRC PASS":"RF CRC NOT VALIDATED",874,307,m->crc_ok?kGreen:kAmber);
  label("RAW PACKET HEX (BOUNDED)",874,344);
  const size_t len=std::strlen(m->raw_hex);
  for(size_t j=0;j<7;++j) {
    size_t start=j*26;if(start>=len)break;
    char row[27]{};std::memcpy(row,m->raw_hex+start,std::min<size_t>(26,len-start));
    d.text(row,873,377+static_cast<int>(j)*27,kWhite,2);
  }
  if(m->raw_truncated)label("RAW DISPLAY TRUNCATED",873,587,kAmber);
  label("NMEA IS GENERATED SEPARATELY",873,611);
}
void protocol_page() {
  card(protocolIndex);card(protocolDetail);
  title("ALL 28 AIS MESSAGE TYPES",38,187);
  label("ITU-R M.1371-6 (02/2026)",38,217);
  for(int j=0;j<10;++j) {
    int type=g_protocol_offset+j+1;if(type>28)break;int y=257+j*35;
    if(type==g_protocol)d.fillround(32,y-16,462,33,4,0x1062);
    char buf[54];std::snprintf(buf,sizeof(buf),"%02d  %.32s",type,kTypes[type-1]);
    d.text(buf,46,y,type==g_protocol?kGreen:kWhite,2);
  }
  button("UP",{44,578,113,44});button("DOWN",{169,578,124,44});
  title("MESSAGE TYPE INSPECTOR",540,186);
  char v[88];std::snprintf(v,sizeof(v),"TYPE %02u   %s",(unsigned)g_protocol,kTypes[g_protocol-1]);
  d.text(v,542,231,kWhite,2);
  d.hline(540,257,700,kEdge);
  label("SPECIFICATION",542,284);
  label("ITU-R M.1371-6 ANNEX 7",760,284,kCyan);
  label("CLASSIFICATION",542,321);
  const char* cls=g_protocol==21||g_protocol==28?"AIDS TO NAVIGATION":
        g_protocol==9?"SEARCH AND RESCUE":g_protocol==27?"LONG RANGE":
        (g_protocol==6||g_protocol==8||g_protocol==25||g_protocol==26)?"APPLICATION / BINARY":
        (g_protocol==12||g_protocol==14)?"SAFETY MESSAGE":"MARITIME RADIO";
  d.text(cls,760,321,kWhite,2);
  label("VALID PACKETS IN JOURNAL",542,366);
  unsigned hits=0;
  for(size_t i=0;i<bounded_messages();++i)if(g_data->messages[i].type==g_protocol&&g_data->messages[i].crc_ok)++hits;
  std::snprintf(v,sizeof(v),"%u visible",hits);d.text(v,960,366,kGreen,2);
  d.hline(542,389,698,kEdge);
  label("DECODER FIELD INTERPRETATION",542,418);
  const Message* recent=nullptr;
  for(size_t i=0;i<bounded_messages();++i)
    if(g_data->messages[i].type==g_protocol&&g_data->messages[i].crc_ok){recent=&g_data->messages[i];break;}
  if(recent) {
    std::snprintf(v,sizeof(v),"MMSI %09lu  /  CHANNEL %u",(unsigned long)recent->mmsi,(unsigned)recent->channel);
    d.text(v,542,454,kWhite,2);
    d.text(recent->summary,542,491,kGreen,2);
    label("OPEN MESSAGES FOR RAW PACKET",542,543,kMuted);
  } else {
    label("NO VALIDATED PACKET OF THIS TYPE",542,463,kAmber);
    label("THE STANDARD DEFINES ITS FIELDS; NO DATA IS INVENTED.",542,507);
  }
  if(g_protocol==21||g_protocol==28)label("AtoN != VESSEL: KEEP ITS OWN MAP ENTITY",542,584,kCyan);
  if(g_protocol==27)label("RF REQUIRES SEPARATE CH75/CH76 TUNING",542,584,kAmber);
}
void signal_trace(Rect plot,unsigned ch) {
  d.fill(plot.x,plot.y,plot.w,plot.h,kBg);
  d.round(plot.x,plot.y,plot.w,plot.h,2,kEdge);
  for(int i=1;i<4;++i)d.hline(plot.x+1,plot.y+i*plot.h/4,plot.w-2,kEdge);
  if(!g_data->spectrum_valid[ch]) {
    label("NO MEASURED FFT DATA",plot.x+20,plot.y+plot.h/2);
    return;
  }
  int lastx=0,lasty=0;
  for(size_t i=0;i<kSpectrumBins;++i) {
    int x=plot.x+3+static_cast<int>(i)*(plot.w-6)/static_cast<int>(kSpectrumBins-1);
    const int db=std::clamp<int>(g_data->spectrum_dbfs[ch][i],-1100,-100);
    const int y=plot.y+plot.h-3-(db+1100)*(plot.h-6)/1000;
    if(i)d.line(lastx,lasty,x,y,ch?kAmber:kCyan);
    lastx=x;lasty=y;
  }
}
void signal_page() {
  card(signalLeftTop);card(signalLeftBottom);card(signalRight);
  title("AIS 1  /  161.975 MHZ",38,182);
  signal_trace({38,211,573,145},0);
  title("AIS 2  /  162.025 MHZ",38,415);
  signal_trace({38,444,573,166},1);
  title("RECEPTION DIAGNOSTICS",662,184);
  label(receiver_label(),662,227,g_data->receiver_settled?kGreen:kAmber);
  char v[80];
  std::snprintf(v,sizeof(v),"%lu",(unsigned long)g_data->sample_rate_sps);
  detailrow("IQ SAMPLES/S",v,662,272,1040);
  std::snprintf(v,sizeof(v),"%lu",(unsigned long)g_data->crc_ok[0]);
  detailrow("AIS 1 CRC PASS",v,662,319,1040);
  std::snprintf(v,sizeof(v),"%lu",(unsigned long)g_data->crc_ok[1]);
  detailrow("AIS 2 CRC PASS",v,662,366,1040);
  std::snprintf(v,sizeof(v),"%lu",(unsigned long)(g_data->crc_failed[0]+g_data->crc_failed[1]));
  detailrow("CRC FAIL",v,662,413,1040);
  std::snprintf(v,sizeof(v),"%lu",(unsigned long)g_data->iq_drops);
  detailrow("IQ DROPS",v,662,460,1040);
  detailrow("TRANSPORT",g_data->rtl_tcp?"RTL_TCP":"LOCAL / USB",662,507,1040);
  label("NO SIGNAL GRAPH WITHOUT REAL IQ METRICS",662,583,kMuted);
}
void events_page() {
  card(eventsTable);card(eventsDetail);
  title("MARITIME ACTIVITY & SAFETY EVENTS",38,187);
  for(size_t j=0;j<9;++j){
    size_t i=g_event_offset+j;if(i>=bounded_events())break;
    const Event& e=g_data->events[i];int y=243+static_cast<int>(j)*43;
    if(e.safety_related)d.fillround(35,y-17,718,38,5,0x3020);
    char v[76];std::snprintf(v,sizeof(v),"%lus", (unsigned long)e.age_s);
    label(v,45,y);
    d.text(e.title,133,y,e.safety_related?kAmber:kWhite,2);
    d.hline(38,y+20,710,kEdge);
  }
  if(!bounded_events())label("NO EVENTS OBSERVED",45,279);
  button("UP",{40,578,120,44});button("DOWN",{172,578,128,44});
  title("EVENT CLASSIFICATION",802,187);
  label("SAFETY / DISTRESS TEXT",802,247,kAmber);
  label("NEW STATION / CHANGED STATE",802,291);
  label("AtoN / VIRTUAL AtoN CHANGES",802,333);
  label("BASE STATION & CONTROL",802,375);
  d.hline(803,407,425,kEdge);
  label("ALL EVENTS COME FROM REAL PACKETS.",803,453,kCyan);
  label("NOT A CERTIFIED NAVIGATION TOOL.",803,497,kAmber);
  label("NO COLLISION CLAIMS WITHOUT VERIFIED",803,548);
  label("OWN-VESSEL MOTION / POSITION.",803,578);
}
void toggle_row(const char* name,const char* value,int x,int y,bool enabled=true) {
  label(name,x,y);d.text(value,x+340,y,enabled?kGreen:kMuted,2);
  d.hline(x,y+25,548,kEdge);
}
void setup_page() {
  card(setupReceiver);card(setupOutput);
  title("RECEIVER / RF CONFIGURATION",38,186);
  toggle_row("AIS 1 - 161.975 MHz",g_data->ais1_enabled?"ON":"OFF",42,245);
  toggle_row("AIS 2 - 162.025 MHz",g_data->ais2_enabled?"ON":"OFF",42,307);
  toggle_row("LONG RANGE - CH 75/76",g_data->long_range?"ON":"OFF",42,369);
  toggle_row("INPUT",g_data->rtl_tcp?"RTL_TCP":"LOCAL USB",42,431);
  label("LONG RANGE REQUIRES A NEW TUNE OR RADIO",42,486,kAmber);
  label("LINK / RF SETTINGS APPLY ONLY AFTER APP ACK",42,537);
  title("DATA & STORAGE",658,186);
  toggle_row("NMEA AIVDM OUTPUT",g_data->nmea_output?"ON":"OFF",662,245);
  toggle_row("MESSAGE JOURNAL",g_data->logging?"SAVING":"OFF",662,307,g_data->sd_available);
  toggle_row("SD CARD",g_data->sd_available?"AVAILABLE":"NOT FOUND",662,369,g_data->sd_available);
  toggle_row("AIS STANDARD","M.1371-6",662,431);
  label("RECEIVE ONLY / NO AIS TRANSMISSIONS",662,504,kCyan);
  label("NO PUBLIC UPLOAD WITHOUT USER OPT-IN",662,543);
  label("CONTROLS RETURN ACTIONS FOR APP TO APPLY",662,580);
}
void paint_page() {
  if(!g_data)return;
  switch(g_view) {
    case View::map:titlebar("LIVE MAP","Vessels, AtoN, stations / offline OrcMaps");map_page();break;
    case View::targets:titlebar("TARGETS","Identity, movement, freshness, static/voyage information");targets_page();break;
    case View::messages:titlebar("MESSAGES","RF-valid and interpreted AIS packet journal");messages_page();break;
    case View::protocol:titlebar("PROTOCOL","All 28 message types / raw and interpreted data");protocol_page();break;
    case View::signal:titlebar("SIGNAL","True dual-channel decoder and IQ metrics");signal_page();break;
    case View::events:titlebar("EVENTS","Notable AIS traffic / not a safety-of-life system");events_page();break;
    case View::setup:titlebar("SETUP","Receiver, channel selection, output, recording");setup_page();break;
    default:break;
  }
}
M5Canvas* create_canvas() {
  auto* c=new(std::nothrow) M5Canvas(&M5.Display);
  if(!c)return nullptr;
  c->setColorDepth(16);c->setPsram(true);
  if(!c->createSprite(1280,kBodyHeight)){delete c;return nullptr;}
  return c;
}
void destroy_canvas(){
  for(auto** c:{&g_canvas,&g_front})if(*c){(*c)->deleteSprite();delete *c;*c=nullptr;}
  g_front_valid=false;
}
void paint_direct(){
  d.set(&M5.Display,0);
  M5.Display.fillRect(0,kBodyY,1280,kBodyHeight,kBg);
  paint_page();
}
void paint(bool page_change=false) {
  if(!g_active||!g_data)return;
  if(!g_canvas){g_canvas=create_canvas();if(g_canvas)g_front=create_canvas();}
  if(!g_canvas){paint_direct();return;}
  d.set(g_canvas,kBodyY);
  g_canvas->fillSprite(kBg);
  paint_page();
  d.set(&M5.Display,0);
  acars_ui::DirtyRect rects[kPushRectCapacity];
  size_t count=0;bool whole=true;
  if(!page_change&&g_front&&g_front_valid) {
    auto* before=static_cast<const uint16_t*>(g_front->getBuffer());
    auto* after=static_cast<const uint16_t*>(g_canvas->getBuffer());
    if(before&&after){
      bool overflow=false;
      count=acars_ui::diff_rects(before,after,1280,kBodyHeight,rects,kPushRectCapacity,&overflow);
      const int64_t area=acars_ui::rect_area(rects,count);
      whole=overflow||area*100>int64_t(1280)*kBodyHeight*kFullThreshold;
    }
  }
  if(whole)paint_direct(); // single native-primitives refresh, not rotated 1.3 MB sprite sweep
  else {
    for(size_t i=0;i<count;++i){
      const auto& r=rects[i];
      M5.Display.setClipRect(r.x,kBodyY+r.y,r.w,r.h);
      g_canvas->pushSprite(0,kBodyY);
    }
    M5.Display.clearClipRect();
  }
  if(g_front){std::swap(g_canvas,g_front);g_front_valid=true;}
}
void nav(){
  M5.Display.fillRect(0,kTabsY-6,1280,720-(kTabsY-6),kBg);
  d.set(&M5.Display,0);
  for(int i=0;i<kTabsCount;++i)button(kTabs[i],tab(i),g_view==static_cast<View>(i));
}
void chrome() {
  M5.Display.fillScreen(kBg);
  audio_header::draw_brand("AIS");
  audio_header::draw_battery(g_data?g_data->battery_percent:-1);
  audio_header::draw_home_button();
  audio_header::draw_mute_button(false); // AIS is data-only; app may replace with its actual audio state
  audio_header::draw_visualizer_button(false);
  audio_header::draw_settings_button();
  nav();
}
void select_page(View v) {if(g_view==v)return;g_view=v;focus_nav::clear();nav();paint(true);}
} // namespace
void enter(const Snapshot& snapshot){
  if(!screens::is_active(screens::Id::ais))return;
  if(!g_data)g_data=new(std::nothrow) Snapshot();
  if(!g_data)return;
  *g_data=snapshot;g_active=true;g_view=View::map;g_selected_mmsi=0;
  g_selected_message=0;g_protocol=1;g_target_offset=g_message_offset=g_event_offset=g_protocol_offset=0;
  g_last_revision=snapshot.revision;g_range_nm=25;
  chrome();paint(true);
}
void update(const Snapshot& snapshot){
  if(!g_active||!g_data||!screens::owns(screens::Id::ais)||g_last_revision==snapshot.revision)return;
  int battery=g_data->battery_percent;
  *g_data=snapshot;
  g_last_revision=snapshot.revision;
  if(battery!=snapshot.battery_percent)audio_header::draw_battery(snapshot.battery_percent);
  paint(false);
}
void leave(){g_active=false;destroy_canvas();delete g_data;g_data=nullptr;}
bool active(){return g_active;}
View view(){return g_view;}
uint8_t protocol_type(){return g_protocol;}
void show_preview_view(View v,const Snapshot& snapshot){
  if(!screens::is_active(screens::Id::ais)||v>=View::count)return;
  if(!g_active)enter(snapshot);else update(snapshot);
  select_page(v);
}
Action handle_touch(int32_t x,int32_t y){
  if(!g_active||!g_data||!screens::owns(screens::Id::ais))return {};
  if(audio_header::home_hit(x,y))return {ActionKind::close};
  if(audio_header::settings_hit(x,y))return {ActionKind::open_settings};
  for(int i=0;i<kTabsCount;++i)if(hit(tab(i),x,y)){select_page(static_cast<View>(i));return {};}
  if(g_view==View::map) {
    if(hit({718,166,42,44},x,y)||hit({772,166,42,44},x,y)){
      const bool zoom_in=x<770;
      const uint16_t values[]={10,25,50,100};
      size_t i=0;while(i<3&&values[i]<g_range_nm)++i;
      if(zoom_in&&i>0)--i;else if(!zoom_in&&i<3)++i;
      g_range_nm=values[i];paint(true);
      return {ActionKind::set_range_nm,g_range_nm};
    }
  } else if(g_view==View::targets){
    for(size_t j=0;j<8;++j){
      const size_t i=g_target_offset+j;
      if(i<bounded_targets()&&hit({32,247+static_cast<int>(j)*43,690,42},x,y)){
        g_selected_mmsi=g_data->targets[i].mmsi;paint();return {ActionKind::target_selected,g_selected_mmsi};
      }
    }
    if(hit({34,574,126,44},x,y)&&g_target_offset>0)--g_target_offset;
    if(hit({170,574,126,44},x,y)&&g_target_offset+8<bounded_targets())++g_target_offset;
    paint();
  } else if(g_view==View::messages){
    for(size_t j=0;j<9;++j){
      const size_t i=g_message_offset+j;
      if(i<bounded_messages()&&hit({30,246+static_cast<int>(j)*38,800,37},x,y)){
        g_selected_message=static_cast<uint8_t>(i);paint();return {ActionKind::message_selected,static_cast<uint32_t>(i)};
      }
    }
    if(hit({36,579,118,44},x,y)&&g_message_offset>0)--g_message_offset;
    if(hit({164,579,125,44},x,y)&&g_message_offset+9<bounded_messages())++g_message_offset;
    paint();
  } else if(g_view==View::protocol){
    for(int j=0;j<10;++j){
      const int type=g_protocol_offset+j+1;
      if(type<=28&&hit({32,241+j*35,464,34},x,y)){
        g_protocol=static_cast<uint8_t>(type);paint();return {ActionKind::set_message_type,g_protocol};
      }
    }
    if(hit({44,578,113,44},x,y)&&g_protocol_offset>0)--g_protocol_offset;
    if(hit({169,578,124,44},x,y)&&g_protocol_offset<18)++g_protocol_offset;
    paint();
  } else if(g_view==View::events){
    if(hit({40,578,120,44},x,y)&&g_event_offset>0)--g_event_offset;
    if(hit({172,578,128,44},x,y)&&g_event_offset+9<bounded_events())++g_event_offset;
    paint();
  } else if(g_view==View::setup){
    if(hit({42,224,548,44},x,y))return {ActionKind::toggle_ais1};
    if(hit({42,286,548,44},x,y))return {ActionKind::toggle_ais2};
    if(hit({42,348,548,44},x,y))return {ActionKind::toggle_long_range};
    if(hit({42,410,548,44},x,y))return {ActionKind::set_receiver};
    if(hit({662,224,550,44},x,y))return {ActionKind::toggle_nmea};
    if(hit({662,286,550,44},x,y))return {ActionKind::toggle_logging};
  }
  return {};
}
bool self_check(){
  return kTabsCount==7&&kBodyY>=104&&kBodyY+kBodyHeight<kTabsY&&
         kContent.y>kContext.y&&g_protocol>=1&&g_protocol<=28&&\n         !kReservedStatus.overlaps(kContext)&&!mapMain.overlaps(mapSummary);
}
} // namespace orcsdr::ais_ui
