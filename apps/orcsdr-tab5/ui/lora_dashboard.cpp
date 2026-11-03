#include "focus_nav.hpp"
#include "lora_dashboard.hpp"
#include "lora_tx_views.hpp"
#include "lora_tx_native_layout.hpp"
#include "lora_rx_layout.hpp"
#include "lora_rx_data_views.hpp"

#include "dashboard_audio_control.hpp"
#include "map_view.hpp"
#include "map_preferences.hpp"
#include "receiver_location.hpp"
#include "screen_controller.hpp"
#include "setup_flow.hpp"
#include "orc_badge.hpp"
#include "lora_channel_control.hpp"
#include "waterfall_style.hpp"

#include <M5Unified.h>
#include <esp_attr.h>

#include <algorithm>
#include <iterator>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace orcsdr::lora {
namespace {

constexpr uint16_t kBg = TFT_BLACK;
constexpr uint16_t kPanel = 0x0841;
constexpr uint16_t kCyan = 0x2e7f;
constexpr uint16_t kGreen = 0x6fe8;
constexpr uint16_t kYellow = 0xff24;
constexpr uint16_t kMuted = 0x8c71;
constexpr uint16_t kGrid = 0x2945;
constexpr int kHeaderH = 120;
constexpr int kTabsY = 640;
constexpr int kTabW = 256;  // RX-only five-tab layout is unchanged.
constexpr uint8_t kRxTabCount = 5;
constexpr uint8_t kTxTabCount = 6;
constexpr int kPlotX = 44;
constexpr int kPlotY = 270;
constexpr int kPlotW = 780;
constexpr int kPlotH = 124;
constexpr int kWaterfallY = 402;
constexpr int kWaterfallH = 126;
constexpr uint32_t kDynamicRefreshIntervalMs = 1000;
constexpr uint32_t kSpectrumRefreshIntervalMs = 33;
constexpr float kSpectrumRangeDb = 42.0f;
constexpr size_t kSpectrumTraceCapacity = 256;
constexpr size_t kTrafficRows = 5;

Snapshot g_snapshot{};
View g_view = View::overview;
tx_views::Page g_tx_page = tx_views::Page::operator_panel;
tx_native::Layout g_tx_layout{};
bool supported_event(const Event&);
audio_header::HeaderRect g_rx_viewport{0,0,1280,720};
rx_layout::Layout g_rx_layout=rx_layout::make(g_rx_viewport,View::overview,false);
bool g_tx_entry_on_enter=false;
size_t g_node_offset=0;
rx_data_views::Cache g_node_cache{},g_traffic_cache{};
uint32_t g_metric_keys[6]{},g_action_keys[6]{},g_preview_key=0,g_map_detail_key=0;
void refresh_rx_layout(){g_rx_layout=rx_layout::make(g_rx_viewport,g_view,g_snapshot.tx.readiness!=lora_tx::Readiness::absent);}
void reset_rx_cache(){
  g_node_cache={};g_traffic_cache={};
  std::fill(std::begin(g_metric_keys),std::end(g_metric_keys),0);
  std::fill(std::begin(g_action_keys),std::end(g_action_keys),0);
  g_preview_key=g_map_detail_key=0;
}
uint32_t rx_key(const char* value,uint32_t salt=1){
  uint32_t key=2166136261u^salt;
  while(*value){key^=static_cast<uint8_t>(*value++);key*=16777619u;}
  return key?key:1;
}
void rx_text(const char* value,rx_layout::Rect r,uint16_t color=TFT_WHITE,int size=2){
  if(r.w<12||r.h<16)return;
  char clipped[160]{};std::snprintf(clipped,sizeof(clipped),"%s",value);
  M5.Display.setTextSize(size);M5.Display.setTextDatum(middle_left);M5.Display.setTextColor(color);
  size_t count=std::strlen(clipped);
  while(count&&M5.Display.textWidth(clipped)>r.w){clipped[--count]=0;}
  if(count<std::strlen(value)&&count>=3)std::memcpy(clipped+count-3,"...",3);
  M5.Display.drawString(clipped,r.x,r.y+r.h/2);
}
void rx_card(rx_layout::Rect r,uint16_t border=kCyan){if(r.w>0&&r.h>0){M5.Display.fillRoundRect(r.x,r.y,r.w,r.h,8,kPanel);M5.Display.drawRoundRect(r.x,r.y,r.w,r.h,8,border);}}
void rx_button(rx_layout::Rect r,const char* value,bool enabled=true,bool selected=false){
  M5.Display.fillRoundRect(r.x,r.y,r.w,r.h,6,selected?0x0a43:kPanel);
  M5.Display.drawRoundRect(r.x,r.y,r.w,r.h,6,enabled?(selected?kGreen:kCyan):kGrid);
  rx_text(value,{r.x+8,r.y,r.w-16,r.h},enabled?TFT_WHITE:kMuted);
  if(enabled)focus_nav::note(r.x,r.y,r.w,r.h);
}
bool native_tx_screen() { return g_tx_layout.valid && g_view==View::tx_lab; }

// This reads the logical M5GFX design dimensions, NOT the panel's physical
// dimensions. The P4 ScaledDisplay defaults to a 1280x720 logical canvas;
// its eventual LilyGO panel backend must explicitly request 568x1232 here.
bool portrait_lora_screen() {
  return tx_views::portrait(M5.Display.width(), M5.Display.height());
}
bool portrait_tx_screen() {
  return portrait_lora_screen() && g_snapshot.tx.readiness != lora_tx::Readiness::absent;
}
bool g_active = false;
bool g_channels_open = false;
bool g_follow_node = false;
uint32_t g_follow_node_id = 0;
bool g_node_details = false;
bool g_traffic_details = false;
bool g_map_center_set = false;
bool g_map_label_error = false;
orcmap::Viewport g_map_camera;
uint32_t g_map_revision_seen=0;
bool g_map_drag=false,g_map_moved=false;
int32_t g_map_start_x=0,g_map_start_y=0,g_map_last_x=0,g_map_last_y=0;

constexpr int kMapZoomSize=44,kMapZoomStep=52;
uint8_t g_node_filter = 0;
uint8_t g_traffic_filter = 0;
size_t g_traffic_offset = 0;
// State the header showed when it was last painted. The header is part of the
// static draw, so without this it kept "RX STOPPED" (the capture starts just
// after the screen opens) until the screen was re-entered.
bool g_header_running = false;
bool g_header_wifi = false;
bool g_header_decoder = false;
bool g_header_key = false;
uint32_t g_last_dynamic_ms = 0;
uint32_t g_last_spectrum_ms = 0;
EXT_RAM_BSS_ATTR uint16_t g_waterfall_row[128]{};  // PSRAM, as in the AM dashboard
int16_t g_previous_spectrum_y[kSpectrumTraceCapacity]{};
size_t g_previous_spectrum_bins = 0;
View g_previous_spectrum_view = View::count;
float g_waterfall_floor = -120.0f;
bool g_waterfall_floor_valid = false;

bool hit(int32_t x, int32_t y, int bx, int by, int bw, int bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

bool has_position(const Node& node) {
  return node.latitude_e7>=-900000000&&node.latitude_e7<=900000000&&
         node.longitude_e7>=-1800000000&&node.longitude_e7<=1800000000;
}
bool has_map_position(const Node& node) {
  return has_position(node)&&node.latitude_e7>=-850511287&&node.latitude_e7<=850511287;
}

bool supported_event(const Event& event) {
  return event.port == 1 || event.port == 3 || event.port == 4 || event.port == 5 ||
         event.port == 7 || event.port == 67;
}

rx_data_views::DataState node_state(){
  rx_data_views::DataState state{};state.offset=g_node_offset;state.details=g_node_details;
  for(size_t i=0;i<std::min<size_t>(g_snapshot.node_count,kNodeCapacity);++i)
    if(!g_node_filter||g_snapshot.nodes[i].favorite)state.indices[state.count++]=uint8_t(i);
  return state;
}
rx_data_views::DataState traffic_state(){
  rx_data_views::DataState state{};state.offset=g_traffic_offset;state.details=g_traffic_details;
  for(size_t i=0;i<std::min<size_t>(g_snapshot.event_count,kEventCapacity);++i)
    if(!g_traffic_filter||supported_event(g_snapshot.events[i]))state.indices[state.count++]=uint8_t(i);
  return state;
}

float estimate_noise_floor(const float* levels, size_t first_bin, size_t visible_bins) {
  float minimum = levels[first_bin];
  for (size_t i = 1; i < visible_bins; ++i)
    minimum = std::min(minimum, levels[first_bin + i]);
  float sum = 0.0f;
  size_t count = 0;
  for (size_t i = 0; i < visible_bins; ++i) {
    const float level = levels[first_bin + i];
    if (level <= minimum + 12.0f) {
      sum += level;
      ++count;
    }
  }
  return (count ? sum / static_cast<float>(count) : minimum) - 3.0f;
}

uint16_t waterfall_color(float level) {
  return waterfall_style::color565(waterfall_style::Screen::lora, level);
}

void draw_lora_channel_markers() {
  const auto p=g_rx_layout.plot,w=g_rx_layout.waterfall;
  if(!g_snapshot.span_hz||p.w<4||p.h<4)return;
  const int half_width=std::min<int>((p.w-2)/2,uint64_t(p.w)*g_snapshot.bandwidth_hz/g_snapshot.span_hz/2);
  const int center=p.x+p.w/2;
  for(int edge:{center-half_width,center+half_width}){
    M5.Display.drawFastVLine(edge,p.y+1,p.h-2,kYellow);
    if(w.h>2)M5.Display.drawFastVLine(edge,w.y+1,w.h-2,kGrid);
  }
  M5.Display.drawFastVLine(center,p.y+1,p.h-2,kGreen);
}

size_t visible_event_count() {
  size_t count = 0;
  for (size_t i = 0; i < g_snapshot.event_count; ++i)
    if (g_traffic_filter == 0 || supported_event(g_snapshot.events[i])) ++count;
  return count;
}

const Event* visible_event_at(size_t row) {
  for (size_t i = 0, visible = 0; i < g_snapshot.event_count; ++i) {
    if (g_traffic_filter != 0 && !supported_event(g_snapshot.events[i])) continue;
    if (visible++ == row) return &g_snapshot.events[i];
  }
  return nullptr;
}

size_t traffic_max_offset() {
  const size_t count=visible_event_count(),rows=std::max(1,g_rx_layout.traffic_row_count);
  return count>rows?count-rows:0;
}

const Node* visible_node_at(size_t row, size_t* snapshot_index = nullptr) {
  size_t visible = 0;
  for (size_t i = 0; i < g_snapshot.node_count; ++i) {
    if (g_node_filter != 0 && !g_snapshot.nodes[i].favorite) continue;
    if (visible++ != row) continue;
    if (snapshot_index) *snapshot_index = i;
    return &g_snapshot.nodes[i];
  }
  return nullptr;
}

const Node* selected_positioned_node() {
  if (g_snapshot.node_count<=kNodeCapacity&&g_snapshot.selected_node<g_snapshot.node_count) {
    const Node& selected = g_snapshot.nodes[g_snapshot.selected_node];
    if (has_map_position(selected)) return &selected;
  }
  return nullptr;
}

const Node* followed_node() {
  if (g_snapshot.node_count > kNodeCapacity) return nullptr;
  for (size_t i=0;i<g_snapshot.node_count;++i)
    if (g_snapshot.nodes[i].id==g_follow_node_id) return &g_snapshot.nodes[i];
  return nullptr;
}

bool center_map(int32_t latitude,int32_t longitude) {
  if(latitude<-850511287||latitude>850511287||longitude<-1800000000||longitude>1800000000)return false;
  if(!g_map_center_set)
    g_map_center_set=maps::regional_view(latitude,longitude,15.0,g_rx_layout.map.w,g_rx_layout.map.h,&g_map_camera);
  else return orcmap::SetCenter(&g_map_camera,latitude/1e7,longitude/1e7);
  return g_map_center_set;
}

void follow_selected_position() {
  const Node* node = g_follow_node ? followed_node() : selected_positioned_node();
  if (!node || !has_map_position(*node)) return;
  center_map(node->latitude_e7,node->longitude_e7);
}

void text(const char* value, int x, int y, uint16_t color = TFT_WHITE, int size = 2,
          textdatum_t datum = middle_center) {
  M5.Display.setTextDatum(datum);
  M5.Display.setTextSize(size);
  M5.Display.setTextColor(color);
  M5.Display.drawString(value, x, y);
}

void card(int x, int y, int w, int h, uint16_t border = kCyan) {
  M5.Display.fillRoundRect(x, y, w, h, 10, kPanel);
  M5.Display.drawRoundRect(x, y, w, h, 10, border);
}

void button(int x, int y, int w, int h, const char* title, uint16_t color = kCyan,
            bool selected = false) {
  orcsdr::focus_nav::note(x, y, w, h);
  M5.Display.fillRoundRect(x, y, w, h, 9, selected ? 0x1264 : kPanel);
  M5.Display.drawRoundRect(x, y, w, h, 9, color);
  text(title, x + w / 2, y + h / 2, selected ? color : TFT_WHITE, 2);
}

void draw_radio_icon(int cx, int cy, uint16_t color) {
  M5.Display.drawCircle(cx, cy - 8, 6, color);
  M5.Display.drawCircle(cx, cy - 8, 16, color);
  M5.Display.drawCircle(cx, cy - 8, 26, color);
  M5.Display.drawLine(cx, cy, cx - 14, cy + 35, color);
  M5.Display.drawLine(cx, cy, cx + 14, cy + 35, color);
  M5.Display.drawFastHLine(cx - 19, cy + 35, 38, color);
}

void draw_star(int cx, int cy, uint16_t color) {
  const int8_t x[] = {0, 7, -11, 11, -7, 0};
  const int8_t y[] = {-11, 9, -3, -3, 9, -11};
  for (size_t i = 1; i < std::size(x); ++i)
    M5.Display.drawLine(cx + x[i - 1], cy + y[i - 1], cx + x[i], cy + y[i], color);
}

void draw_header(bool structure=false) {
  g_header_running=g_snapshot.running;g_header_wifi=g_snapshot.wifi_connected;
  g_header_decoder=g_snapshot.native_decoder_ready;g_header_key=g_snapshot.key_loaded;
  if(structure){
    audio_header::draw_brand("LORA / MESH");audio_header::draw_home_button();
    audio_header::draw_settings_button();audio_header::draw_mute_button(g_snapshot.sound_enabled);
    audio_header::draw_battery(M5.Power.getBatteryLevel());
  }
  const auto r=g_rx_layout.header_status;
  M5.Display.fillRect(r.x,r.y,r.w,r.h,kBg);
  char line[96];
  std::snprintf(line,sizeof(line),"%s / %s",g_snapshot.running?"RTL RX":"RTL STOPPED",g_snapshot.wifi_connected?"WI-FI ON":"WI-FI OFF");
  rx_text(line,{r.x+8,r.y+2,r.w-16,24},g_snapshot.running?kGreen:kMuted);
  std::snprintf(line,sizeof(line),"%.12s SF%u BW%luk / %s",g_snapshot.region[0]?g_snapshot.region:"US",g_snapshot.sf,(unsigned long)(g_snapshot.bandwidth_hz/1000),g_snapshot.native_decoder_ready?"DECODE":"NO DECODER");
  rx_text(line,{r.x+8,r.y+28,r.w-16,24},g_snapshot.native_decoder_ready?kCyan:kYellow);
}

bool tx_profile_visible() {
  return g_snapshot.tx.readiness != lora_tx::Readiness::absent;
}
int tab_width() { return tx_profile_visible() ? 1280 / kTxTabCount : kTabW; }

void draw_tab_icon(View view, int cx, int cy, uint16_t color) {
  if (view == View::overview) {
    M5.Display.drawFastHLine(cx - 24, cy + 8, 48, color);
    M5.Display.drawLine(cx - 24, cy + 8, cx, cy - 18, color);
    M5.Display.drawLine(cx, cy - 18, cx + 24, cy + 8, color);
    M5.Display.drawFastVLine(cx - 8, cy + 8, 18, color);
  } else if (view == View::nodes) {
    M5.Display.drawCircle(cx, cy - 12, 7, color);
    M5.Display.drawCircle(cx - 18, cy + 15, 6, color);
    M5.Display.drawCircle(cx + 18, cy + 15, 6, color);
    M5.Display.drawLine(cx, cy - 5, cx - 18, cy + 9, color);
    M5.Display.drawLine(cx, cy - 5, cx + 18, cy + 9, color);
  } else if (view == View::traffic) {
    const int16_t xs[] = {-25, -17, -10, -2, 5, 13, 20, 26};
    const int16_t ys[] = {0, 0, -16, 17, -22, 10, 0, 0};
    for (int i = 1; i < 8; ++i)
      M5.Display.drawLine(cx + xs[i - 1], cy + ys[i - 1], cx + xs[i], cy + ys[i], color);
  } else if (view == View::map) {
    M5.Display.drawCircle(cx, cy, 20, color);
    M5.Display.drawCircle(cx, cy, 5, color);
    M5.Display.drawLine(cx, cy + 20, cx - 18, cy + 33, color);
    M5.Display.drawLine(cx, cy + 20, cx + 18, cy + 33, color);
  } else if (view == View::tx_lab) {
    // An unfilled transmit antenna: a control panel, not a TX-ready indicator.
    M5.Display.drawLine(cx - 13, cy + 28, cx, cy - 24, color);
    M5.Display.drawLine(cx, cy - 24, cx + 13, cy + 28, color);
    M5.Display.drawFastHLine(cx - 18, cy + 28, 36, color);
    M5.Display.drawCircle(cx, cy - 20, 7, color);
  } else {
    const int16_t xs[] = {-27, -18, -10, -2, 5, 12, 20, 28};
    const int16_t ys[] = {0, 0, -19, 19, -24, 13, 0, 0};
    for (int i = 1; i < 8; ++i)
      M5.Display.drawLine(cx + xs[i - 1], cy + ys[i - 1], cx + xs[i], cy + ys[i], color);
  }
}

void draw_tabs() {
  const char* names[]={"OVERVIEW","NODES","TRAFFIC","MAP","RF HEALTH","TX LAB"};
  const char* short_names[]={"RX","NODES","TRAFFIC","MAP","HEALTH","TX LAB"};
  for(int i=0;i<g_rx_layout.tab_count;++i){
    const auto r=g_rx_layout.tab[i];M5.Display.setTextSize(2);
    const char* name=M5.Display.textWidth(names[i])<=r.w-16?names[i]:short_names[i];
    rx_button(r,name,true,i==int(g_view));
  }
}

void format_id(char* out, size_t size, uint32_t id) {
  if (id == 0) strlcpy(out, "—", size);
  else snprintf(out, size, "!%08lX", static_cast<unsigned long>(id));
}

void format_coord(char* out, size_t size, int32_t value) {
  if (value == INT32_MAX) strlcpy(out, "—", size);
  else snprintf(out, size, "%.5f", value / 10000000.0);
}

void node_name(const Node& node, char* out, size_t size) {
  if (node.name[0]) strlcpy(out, node.name, size);
  else format_id(out, size, node.id);
}

const Node* find_node(uint32_t id) {
  for (size_t i = 0; i < g_snapshot.node_count; ++i)
    if (g_snapshot.nodes[i].id == id) return &g_snapshot.nodes[i];
  return nullptr;
}

void event_sender(const Event& event, char* out, size_t size, bool include_id) {
  const Node* node = find_node(event.sender);
  if (node != nullptr && node->name[0]) {
    if (include_id)
      snprintf(out, size, "%s  !%08lX", node->name, static_cast<unsigned long>(event.sender));
    else
      strlcpy(out, node->name, size);
  } else {
    format_id(out, size, event.sender);
  }
}

void draw_metric(int x, int y, int w, const char* title, const char* value,
                 uint16_t color = TFT_WHITE) {
  card(x, y, w, 88);
  text(title, x + 16, y + 20, kCyan, 1, middle_left);
  text(value, x + 16, y + 56, color, 3, middle_left);
}

constexpr int kStyleChipY = 146, kStyleChipH = 44, kStyleChipW = 118;
constexpr int kPaletteChipX = 760, kSpeedChipX = 888;

void draw_style_chip(int x, const char* title, const char* value) {
  orcsdr::focus_nav::note(x, kStyleChipY, kStyleChipW, kStyleChipH);
  M5.Display.fillRoundRect(x, kStyleChipY, kStyleChipW, kStyleChipH, 8, kPanel);
  M5.Display.drawRoundRect(x, kStyleChipY, kStyleChipW, kStyleChipH, 8, kCyan);
  text(title, x + kStyleChipW / 2, kStyleChipY + 13, kCyan, 1);
  text(value, x + kStyleChipW / 2, kStyleChipY + 31, kGreen, 2);
}

void draw_style_chips() {
  using waterfall_style::Screen;
  draw_style_chip(kPaletteChipX, "PALETTE",
                  waterfall_style::palette_name(waterfall_style::palette(Screen::lora)));
  draw_style_chip(kSpeedChipX, "SPEED",
                  waterfall_style::speed_name(waterfall_style::speed(Screen::lora)));
}


void draw_rx_metrics() {
  const char* names[]={"FREQ MHz","PROFILE","SIGNAL","PACKETS","CRC OK","NODES"};
  const char* health[]={"MSPS","USB DROP","IQ DROP","ENCRYPTED","DECODER","RX MODE"};
  char value[64];
  for(int i=0;i<g_rx_layout.metric_count;++i){
    if(g_view==View::rf_health){
      switch(i){case 0:std::snprintf(value,sizeof(value),"%.3f",g_snapshot.effective_sps/1e6);break;
      case 1:std::snprintf(value,sizeof(value),"%lu",(unsigned long)g_snapshot.usb_overruns);break;
      case 2:std::snprintf(value,sizeof(value),"%lu",(unsigned long)g_snapshot.consumer_drops);break;
      case 3:std::snprintf(value,sizeof(value),"%lu",(unsigned long)g_snapshot.encrypted_frames);break;
      case 4:std::snprintf(value,sizeof(value),"%s",g_snapshot.native_decoder_ready?"READY":"WAIT");break;
      default:std::snprintf(value,sizeof(value),"RX ONLY");break;}
    }else{
      switch(i){case 0:std::snprintf(value,sizeof(value),"%.3f",g_snapshot.frequency_hz/1e6);break;
      case 1:std::snprintf(value,sizeof(value),"%s",g_snapshot.profile);break;
      case 2:std::snprintf(value,sizeof(value),"%.1f",g_snapshot.relative_dbfs);break;
      case 3:std::snprintf(value,sizeof(value),"%lu",(unsigned long)g_snapshot.decoded_frames);break;
      case 4:std::snprintf(value,sizeof(value),"%lu",(unsigned long)g_snapshot.crc_ok);break;
      default:std::snprintf(value,sizeof(value),"%u",g_snapshot.node_count);break;}
    }
    const auto key=rx_key(value,unsigned(i)+1);if(g_metric_keys[i]==key)continue;g_metric_keys[i]=key;
    const auto r=g_rx_layout.metric[i];rx_card(r,kGrid);
    rx_text(g_view==View::rf_health?health[i]:names[i],{r.x+8,r.y+2,r.w-16,22},kGreen);
    rx_text(value,{r.x+8,r.y+26,r.w-16,24});
  }
}
void draw_rx_actions(){
  const char* label[6]={};bool enabled[6]={true,true,true,true,true,true};bool selected[6]={};
  if(g_view==View::overview||g_view==View::rf_health){
    label[0]=g_snapshot.survey_active?"STOP SCAN":"SCAN BAND";
    label[1]=g_snapshot.iq_recording?"STOP IQ":g_snapshot.iq_busy?"DECODING IQ":g_snapshot.iq_ready?(g_snapshot.iq_save_available?"SAVE IQ":"IQ CAPTURE READY"):"RECORD IQ";
    enabled[0]=g_snapshot.running||g_snapshot.survey_active;
    enabled[1]=g_snapshot.native_decoder_ready&&!g_snapshot.iq_busy&&(!g_snapshot.iq_ready||g_snapshot.iq_save_available);
    selected[0]=g_snapshot.survey_active;selected[1]=g_snapshot.iq_recording;
    label[2]=g_view==View::overview?"CHANNELS":g_snapshot.export_available?"EXPORT LOG":"NO EXPORT";
    label[3]=g_view==View::overview?(g_snapshot.logging_available?(g_snapshot.sd_logging?"SD LOG ON":"SD LOG OFF"):"NO SD LOG"):"CLEAR EVENTS";
    enabled[2]=g_view==View::overview||g_snapshot.export_available;
    enabled[3]=g_view!=View::overview||g_snapshot.logging_available;
    label[4]="PALETTE";label[5]="WF SPEED";
  }else if(g_view==View::nodes){
    label[0]=g_node_filter?"FAVORITES":"FILTER ALL";selected[0]=g_node_filter!=0;
    label[1]="FAVORITE";enabled[1]=g_snapshot.node_count>0;
    label[2]=g_node_details?"HIDE DETAILS":"VIEW DETAILS";selected[2]=g_node_details;
    label[3]=g_snapshot.export_available?"EXPORT LOG":"NO EXPORT";enabled[3]=g_snapshot.export_available;
  }else if(g_view==View::traffic){
    label[0]=g_traffic_details?"MESSAGE TEXT":"PACKET DETAILS";selected[0]=g_traffic_details;
    label[1]=g_snapshot.export_available?"EXPORT LOG":"NO EXPORT";enabled[1]=g_snapshot.export_available;
    label[2]=g_traffic_filter?"SUPPORTED":"FILTER ALL";selected[2]=g_traffic_filter!=0;label[3]="CLEAR EVENTS";
  }else if(g_view==View::map){
    label[0]=g_rx_layout.vertical?"NODE":"NODE CENTER";enabled[0]=selected_positioned_node()!=nullptr;
    label[1]=g_follow_node?"UNFOLLOW":g_rx_layout.vertical?"FOLLOW":"FOLLOW NODE";enabled[1]=enabled[0]||g_follow_node;selected[1]=g_follow_node;
    label[2]=g_map_label_error?"SAVE FAILED":maps::preferences::labels(maps::preferences::Consumer::lora)?"LABELS ON":"LABELS OFF";
    label[3]="RECEIVER";enabled[3]=receiver_location::snapshot().configured;
    label[4]="ZOOM +";label[5]="ZOOM -";enabled[4]=enabled[5]=g_map_center_set;
  }
  for(int i=0;i<g_rx_layout.action_count;++i){
    if(!label[i])continue;
    const auto key=rx_key(label[i],(enabled[i]?1:2)+(selected[i]?4:0));
    if(g_action_keys[i]==key)continue;
    g_action_keys[i]=key;
    rx_button(g_rx_layout.actions[i],label[i],enabled[i],selected[i]);
  }
}
void draw_rx_preview(){
  const auto r=g_rx_layout.events;if(r.w<44||r.h<44)return;
  char key_text[320]{};
  const bool survey=g_view==View::overview&&g_snapshot.survey_result_count;
  if(survey)std::snprintf(key_text,sizeof(key_text),"scan %u %.3f %.1f %.3f %.1f",g_snapshot.survey_result_count,g_snapshot.survey_results[0].frequency_hz/1e6,g_snapshot.survey_results[0].level_dbfs,g_snapshot.survey_results[1].frequency_hz/1e6,g_snapshot.survey_results[1].level_dbfs);
  else std::snprintf(key_text,sizeof(key_text),"%u %lu %lu %.111s %.111s",g_snapshot.event_count,(unsigned long)g_snapshot.events[0].packet_id,(unsigned long)g_snapshot.events[1].packet_id,g_snapshot.events[0].text,g_snapshot.events[1].text);
  const auto key=rx_key(key_text);if(g_preview_key==key)return;g_preview_key=key;
  rx_card(r);rx_text(survey?"SCAN RESULTS":"RECENT PACKETS",{r.x+8,r.y+4,r.w-16,24},kCyan);
  int y=r.y+34;
  if(!survey&&!g_snapshot.event_count)rx_text("WAITING FOR TRAFFIC",{r.x+8,y,r.w-16,24},kMuted);
  for(int i=0;i<3&&y+40<r.y+r.h-8;++i){
    char line[128]{};
    if(survey){
      if(i>=g_snapshot.survey_result_count)break;
      std::snprintf(line,sizeof(line),"%.3f MHz",g_snapshot.survey_results[i].frequency_hz/1e6);rx_text(line,{r.x+8,y,r.w-16,20});
      std::snprintf(line,sizeof(line),"%.1f dBFS",g_snapshot.survey_results[i].level_dbfs);rx_text(line,{r.x+8,y+24,r.w-16,20},kGreen);
    }else{
      if(i>=g_snapshot.event_count)break;
      const auto& event=g_snapshot.events[i];
      std::snprintf(line,sizeof(line),"%s %08lX",event.source==PacketSource::sx1262?"SX1262":"RTL",(unsigned long)event.sender);rx_text(line,{r.x+8,y,r.w-16,20},event.verified?kGreen:kYellow);
      rx_text(event.verified?(event.text[0]?event.text:"NO MESSAGE TEXT"):"UNVERIFIED / HIDDEN",{r.x+8,y+24,r.w-16,20});
    }
    y+=56;
  }
}
void draw_plot_static() {
  const auto p=g_rx_layout.plot,w=g_rx_layout.waterfall;
  M5.Display.fillRect(p.x,p.y,p.w,p.h,kBg);M5.Display.drawRect(p.x,p.y,p.w,p.h,kGrid);
  M5.Display.fillRect(w.x,w.y,w.w,w.h,kBg);M5.Display.drawRect(w.x,w.y,w.w,w.h,kCyan);
  M5.Display.setScrollRect(w.x+1,w.y+1,w.w-2,w.h-2,kBg);
  draw_lora_channel_markers();
}

void draw_overview_static() { draw_plot_static(); }

void draw_nodes_static(){rx_data_views::draw_nodes_static(M5.Display,g_rx_layout);}

void draw_traffic_static(){rx_data_views::draw_traffic_static(M5.Display,g_rx_layout);}

void draw_map_label_control(){g_action_keys[2]=0;draw_rx_actions();}
void draw_map_controls(){draw_rx_actions();}
void draw_map_static(){rx_card(g_rx_layout.map);rx_card(g_rx_layout.map_info);draw_map_controls();}

void draw_tx_static() {
  const bool vertical = portrait_tx_screen();
  tx_views::draw_static(M5.Display, g_snapshot, g_tx_page, vertical);
  tx_views::menu(M5.Display, g_tx_page, vertical);
}

void draw_portrait_tx_header() {
  M5.Display.fillRect(0, 0, 568, 139, kBg);
  // The real OrcSDR badge asset, not a substituted icon.
  if (!badge::draw(18, 16, 86)) {
    M5.Display.drawRoundRect(18, 16, 86, 86, 9, kGreen);
    text("O", 61, 59, kGreen, 4);
  }
  text("OrcSDR", 119, 37, TFT_WHITE, 3, middle_left);
  text(g_snapshot.board_label, 119, 72, kCyan, 1, middle_left);
  M5.Display.fillRoundRect(432, 20, 115, 62, 9, kPanel);
  M5.Display.drawRoundRect(432, 20, 115, 62, 9, kGreen);
  text("HOME", 489, 51, kGreen, 2);
  M5.Display.drawFastHLine(8, 139, 552, kCyan);
  text("LORA / SX1262 TX", 19, 121, TFT_WHITE, 2, middle_left);
  text("TX LAB", 475, 121, kYellow, 1, middle_left);
}

void draw_portrait_no_radio() {
  card(16, 225, 536, 360, kYellow);
  text("SX1262 NOT AVAILABLE", 34, 262, kYellow, 2, middle_left);
  text("This device has no verified LoRa radio", 34, 320, kMuted, 1, middle_left);
  text("TX controls are unavailable", 34, 372, kMuted, 1, middle_left);
  text("HOME returns to the main application", 34, 425, kMuted, 1, middle_left);
}

void draw_portrait_tx_footer() {
  M5.Display.fillRect(0, 1145, 568, 87, kPanel);
  M5.Display.drawFastHLine(0, 1145, 568, kCyan);
  const bool armed_and_admitted = g_snapshot.tx.operator_armed &&
      lora_tx::can_arm(g_snapshot.tx) == lora_tx::Denial::none;
  text("SX1262", 24, 1186, kCyan, 2, middle_left);
  text("TX LAB", 249, 1186, kGreen, 2, middle_left);
  text(armed_and_admitted ? "ARMED" : "LOCKED", 440, 1186, kYellow, 2, middle_left);
}

void draw_health_static(){draw_plot_static();}

void split_message(const char* value, size_t line_chars, char* first, char* second) {
  const size_t length = strlen(value);
  size_t split = std::min(length, line_chars);
  if (split < length) {
    for (size_t i = split; i > line_chars / 2; --i) {
      if (value[i - 1] == ' ' || value[i - 1] == ',') {
        split = value[i - 1] == ' ' ? i - 1 : i;
        break;
      }
    }
  }
  memcpy(first, value, split);
  first[split] = '\0';
  const char* remainder = value + split;
  while (*remainder == ' ') ++remainder;
  strlcpy(second, remainder, line_chars + 1);
  if (strlen(remainder) > line_chars) memcpy(second + line_chars - 3, "...", 4);
}

void format_event_time(char* output, size_t output_size, const Event& event,
                       uint32_t now_ms, bool include_age = true) {
  const uint32_t age = event.sender == 0 ? 0 : (now_ms - event.received_ms) / 1000u;
  if (event.received_utc == 0) {
    if (include_age)
      snprintf(output, output_size, "TIME NOT SET | %lus", static_cast<unsigned long>(age));
    else
      strlcpy(output, "TIME NOT SET", output_size);
    return;
  }
  const uint32_t day_seconds = event.received_utc % 86400u;
  if (include_age)
    snprintf(output, output_size, "%02lu:%02lu:%02luZ | %lus",
             static_cast<unsigned long>(day_seconds / 3600u),
             static_cast<unsigned long>((day_seconds / 60u) % 60u),
             static_cast<unsigned long>(day_seconds % 60u),
             static_cast<unsigned long>(age));
  else
    snprintf(output, output_size, "%02lu:%02lu:%02luZ",
             static_cast<unsigned long>(day_seconds / 3600u),
             static_cast<unsigned long>((day_seconds / 60u) % 60u),
             static_cast<unsigned long>(day_seconds % 60u));
}

void format_event_details(char* output, size_t output_size, const Event& event) {
  char destination[12]{};
  char signal[12] = "—";
  char snr[12] = "—";
  format_id(destination, sizeof(destination), event.destination);
  if (event.signal_tenths != INT16_MAX)
    snprintf(signal, sizeof(signal), "%.1f", event.signal_tenths / 10.0);
  if (event.snr_tenths != INT16_MAX)
    snprintf(snr, sizeof(snr), "%.1f", event.snr_tenths / 10.0);
  snprintf(output, output_size, "TO %s  ID %08lX  PORT %u  RSSI %s  SNR %s",
           destination, static_cast<unsigned long>(event.packet_id), event.port,
           signal, snr);
}

void draw_event_row(const Event& event, int x, int y, int w) {
  M5.Display.fillRoundRect(x, y, w, 66, 8, kPanel);
  M5.Display.drawRoundRect(x, y, w, 66, 8, event.verified ? kGrid : kYellow);
  char sender[48];
  event_sender(event, sender, w >= 500 ? sizeof(sender) : 18, w >= 500);
  text(sender, x + 16, y + 19, event.verified ? kGreen : kYellow, 2, middle_left);
  if (g_traffic_details) {
    char details[96]{};
    format_event_details(details, sizeof(details), event);
    text(details, x + 16, y + 46, TFT_WHITE, 2, middle_left);
  } else {
    const char* message = event.text[0] ? event.text
                                        : (event.encrypted ? "ENCRYPTED FRAME" : "WAITING");
    const size_t line_chars = std::min<size_t>(62, static_cast<size_t>((w - 32) / 18));
    char first[66]{}, second[66]{};
    split_message(message, line_chars, first, second);
    if (second[0] && strlen(first) <= sizeof(first) - 4) strcat(first, "...");
    text(first, x + 16, y + 46, TFT_WHITE, 3, middle_left);
  }
  char received[32];
  format_event_time(received, sizeof(received), event, millis(),
                    g_view != View::traffic || g_traffic_details);
  text(received, x + w - 16, y + 19,
       event.received_utc ? kMuted : kYellow, 2, middle_right);
}

void draw_large_event_row(const Event& event, int x, int y, int w) {
  M5.Display.fillRoundRect(x, y, w, 94, 8, kPanel);
  M5.Display.drawRoundRect(x, y, w, 94, 8, event.verified ? kGrid : kYellow);
  char sender[18];
  event_sender(event, sender, sizeof(sender), false);
  text(sender, x + 16, y + 18, event.verified ? kGreen : kYellow, 2, middle_left);
  char received[32];
  format_event_time(received, sizeof(received), event, millis());
  text(received, x + w - 16, y + 18,
       event.received_utc ? kMuted : kYellow, 1, middle_right);
  const char* message = event.text[0] ? event.text
                                      : (event.encrypted ? "ENCRYPTED FRAME" : "WAITING");
  char first[18]{}, second[18]{};
  split_message(message, 17, first, second);
  text(first, x + 16, y + 48, TFT_WHITE, 3, middle_left);
  if (second[0]) text(second, x + 16, y + 76, TFT_WHITE, 3, middle_left);
}

void draw_overview_dynamic(){draw_rx_metrics();draw_rx_preview();}

void draw_nodes_dynamic(){rx_data_views::draw_nodes_values(M5.Display,g_rx_layout,g_snapshot,node_state(),g_node_cache,millis());}

void draw_traffic_dynamic(){rx_data_views::draw_traffic_values(M5.Display,g_rx_layout,g_snapshot,traffic_state(),g_traffic_cache,millis());}

void draw_map_dynamic(bool details=true) {
  static EXT_RAM_BSS_ATTR maps::FrameInfo map_info;
  g_map_revision_seen=maps::revision();
  const Node* center = g_follow_node ? followed_node() : g_snapshot.node_count ? &g_snapshot.nodes[
      std::min<size_t>(g_snapshot.selected_node, g_snapshot.node_count - 1)] : nullptr;
  if(!g_map_center_set) {
    const auto location=receiver_location::snapshot();
    if(location.configured)center_map(location.latitude_e7,location.longitude_e7);
    if(!g_map_center_set)follow_selected_position();
  }
  if (g_follow_node) follow_selected_position();
  if (!g_map_center_set) {
    M5.Display.fillRect(g_rx_layout.map.x,g_rx_layout.map.y,g_rx_layout.map.w,g_rx_layout.map.h,kBg);
    rx_text("NO MAPPABLE POSITION",rx_layout::inset(g_rx_layout.map),kMuted);
  } else {
    const auto location=receiver_location::snapshot();
    (void)orcmap::SetViewportSize(&g_map_camera,g_rx_layout.map.w,g_rx_layout.map.h);
    const auto map=maps::draw_camera(M5.Display,g_map_camera,g_rx_layout.map.x,g_rx_layout.map.y,
        kBg,kGrid,kMuted,location.map_pack,maps::preferences::labels(maps::preferences::Consumer::lora),2);
    map_info=map.frame;
    const char* caption=map_info.credit[0]?map_info.credit:map_info.status;
    const int caption_columns=std::max(1,g_rx_layout.map.w/12);
    const int caption_height=std::min(g_rx_layout.map.h,int((std::strlen(caption)+caption_columns-1)/caption_columns)*20+4);
    M5.Display.setClipRect(g_rx_layout.map.x,g_rx_layout.map.y,g_rx_layout.map.w,g_rx_layout.map.h-caption_height);
    int receiver_x=0,receiver_y=0;
    if(location.configured&&maps::project(map,location.latitude_e7,location.longitude_e7,&receiver_x,&receiver_y)) {
      M5.Display.drawCircle(receiver_x,receiver_y,8,kYellow);
      text("RX",receiver_x+10,receiver_y+12,kYellow,2,middle_left);
    }
    struct MapLabel {int x=0,y=0,width=0,left=0,top=0;bool visible=false,selected=false,placed=false;};
    static EXT_RAM_BSS_ATTR MapLabel labels[kNodeCapacity];
    const size_t count=std::min<size_t>(g_snapshot.node_count,kNodeCapacity);
    for (size_t i = 0; i < count; ++i) {
      labels[i]=MapLabel{};
      const Node& node = g_snapshot.nodes[i];
      if (!has_map_position(node)) continue;
      int x = 0, y = 0;
      if (!maps::project(map,node.latitude_e7,node.longitude_e7,&x,&y))continue;
      const bool selected = g_follow_node ? node.id==g_follow_node_id : i==g_snapshot.selected_node;
      const uint16_t color = selected ? kGreen : kCyan;
      M5.Display.drawCircle(x, y, selected ? 13 : 9, color);
      M5.Display.fillCircle(x, y, 3, color);
      char value[32]; node_name(node, value, sizeof(value));
      M5.Display.setTextSize(2);
      labels[i].x=x;labels[i].y=y;labels[i].width=static_cast<int>(M5.Display.textWidth(value));
      labels[i].visible=true;labels[i].selected=selected;
    }
    const auto intersects=[](int l,int t,int w,int h,int other_l,int other_t,int other_w,int other_h) {
      return l<other_l+other_w&&l+w>other_l&&t<other_t+other_h&&t+h>other_t;
    };
    // Selected/followed node gets first choice. Nearby alternatives keep labels
    // readable; crowded names remain in Nodes instead of overwriting each other.
    for(int pass=0;pass<2;++pass)for(size_t i=0;i<count;++i) {
      auto& label=labels[i];
      if(!label.visible||label.selected!=(pass==0))continue;
      for(int row=0;row<5&&!label.placed;++row)for(int side=0;side<2&&!label.placed;++side) {
        constexpr int offsets[]={-6,-32,20,-58,46};
        const int left=std::clamp(side?label.x-14-label.width:label.x+14,g_rx_layout.map.x+4,
            std::max(g_rx_layout.map.x+4,g_rx_layout.map.x+g_rx_layout.map.w-4-label.width));
        const int top=label.y+offsets[row]-12;
        if(label.width<=0||left+label.width>g_rx_layout.map.x+g_rx_layout.map.w-4||top<g_rx_layout.map.y+4||top+24>g_rx_layout.map.y+g_rx_layout.map.h-caption_height)continue;
        bool blocked=intersects(left,top,label.width,24,g_rx_layout.map_zoom_in.x-2,g_rx_layout.map_zoom_in.y-2,
            kMapZoomSize+4,kMapZoomStep+kMapZoomSize+4);
        for(size_t j=0;!blocked&&j<count;++j) {
          const auto& other=labels[j];
          if(other.placed)blocked=intersects(left-2,top-2,label.width+4,28,
              other.left,other.top,other.width,24);
          if(!blocked&&j!=i&&other.visible)blocked=intersects(left,top,label.width,24,
              other.x-11,other.y-11,22,22);
        }
        if(blocked)continue;
        label.left=left;label.top=top;label.placed=true;
        char value[32];node_name(g_snapshot.nodes[i],value,sizeof(value));
        text(value,left,top+12,label.selected?kGreen:kCyan,2,middle_left);
      }
    }
    M5.Display.clearClipRect();
    button(g_rx_layout.map_zoom_in.x,g_rx_layout.map_zoom_in.y,kMapZoomSize,kMapZoomSize,"+",kCyan);
    button(g_rx_layout.map_zoom_in.x,g_rx_layout.map_zoom_in.y+kMapZoomStep,kMapZoomSize,kMapZoomSize,"-",kCyan);
  }
  if(!details)return;
  const auto info=rx_layout::inset(g_rx_layout.map_info);
  char value[128]{};
  if(center)std::snprintf(value,sizeof(value),"%.31s %ld %ld %u %s",center->name,(long)center->latitude_e7,(long)center->longitude_e7,g_map_camera.zoom,g_follow_node?"FOLLOWING":"FREE MAP");
  else std::snprintf(value,sizeof(value),"NO NODE %u %s",g_map_camera.zoom,g_follow_node?"FOLLOWING":"FREE MAP");
  const auto key=rx_key(value,g_map_revision_seen+1);if(g_map_detail_key==key)return;g_map_detail_key=key;
  rx_card(g_rx_layout.map_info);
  int y=info.y;
  for(const char* line:{"SELECTED NODE",center?center->name:"NO NODE SELECTED",g_follow_node?"FOLLOWING":"FREE MAP"}){
    if(y+24>info.y+info.h)break;
    rx_text(line,{info.x,y,info.w,24},kCyan);y+=28;
  }
  if(center&&y+24<=info.y+info.h){std::snprintf(value,sizeof(value),"%.5f / %.5f",center->latitude_e7/1e7,center->longitude_e7/1e7);rx_text(value,{info.x,y,info.w,24});y+=28;}
  if(y+24<=info.y+info.h)rx_text(g_map_center_set?map_info.status:"NO VERIFIED CENTER",{info.x,y,info.w,24},kMuted);

}

void draw_tx_dynamic() {
  tx_views::draw_values(M5.Display, g_snapshot, g_tx_page, portrait_tx_screen());
  if (g_tx_page == tx_views::Page::messenger) {
    const auto r = tx_views::packet_rx_control_rect(portrait_tx_screen());
    focus_nav::note(r.x, r.y, r.w, r.h);
  }
}

void draw_tx_message_value() {
  const bool vertical = portrait_tx_screen() ||
      (native_tx_screen() && g_tx_layout.vertical);
  if (native_tx_screen()) {
    tx_native::Canvas body(M5.Display, g_tx_layout);
    tx_views::draw_rx_message(body, g_snapshot, vertical);
  } else {
    tx_views::draw_rx_message(M5.Display, g_snapshot, vertical);
  }
}

void draw_tx_packet_rx_control() {
  const bool vertical = portrait_tx_screen() ||
      (native_tx_screen() && g_tx_layout.vertical);
  const auto source = tx_views::packet_rx_control_rect(vertical);
  if (native_tx_screen()) {
    tx_native::Canvas body(M5.Display, g_tx_layout);
    tx_views::draw_packet_rx_control(body, g_snapshot, vertical);
    const auto mapped = g_tx_layout.bounds(source.x, source.y, source.w, source.h);
    focus_nav::note(mapped.x, mapped.y, mapped.w, mapped.h);
  } else {
    tx_views::draw_packet_rx_control(M5.Display, g_snapshot, vertical);
    focus_nav::note(source.x, source.y, source.w, source.h);
  }
}

void draw_tx_packet_rx_frequency() {
  const bool vertical = portrait_tx_screen() ||
      (native_tx_screen() && g_tx_layout.vertical);
  if (native_tx_screen()) {
    tx_native::Canvas body(M5.Display, g_tx_layout);
    tx_views::draw_packet_rx_frequency(body, g_snapshot, vertical);
  } else {
    tx_views::draw_packet_rx_frequency(M5.Display, g_snapshot, vertical);
  }
}

void draw_health_dynamic(){draw_rx_metrics();draw_rx_preview();}

void draw_dynamic() {
  if(g_view==View::overview)draw_overview_dynamic();
  else if(g_view==View::nodes)draw_nodes_dynamic();
  else if(g_view==View::traffic)draw_traffic_dynamic();
  else if(g_view==View::map)draw_map_dynamic();
  else if(g_view==View::rf_health)draw_health_dynamic();
  else draw_tx_dynamic();
  if(g_view!=View::tx_lab)draw_rx_actions();
}

void draw_channels_overlay() {
  const auto d=g_rx_layout.channel_dialog;
  rx_card(d);rx_text("MESHTASTIC LONGFAST",{d.x+12,d.y+8,d.w-76,44},kCyan);
  rx_button(g_rx_layout.channel_close,"X");
  rx_button(g_rx_layout.region_previous,"<");rx_button(g_rx_layout.region_next,">");
  rx_button(g_rx_layout.slot_previous,"<");rx_button(g_rx_layout.slot_next,">");
  char value[80];
  const auto r=g_rx_layout.region_previous,c=g_rx_layout.slot_previous;
  rx_text(g_snapshot.region[0]?g_snapshot.region:"US",{r.x+52,r.y,d.w-136,44},kGreen);
  std::snprintf(value,sizeof(value),"CHANNEL %u / %u",g_snapshot.channel_slot,g_snapshot.channel_count);
  rx_text(value,{c.x+52,c.y,d.w-136,44});
  std::snprintf(value,sizeof(value),"%.3f MHz / DEFAULT %u",g_snapshot.frequency_hz/1e6,g_snapshot.default_slot);
  rx_text(value,{d.x+12,c.y+60,d.w-24,32},kCyan);
}

void draw_native_tx(bool structure) {
  const auto& l=g_tx_layout;
  if(structure) {
    M5.Display.fillRect(l.viewport.x,l.viewport.y,l.viewport.w,l.viewport.h,kBg);
    audio_header::draw_brand("LORA TX LAB");
    audio_header::draw_home_button();audio_header::draw_settings_button();
    audio_header::draw_mute_button(false);audio_header::draw_battery(M5.Power.getBatteryLevel());
    tx_native::Canvas body(M5.Display,l);
    tx_views::draw_static(body,g_snapshot,g_tx_page,l.vertical);
    tx_native::Canvas menu(M5.Display,l,true);
    tx_views::menu(menu,g_tx_page,l.vertical);
    // Native RX tab migration is separate from this TX handoff. Keep the
    // imported legacy 5/6-tab navigation on legacy profiles, rather than
    // advertise native RX hit targets whose views still use legacy geometry.
    draw_tabs();
    for(int i=0;i<3;++i){const auto r=l.mode(i);focus_nav::note(r.x,r.y,r.w,r.h);}
    for(bool send:{false,true}){const auto r=l.action(send);focus_nav::note(r.x,r.y,r.w,r.h);}
  }
  M5.Display.fillRect(l.status.x,l.status.y,l.status.w,l.status.h,kBg);
  text(lora_tx::readiness_name(g_snapshot.tx.readiness),l.status.x+8,l.status.y+14,kYellow,2,middle_left);
  text(lora_tx::can_arm(g_snapshot.tx)==lora_tx::Denial::none?"TX ADMITTED / RX INDEPENDENT":"TX LOCKED / RX INDEPENDENT",l.status.x+8,l.status.y+39,kMuted,2,middle_left);
  tx_native::Canvas body(M5.Display,l);
  tx_views::draw_values(body,g_snapshot,g_tx_page,l.vertical);
  if(g_tx_page==tx_views::Page::messenger) {
    const auto source=tx_views::packet_rx_control_rect(l.vertical);
    const auto r=l.bounds(source.x,source.y,source.w,source.h);
    focus_nav::note(r.x,r.y,r.w,r.h);
  }
  tx_views::action_row(body,g_snapshot,l.vertical,g_tx_page==tx_views::Page::operator_panel);
}

void draw_static() {
  refresh_rx_layout();reset_rx_cache();
  focus_nav::clear();
  if(g_view==View::nodes){
    for(int i=0;i<g_rx_layout.node_row_count;++i){const auto r=g_rx_layout.node_rows[i];focus_nav::note(r.x,r.y,r.w,r.h);}
    for(const auto r:{g_rx_layout.node_previous,g_rx_layout.node_next})focus_nav::note(r.x,r.y,r.w,r.h);
  }
  if(g_view==View::traffic)for(const auto r:{g_rx_layout.traffic_previous,g_rx_layout.traffic_next})focus_nav::note(r.x,r.y,r.w,r.h);
  if(native_tx_screen()){draw_native_tx(true);return;}
  M5.Display.clearScrollRect();
  M5.Display.fillRect(g_rx_viewport.x,g_rx_viewport.y,g_rx_viewport.w,g_rx_viewport.h,kBg);
  draw_header(true);
  if(g_view==View::overview)draw_overview_static();
  else if(g_view==View::nodes)draw_nodes_static();
  else if(g_view==View::traffic)draw_traffic_static();
  else if(g_view==View::map)draw_map_static();
  else if(g_view==View::rf_health)draw_health_static();
  else draw_tx_static();
  draw_tabs();draw_dynamic();
  if(g_channels_open&&g_view==View::overview)draw_channels_overlay();
}

}  // namespace

bool set_viewport(audio_header::HeaderRect viewport) {
  const auto next=rx_layout::make(viewport,g_view,g_snapshot.tx.readiness!=lora_tx::Readiness::absent);
  if(!next.valid)return false;
  tx_native::Layout tx{};
  if(!(viewport.x==0&&viewport.y==0&&viewport.w==1280&&viewport.h==720)){
    tx=tx_native::make(viewport);if(!tx.valid)return false;
  }
  g_rx_viewport=viewport;g_rx_layout=next;g_tx_layout=tx;g_tx_entry_on_enter=false;
  focus_nav::set_viewport(viewport.x+viewport.w,viewport.y+viewport.h);
  if(g_map_center_set)(void)orcmap::SetViewportSize(&g_map_camera,next.map.w,next.map.h);
  reset_rx_cache();return true;
}
bool set_tx_viewport(audio_header::HeaderRect viewport) {
  if(!orcsdr::lora::set_viewport(viewport))return false;
  g_tx_entry_on_enter=true;return true;
}
bool select_view(View next) {
  if(!g_active||next>=View::count||(next==View::tx_lab&&g_snapshot.tx.readiness==lora_tx::Readiness::absent))return false;
  if(next==g_view)return true;
  g_view=next;g_channels_open=false;g_map_drag=false;g_previous_spectrum_bins=0;
  refresh_rx_layout();
  if(g_map_center_set)(void)orcmap::SetViewportSize(&g_map_camera,g_rx_layout.map.w,g_rx_layout.map.h);
  focus_nav::clear();draw_static();return true;
}
bool page_items(int direction) {
  if(!g_active||!direction)return false;
  if(g_view==View::traffic){
    const auto last=traffic_max_offset();const auto old=g_traffic_offset;
    const auto step=std::max(1,g_rx_layout.traffic_row_count);
    g_traffic_offset=direction<0?(old>unsigned(step)?old-step:0):std::min(last,old+step);
    if(old==g_traffic_offset)return false;
    draw_traffic_dynamic();return true;
  }
  if(g_view==View::nodes){
    const auto state=node_state();const unsigned rows=std::max(1,g_rx_layout.node_row_count);
    const size_t last=state.count>rows?state.count-rows:0,old=g_node_offset;
    g_node_offset=direction<0?(old>rows?old-rows:0):std::min(last,old+rows);
    if(old==g_node_offset)return false;
    draw_nodes_dynamic();return true;
  }
  return false;
}

void enter(const Snapshot& snapshot) {
  g_snapshot = snapshot;
  g_snapshot.node_count=std::min<uint8_t>(g_snapshot.node_count,kNodeCapacity);
  g_snapshot.event_count=std::min<uint8_t>(g_snapshot.event_count,kEventCapacity);
  g_active = true;
  g_channels_open = false;
  g_map_center_set = false;
  g_map_drag=false;g_follow_node=false;g_follow_node_id=0;
  const auto location=receiver_location::snapshot();
  if(location.configured)center_map(location.latitude_e7,location.longitude_e7);
  if(!g_map_center_set)follow_selected_position();
  g_last_dynamic_ms = 0;
  g_last_spectrum_ms = 0;
  g_previous_spectrum_bins = 0;
  g_previous_spectrum_view = View::count;
  g_waterfall_floor_valid = false;
  g_tx_page = tx_views::Page::operator_panel;
  if(g_tx_entry_on_enter&&snapshot.tx.readiness!=lora_tx::Readiness::absent)g_view=View::tx_lab;
  if(g_view==View::tx_lab&&snapshot.tx.readiness==lora_tx::Readiness::absent)g_view=View::overview;
  refresh_rx_layout();draw_static();
}

void leave() {
  g_map_drag=false;
  g_channels_open = false;
  if (!g_active) return;
  M5.Display.clearScrollRect();
  g_active = false;
}

void draw() {
  if (!g_active) return;
  draw_static();
}

bool select_tx_page(unsigned page) {
  if(!g_active||g_view!=View::tx_lab||page>2||g_snapshot.tx.readiness==lora_tx::Readiness::absent)return false;
  if(unsigned(g_tx_page)!=page){g_tx_page=static_cast<tx_views::Page>(page);draw_static();}
  return true;
}

void update(const Snapshot& snapshot) {
  if (!g_active) return;
  bool map_content_changed=snapshot.node_count!=g_snapshot.node_count||
      snapshot.selected_node!=g_snapshot.selected_node;
  for(size_t i=0;!map_content_changed&&i<std::min<size_t>(snapshot.node_count,kNodeCapacity);++i) {
    const auto& before=g_snapshot.nodes[i];const auto& after=snapshot.nodes[i];
    map_content_changed=before.id!=after.id||before.latitude_e7!=after.latitude_e7||
        before.longitude_e7!=after.longitude_e7||std::strcmp(before.name,after.name)!=0;
  }
  const bool tx_changed =
      snapshot.tx.readiness != g_snapshot.tx.readiness ||
      snapshot.tx.operator_armed != g_snapshot.tx.operator_armed ||
      snapshot.tx.antenna_path_verified != g_snapshot.tx.antenna_path_verified ||
      snapshot.tx.regional_tx_profile_approved != g_snapshot.tx.regional_tx_profile_approved ||
      snapshot.tx.frequency_approved != g_snapshot.tx.frequency_approved ||
      snapshot.tx.frequency_hz != g_snapshot.tx.frequency_hz ||
      snapshot.tx.busy != g_snapshot.tx.busy ||
      snapshot.tx.max_payload_bytes != g_snapshot.tx.max_payload_bytes;
  const bool capability_presence_changed =
      (snapshot.tx.readiness == lora_tx::Readiness::absent) !=
      (g_snapshot.tx.readiness == lora_tx::Readiness::absent);
  const bool packet_rx_control_changed =
      snapshot.packet_rx_supported != g_snapshot.packet_rx_supported ||
      snapshot.packet_rx_available != g_snapshot.packet_rx_available ||
      snapshot.packet_rx_requested != g_snapshot.packet_rx_requested ||
      snapshot.packet_rx_receiving != g_snapshot.packet_rx_receiving;
  const bool packet_rx_frequency_changed =
      snapshot.packet_rx_frequency_hz != g_snapshot.packet_rx_frequency_hz;
  const bool tx_rx_changed=!tx_views::same_latest_verified_rx_event(snapshot,g_snapshot);
  const bool content_changed = tx_changed || tx_rx_changed || packet_rx_control_changed ||
      packet_rx_frequency_changed ||
      snapshot.revision != g_snapshot.revision;
  const bool channel_changed=snapshot.frequency_hz!=g_snapshot.frequency_hz||snapshot.region_index!=g_snapshot.region_index||snapshot.channel_slot!=g_snapshot.channel_slot;
  g_snapshot = snapshot;
  g_snapshot.node_count=std::min<uint8_t>(g_snapshot.node_count,kNodeCapacity);
  g_snapshot.event_count=std::min<uint8_t>(g_snapshot.event_count,kEventCapacity);
  if(g_channels_open&&g_view==View::overview){if(channel_changed)draw_channels_overlay();return;}
  if(native_tx_screen()&&capability_presence_changed&&g_snapshot.tx.readiness==lora_tx::Readiness::absent){
    g_view=View::overview;draw_static();return;
  }
  if(native_tx_screen()) {
    if(capability_presence_changed)g_tx_page=tx_views::Page::operator_panel;
    // Ordinary RX revision/age ticks must not repaint static TX cards. A new
    // message affects only its value card, including when VERIFIED stays true.
    if(capability_presence_changed) draw_native_tx(true);
    else if(tx_changed) draw_native_tx(false);
    else if(g_tx_page==tx_views::Page::messenger) {
      if(tx_rx_changed) draw_tx_message_value();
      if(packet_rx_control_changed) draw_tx_packet_rx_control();
      if(packet_rx_frequency_changed) draw_tx_packet_rx_frequency();
    }
    return;
  }
  if (g_view==View::tx_lab && portrait_lora_screen() && !g_tx_layout.valid) {
    if (capability_presence_changed) {
      g_tx_page = tx_views::Page::operator_panel; // never resume stale modal on reattach
      M5.Display.startWrite();
      draw_static(); // structural transition: erase stale TX controls
      M5.Display.endWrite();
      return;
    }
    if (!portrait_tx_screen()) return; // no radio; no telemetry widgets to update
    if(g_tx_page==tx_views::Page::messenger && !tx_changed) {
      M5.Display.startWrite();
      if(tx_rx_changed) draw_tx_message_value();
      if(packet_rx_control_changed) draw_tx_packet_rx_control();
      if(packet_rx_frequency_changed) draw_tx_packet_rx_frequency();
      M5.Display.endWrite();
      return; // ignore unrelated revision churn on this message page
    }
    if (!content_changed) return;  // no full-frame repaint on idle
    M5.Display.startWrite();
    if (tx_rx_changed && !tx_changed && g_tx_page==tx_views::Page::messenger)
      draw_tx_message_value();
    else draw_tx_dynamic();
    if (tx_changed) {
      tx_views::action_row(M5.Display, g_snapshot, true,
                           g_tx_page == tx_views::Page::operator_panel);
      draw_portrait_tx_footer();
    }
    M5.Display.endWrite();
    return;
  }
  if (capability_presence_changed) {
    g_tx_page = tx_views::Page::operator_panel;
    if (g_view == View::tx_lab &&
        g_snapshot.tx.readiness == lora_tx::Readiness::absent)
      g_view = View::overview;
    M5.Display.startWrite();
    draw_static(); // rebuild the 5/6-tab navigation in BOTH directions
    M5.Display.endWrite();
    return;
  }
  if (g_header_running != g_snapshot.running || g_header_wifi != g_snapshot.wifi_connected ||
      g_header_decoder != g_snapshot.native_decoder_ready ||
      g_header_key != g_snapshot.key_loaded ||
      (tx_changed && g_view == View::tx_lab)) {
    M5.Display.startWrite();
    draw_header();
    M5.Display.endWrite();
  }
  if(g_view==View::tx_lab && g_tx_page==tx_views::Page::messenger && !tx_changed) {
    M5.Display.startWrite();
    if(tx_rx_changed) draw_tx_message_value();
    if(packet_rx_control_changed) draw_tx_packet_rx_control();
    if(packet_rx_frequency_changed) draw_tx_packet_rx_frequency();
    M5.Display.endWrite();
    return; // message page ignores revisions with no changed visible values
  }
  const uint32_t now = millis();
  const bool needs_age_refresh = g_view != View::tx_lab && (g_view != View::traffic || g_traffic_details);
  const bool map_changed=g_view==View::map&&g_map_revision_seen!=maps::revision();
  // Map contents have no age labels. RF counters and age ticks must not repaint
  // a stationary basemap or clear its selected-node panel repeatedly.
  if(g_view==View::map&&!map_changed&&!map_content_changed)return;
  if (!map_changed&&!content_changed &&
      (!needs_age_refresh || now - g_last_dynamic_ms < kDynamicRefreshIntervalMs)) return;
  g_last_dynamic_ms = now;
  M5.Display.startWrite();
  if (g_view==View::tx_lab && tx_rx_changed && !tx_changed &&
      g_tx_page==tx_views::Page::messenger)
    draw_tx_message_value();
  else draw_dynamic();
  if (tx_changed && g_view == View::tx_lab) {
    tx_views::action_row(M5.Display, g_snapshot, false,
                         g_tx_page == tx_views::Page::operator_panel);
  }
  if (g_channels_open && g_view == View::overview) draw_channels_overlay();
  M5.Display.endWrite();
}

void draw_spectrum(const float* levels, size_t first_bin, size_t visible_bins, float /*floor*/) {
  if (!spectrum_active() || levels == nullptr || visible_bins < 2) return;
  const uint32_t now = millis();
  if (now - g_last_spectrum_ms < kSpectrumRefreshIntervalMs) return;
  g_last_spectrum_ms = now;
  const auto plot=g_rx_layout.plot;
  const int x=plot.x,y=plot.y,w=plot.w,h=plot.h;
  if(w<4||h<4)return;
  const int trace_x = x + 1;
  const int trace_w = w - 2;
  visible_bins = std::min(visible_bins, kSpectrumTraceCapacity);
  const float target_floor = estimate_noise_floor(levels, first_bin, visible_bins);
  g_waterfall_floor = g_waterfall_floor_valid
                          ? 0.97f * g_waterfall_floor + 0.03f * target_floor
                          : target_floor;
  g_waterfall_floor_valid = true;
  const float display_floor = g_waterfall_floor;
  M5.Display.startWrite();
  const bool erase_previous = g_previous_spectrum_bins == visible_bins &&
                              g_previous_spectrum_view == g_view;
  if (erase_previous) {
    int previous_x = trace_x;
    int previous_y = g_previous_spectrum_y[0];
    for (size_t i = 1; i < visible_bins; ++i) {
      const int next_x = trace_x + static_cast<int>(i * static_cast<size_t>(trace_w - 1) /
                                              (visible_bins - 1));
      M5.Display.drawLine(previous_x, previous_y, next_x,
                          g_previous_spectrum_y[i], kBg);
      previous_x = next_x;
      previous_y = g_previous_spectrum_y[i];
    }
  } else {
    M5.Display.fillRect(x + 1, y + 1, w - 2, h - 2, kBg);
  }
  for (int i = 1; i < 5; ++i) {
    M5.Display.drawFastHLine(x, y + i * h / 5, w, kGrid);
    M5.Display.drawFastVLine(x + i * w / 5, y, h, kGrid);
  }
  int px = trace_x;
  int py = y + h - 2 - static_cast<int>(
      std::clamp((levels[first_bin] - display_floor) / kSpectrumRangeDb, 0.0f, 1.0f) *
      (h - 4));
  g_previous_spectrum_y[0] = static_cast<int16_t>(py);
  for (size_t i = 1; i < visible_bins; ++i) {
    const size_t source = first_bin + i;
    const int next_x = trace_x + static_cast<int>(i * static_cast<size_t>(trace_w - 1) /
                                            (visible_bins - 1));
    const float normalized = std::clamp(
        (levels[source] - display_floor) / kSpectrumRangeDb, 0.0f, 1.0f);
    const int next_y = y + h - 2 - static_cast<int>(normalized * (h - 4));
    M5.Display.drawLine(px, py, next_x, next_y, kGreen);
    g_previous_spectrum_y[i] = static_cast<int16_t>(next_y);
    px = next_x;
    py = next_y;
  }
  g_previous_spectrum_bins = visible_bins;
  g_previous_spectrum_view = g_view;
  if(g_view==View::overview||g_view==View::rf_health){
    const auto area=g_rx_layout.waterfall;
    const int rows=std::min(area.h-2,waterfall_style::rows_due(waterfall_style::Screen::lora,now));
    if(rows>0){
      M5.Display.scroll(0,-rows);
      for(int offset=0;offset<area.w-2;offset+=128){
        const int count=std::min(128,area.w-2-offset);
        for(int i=0;i<count;++i){
          const size_t source=first_bin+std::min(visible_bins-1,size_t(offset+i)*visible_bins/size_t(area.w-2));
          g_waterfall_row[i]=waterfall_color((levels[source]-display_floor)/kSpectrumRangeDb);
        }
        for(int row=0;row<rows;++row)M5.Display.pushImage(area.x+1+offset,area.y+area.h-1-rows+row,count,1,reinterpret_cast<const lgfx::rgb565_t*>(g_waterfall_row));
      }
    }
    draw_lora_channel_markers();
  }
  M5.Display.endWrite();
}

Action handle_touch(int32_t x,int32_t y) {
  if(!g_active)return {};
  if(audio_header::home_hit(x,y))return {ActionKind::exit_home};
  if(audio_header::settings_hit(x,y))return {ActionKind::open_settings};
  if(g_channels_open&&g_view==View::overview){
    if(rx_layout::hit(g_rx_layout.channel_close,x,y)){g_channels_open=false;draw_static();return {};}
    if(rx_layout::hit(g_rx_layout.region_previous,x,y))return {ActionKind::region_previous};
    if(rx_layout::hit(g_rx_layout.region_next,x,y))return {ActionKind::region_next};
    if(rx_layout::hit(g_rx_layout.slot_previous,x,y))return {ActionKind::channel_previous};
    if(rx_layout::hit(g_rx_layout.slot_next,x,y))return {ActionKind::channel_next};
    return {};
  }
  for(int i=0;i<g_rx_layout.tab_count;++i)if(rx_layout::hit(g_rx_layout.tab[i],x,y))return {ActionKind::select_view,uint32_t(i)};
  if(g_view==View::tx_lab){
    const bool vertical=native_tx_screen()?g_tx_layout.vertical:portrait_tx_screen();
    if(native_tx_screen()){
      for(int i=0;i<3;++i){const auto r=g_tx_layout.mode(i);if(rx_layout::hit(r,x,y)){select_tx_page(i);return {};}}
      const auto src=tx_views::packet_rx_control_rect(vertical);
      if(g_tx_page==tx_views::Page::messenger&&rx_layout::hit(g_tx_layout.bounds(src.x,src.y,src.w,src.h),x,y)&&tx_views::packet_rx_control_enabled(g_snapshot))return {ActionKind::packet_rx_toggle};
      if(rx_layout::hit(g_tx_layout.action(false),x,y)&&lora_tx::can_arm(g_snapshot.tx)==lora_tx::Denial::none)return {g_snapshot.tx.operator_armed?ActionKind::tx_request_disarm:ActionKind::tx_request_arm};
      if(g_tx_page==tx_views::Page::operator_panel&&rx_layout::hit(g_tx_layout.action(true),x,y)&&lora_tx::can_send(g_snapshot.tx,tx_views::kBenchPacketBytes)==lora_tx::Denial::none)return {ActionKind::tx_send_test_packet};
    }else{
      const int mode=tx_views::page_at(x,y,vertical);if(mode>=0){select_tx_page(mode);return {};}
      if(g_tx_page==tx_views::Page::messenger&&tx_views::packet_rx_control_hit(x,y,vertical)&&tx_views::packet_rx_control_enabled(g_snapshot))return {ActionKind::packet_rx_toggle};
      if(tx_views::arm_rect(x,y,vertical)&&lora_tx::can_arm(g_snapshot.tx)==lora_tx::Denial::none)return {g_snapshot.tx.operator_armed?ActionKind::tx_request_disarm:ActionKind::tx_request_arm};
      if(g_tx_page==tx_views::Page::operator_panel&&tx_views::send_rect(x,y,vertical)&&lora_tx::can_send(g_snapshot.tx,tx_views::kBenchPacketBytes)==lora_tx::Denial::none)return {ActionKind::tx_send_test_packet};
    }
    return {};
  }
  if(g_view==View::map){
    if(rx_layout::hit(g_rx_layout.map_zoom_in,x,y)){map_control(MapControl::zoom_in);return {};}
    if(rx_layout::hit(g_rx_layout.map_zoom_out,x,y)){map_control(MapControl::zoom_out);return {};}
  }
  if(g_view==View::nodes){
    const auto state=node_state();
    if(!(g_rx_layout.single_node_panel&&g_node_details))for(int i=0;i<g_rx_layout.node_row_count;++i)
      if(rx_layout::hit(g_rx_layout.node_rows[i],x,y)&&state.offset+unsigned(i)<state.count)return {ActionKind::select_node,state.indices[state.offset+i]};
    if(rx_layout::hit(g_rx_layout.node_previous,x,y))return {ActionKind::page_previous};
    if(rx_layout::hit(g_rx_layout.node_next,x,y))return {ActionKind::page_next};
  }
  if(g_view==View::traffic){
    if(rx_layout::hit(g_rx_layout.traffic_previous,x,y))return {ActionKind::page_previous};
    if(rx_layout::hit(g_rx_layout.traffic_next,x,y))return {ActionKind::page_next};
  }
  for(int i=0;i<g_rx_layout.action_count;++i){
    if(!rx_layout::hit(g_rx_layout.actions[i],x,y))continue;
    if(g_view==View::overview||g_view==View::rf_health){
      if(i==0&&(g_snapshot.running||g_snapshot.survey_active))return {ActionKind::scan_toggle};
      if(i==1&&g_snapshot.native_decoder_ready&&!g_snapshot.iq_busy&&(!g_snapshot.iq_ready||g_snapshot.iq_save_available))return {ActionKind::record_iq_toggle};
      if(i==2){if(g_view==View::overview)return {ActionKind::open_channels};if(g_snapshot.export_available)return {ActionKind::export_log};}
      if(i==3){if(g_view==View::rf_health)return {ActionKind::clear_events};if(g_snapshot.logging_available)return {ActionKind::logging_toggle};}
      if(i==4){waterfall_style::next_palette(waterfall_style::Screen::lora);return {};}
      if(i==5){waterfall_style::next_speed(waterfall_style::Screen::lora);return {};}
    }else if(g_view==View::nodes){
      if(i==0)return {ActionKind::filter_next};
      if(i==1&&g_snapshot.node_count)return {ActionKind::toggle_favorite};
      if(i==2)return {ActionKind::toggle_packet_details};
      if(i==3&&g_snapshot.export_available)return {ActionKind::export_log};
    }else if(g_view==View::traffic){
      if(i==0)return {ActionKind::toggle_packet_details};
      if(i==1&&g_snapshot.export_available)return {ActionKind::export_log};
      if(i==2)return {ActionKind::filter_next};
      if(i==3)return {ActionKind::clear_events};
    }else if(g_view==View::map){
      if(i==0)return {ActionKind::center_map};
      if(i==1)return {ActionKind::follow_node};
      if(i==2){g_map_label_error=!maps::preferences::set_labels(maps::preferences::Consumer::lora,!maps::preferences::labels(maps::preferences::Consumer::lora));g_action_keys[2]=0;draw_map_controls();draw_map_dynamic();return {};}
      if(i==3){map_control(MapControl::receiver);return {};}
      if(i==4){map_control(MapControl::zoom_in);return {};}
      if(i==5){map_control(MapControl::zoom_out);return {};}
    }
  }
  return {};
}

bool active() { return g_active; }
bool spectrum_active() {
  return !g_channels_open && g_active &&
         (g_view == View::overview || g_view == View::rf_health);
}
View view() { return g_view; }

void open_channel_picker() {
  if (!g_active) return;
  g_view = View::overview;
  g_channels_open = true;
  draw_static();
}

void show_documentation_view(View next,const Snapshot& snapshot) {
  if(g_active){g_snapshot=snapshot;(void)select_view(next);return;}
  g_view=next;enter(snapshot);
}

void toggle_filter() {
  uint8_t& filter = g_view == View::traffic ? g_traffic_filter : g_node_filter;
  filter = (filter + 1) % 2;
  g_traffic_offset = g_node_offset = 0;
  if (g_active) draw_static();
}

void toggle_packet_details() {
  if(g_view==View::nodes)g_node_details=!g_node_details;
  else g_traffic_details = !g_traffic_details;
  if (g_active) draw_static();
}

void center_on_selected() {
  map_control(MapControl::node);
}

void toggle_follow_node() {
  map_control(g_follow_node?MapControl::follow_off:MapControl::follow_on);
}

MapState map_state() {
  MapState state;state.ready=g_map_center_set;state.following=g_follow_node;
  state.following_node_id=g_follow_node?g_follow_node_id:0;
  if(state.ready) {
    state.latitude_e7=static_cast<int32_t>(std::llround(g_map_camera.center_lat_deg*1e7));
    state.longitude_e7=static_cast<int32_t>(std::llround(g_map_camera.center_lon_deg*1e7));
    state.zoom=g_map_camera.zoom;
  }
  return state;
}

void service_map() {
  if(g_active&&g_view==View::map&&!g_channels_open&&screens::owns(screens::Id::lora)&&
     g_map_revision_seen!=maps::revision()) {
    M5.Display.startWrite();draw_map_dynamic();M5.Display.endWrite();
  }
}

bool map_control(MapControl control,int east,int south) {
  if(!g_active||g_view!=View::map||g_channels_open||!screens::owns(screens::Id::lora))return false;
  const bool was_following=g_follow_node;
  bool changed=false;
  switch(control) {
    case MapControl::pan:
      if(!g_map_center_set||east<-g_rx_layout.map.w||east>g_rx_layout.map.w||south<-g_rx_layout.map.h||south>g_rx_layout.map.h||(!east&&!south))return false;
      changed=orcmap::PanByPixels(&g_map_camera,east,south);
      if(changed)g_follow_node=false;
      break;
    case MapControl::zoom_in:
    case MapControl::zoom_out:
      if(!g_map_center_set)return false;
      if(control==MapControl::zoom_in&&g_map_camera.zoom<16)changed=orcmap::ZoomIn(&g_map_camera);
      if(control==MapControl::zoom_out&&g_map_camera.zoom>0)changed=orcmap::ZoomOut(&g_map_camera);
      break;
    case MapControl::receiver: {
      const auto location=receiver_location::snapshot();
      if(location.configured)changed=center_map(location.latitude_e7,location.longitude_e7);
      if(changed)g_follow_node=false;
      break;
    }
    case MapControl::node:
    case MapControl::follow_on: {
      const auto* node=selected_positioned_node();
      if(node)changed=center_map(node->latitude_e7,node->longitude_e7);
      if(changed) {
        g_follow_node=control==MapControl::follow_on;
        g_follow_node_id=g_follow_node?node->id:0;
      }
      break;
    }
    case MapControl::follow_off: g_follow_node=false;changed=true;break;
    default:return false;
  }
  if(!changed)return false;
  if(!g_follow_node)g_follow_node_id=0;
  M5.Display.startWrite();
  draw_map_dynamic(control!=MapControl::pan||was_following!=g_follow_node);
  if(was_following!=g_follow_node)draw_map_controls();
  M5.Display.endWrite();
  return true;
}

bool map_touch(int32_t x,int32_t y,bool pressed,bool new_press) {
  if(!g_active||g_view!=View::map||g_channels_open||!screens::owns(screens::Id::lora)) {
    g_map_drag=false;return false;
  }
  if(new_press) {
    g_map_drag=false;
    if(!hit(x,y,g_rx_layout.map.x,g_rx_layout.map.y,g_rx_layout.map.w,g_rx_layout.map.h)||
       hit(x,y,g_rx_layout.map_zoom_in.x,g_rx_layout.map_zoom_in.y,kMapZoomSize,kMapZoomSize)||
       hit(x,y,g_rx_layout.map_zoom_in.x,g_rx_layout.map_zoom_in.y+kMapZoomStep,kMapZoomSize,kMapZoomSize))return false;
    g_map_drag=true;g_map_moved=false;
    g_map_start_x=g_map_last_x=x;g_map_start_y=g_map_last_y=y;
  }
  if(!g_map_drag)return false;
  if(!pressed){g_map_drag=false;return true;}
  x=std::clamp<int32_t>(x,g_rx_layout.map.x,g_rx_layout.map.x+g_rx_layout.map.w-1);y=std::clamp<int32_t>(y,g_rx_layout.map.y,g_rx_layout.map.y+g_rx_layout.map.h-1);
  if(!g_map_moved&&std::abs(x-g_map_start_x)+std::abs(y-g_map_start_y)<6)return true;
  g_map_moved=true;
  map_control(MapControl::pan,g_map_last_x-x,g_map_last_y-y);
  g_map_last_x=x;g_map_last_y=y;return true;
}

bool handle_map_command(const char* command,bool authenticated,void (*write)(const char*)) {
  if(!command||std::strncmp(command,"RTL_LORA_MAP ",13))return false;
  if(!write)return true;
  if(std::strlen(command)>127){write("RTL_LORA_MAP_ERROR invalid_command\n");return true;}
  char op[16]{},arg[32]{},value[32]{},extra=0;
  const int count=std::sscanf(command+13,"%15s %31s %31s %c",op,arg,value,&extra);
  const auto same=[](const char* a,const char* b){return std::strcmp(a,b)==0;};
  if(count==1&&same(op,"HELP")) {
    write("RTL_LORA_MAP STATUS|ZOOM IN|OUT|PAN <east_px> <south_px>|CENTER RECEIVER|NODE|FOLLOW ON|OFF\n");return true;
  }
  if(count==1&&same(op,"STATUS")) {
    const auto state=map_state();char line[192];
    if(!authenticated) {
      std::snprintf(line,sizeof(line),"RTL_LORA_MAP_STATUS active=%u ready=%u following=%u zoom=%u\n",
          g_active&&g_view==View::map&&screens::owns(screens::Id::lora),state.ready,state.following,state.zoom);
      write(line);return true;
    }
    std::snprintf(line,sizeof(line),"RTL_LORA_MAP_STATUS active=%u ready=%u following=%u latitude_e7=%ld longitude_e7=%ld zoom=%u following_node=%08lx\n",
        g_active&&g_view==View::map&&screens::owns(screens::Id::lora),state.ready,state.following,
        static_cast<long>(state.latitude_e7),static_cast<long>(state.longitude_e7),state.zoom,
        static_cast<unsigned long>(state.following_node_id));
    write(line);return true;
  }
  if(!authenticated){write("RTL_LORA_MAP_ERROR auth_required\n");return true;}
  bool valid=true,ok=false;
  if(count==2&&same(op,"ZOOM")&&(same(arg,"IN")||same(arg,"OUT")))
    ok=map_control(same(arg,"IN")?MapControl::zoom_in:MapControl::zoom_out);
  else if(count==2&&same(op,"CENTER")&&(same(arg,"RECEIVER")||same(arg,"NODE")))
    ok=map_control(same(arg,"RECEIVER")?MapControl::receiver:MapControl::node);
  else if(count==2&&same(op,"FOLLOW")&&(same(arg,"ON")||same(arg,"OFF")))
    ok=map_control(same(arg,"ON")?MapControl::follow_on:MapControl::follow_off);
  else if(count==3&&same(op,"PAN")) {
    int32_t east=0,south=0;
    valid=setup::parse_e7(arg,false,&east)&&setup::parse_e7(value,false,&south);
    if(valid)ok=map_control(MapControl::pan,east,south);
  } else valid=false;
  write(!valid?"RTL_LORA_MAP_ERROR invalid_command\n":ok?"RTL_LORA_MAP_OK\n":"RTL_LORA_MAP_ERROR rejected\n");
  return true;
}

bool self_check() {
  for(const auto viewport:{audio_header::HeaderRect{0,0,1280,720},{72,72,424,1088},{72,72,1088,424},{0,0,800,480},{0,0,480,800}}){
    for(int v=0;v<5;++v){
      const auto l=rx_layout::make(viewport,static_cast<View>(v),true);
      if(!l.valid)return false;
      for(int i=0;i<l.tab_count;++i)if(!rx_layout::hit(l.tab[i],l.tab[i].x+l.tab[i].w/2,l.tab[i].y+l.tab[i].h/2))return false;
    }
  }
  return audio_header::self_check()&&lora_channel::self_check();
}

}  // namespace orcsdr::lora
