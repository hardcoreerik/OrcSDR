#include "focus_nav.hpp"
#include "adsb_dashboard.hpp"

#include "dashboard_audio_control.hpp"
#include "map_view.hpp"
#include "map_preferences.hpp"
#include "receiver_location.hpp"
#include "orc_badge.hpp"

#include <M5Unified.h>
#include <esp_attr.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace orcsdr::adsb {
namespace {

constexpr uint16_t kBg = TFT_BLACK;
constexpr uint16_t kPanel = 0x0861;
constexpr uint16_t kBorder = 0x2945;
constexpr uint16_t kBlue = 0x04ff;
constexpr uint16_t kGreen = 0x6fe8;
constexpr uint16_t kMuted = 0x9cf3;
constexpr int kHeaderH = 100;
constexpr int kTabsY = 646;
constexpr int kTabW = 256;
constexpr int kRadarPanelX = 234;
constexpr int kRadarPanelY = 88;
constexpr int kRadarPanelW = 646;
constexpr int kRadarPanelH = 390;
constexpr int kMapX = 12, kMapY = 76;
constexpr int kMapW = kRadarPanelW - 2 * kMapX;
constexpr int kMapH = kRadarPanelH - kMapY - 12;
constexpr int kZoomX = kRadarPanelX + kMapX + kMapW - 60;
constexpr int kZoomY = kRadarPanelY + kMapY + 12;
constexpr int kZoomSize = 48, kZoomStep = 56;
static_assert(kMapW == 622 && kMapH == 302);
static_assert(kZoomY + kZoomStep + kZoomSize < kRadarPanelY + kMapY + kMapH);
constexpr auto& kRanges = kMapRangesNm;
constexpr uint16_t zoomed_range(uint16_t current, bool zoom_in) {
  size_t index = 0;
  while (index + 1 < std::size(kRanges) && kRanges[index] < current) ++index;
  if (zoom_in) return kRanges[index ? index - 1 : 0];
  return kRanges[index + 1 < std::size(kRanges) ? index + 1 : index];
}
static_assert(zoomed_range(10,true)==10 && zoomed_range(100,false)==100);
static_assert(zoomed_range(25,true)==10 && zoomed_range(25,false)==50);
constexpr size_t kAltitudeBins = 12;
constexpr int kGainMinTenthDb = 0;
constexpr int kGainMaxTenthDb = 496;
constexpr int kGainSliderX = 170;
constexpr int kGainSliderY = 452;
constexpr int kGainSliderW = 280;

enum class View : uint8_t { radar, list, target, stats, settings, count };
enum class EditField : uint8_t { none, latitude, longitude };

struct DisplayAircraft {
  uint32_t icao;
  char callsign[9];
  char registration[9];
  char type[49];
  char op[51];
  int altitude_ft;
  int speed_kts;
  int heading_deg;
  int vertical_rate_fpm;
  float range_nm;
  int bearing_deg;
  float latitude;
  float longitude;
  float signal_dbfs;
  bool has_altitude;
  bool has_speed;
  bool has_heading;
  bool has_vertical_rate;
  bool has_position;
  bool stale;
};

static_assert(static_cast<uint8_t>(View::settings) == kDocumentationViewCount);
static_assert(kRanges[0] == 10 && kRanges[1] == 25 && kRanges[2] == 50 &&
              kRanges[3] == 100);

Settings g_settings;
layout::View g_layout = layout::make_view({0,0,1280,720});
View g_view = View::radar;
EditField g_edit = EditField::none;
size_t g_selected = 0;
bool g_locked = false;
bool g_active = false;
bool g_latitude_set = false;
bool g_longitude_set = false;
char g_entry[20]{};
DisplayAircraft g_aircraft[kVisibleAircraft]{};
size_t g_aircraft_count = 0;
Snapshot g_live_snapshot{};
uint32_t g_drawn_revision = 0;
bool g_live = false;
bool g_atc_listening = false;
constexpr size_t kHistorySamples = 12;
float g_rate_history[kHistorySamples]{};
float g_signal_history[kHistorySamples]{};
size_t g_history_count = 0;
M5Canvas g_radar_primary(&M5.Display), g_radar_candidate(&M5.Display);
M5Canvas* g_radar_base=&g_radar_primary;
M5Canvas* g_radar_next=&g_radar_candidate;
int g_radar_cache_w=0,g_radar_cache_h=0,g_radar_cache_x=0,g_radar_cache_y=0;
int32_t g_radar_cache_latitude_e7 = INT32_MIN;
int32_t g_radar_cache_longitude_e7 = INT32_MIN;
uint16_t g_radar_cache_range_nm = 0;
uint32_t g_radar_map_revision = 0, g_radar_location_revision = 0;
bool g_radar_map_labels = true,g_map_label_error = false;
EXT_RAM_BSS_ATTR maps::RegionalView g_radar_map;

uint8_t displayed_aircraft_count() {
  return g_live_snapshot.aircraft_count;
}

float displayed_message_rate() { return g_live_snapshot.message_rate; }

uint32_t displayed_total_messages() { return g_live_snapshot.total_messages; }

int gain_from_slider_x(int x) {
  const int position = constrain(x - kGainSliderX, 0, kGainSliderW);
  return (position * kGainMaxTenthDb + kGainSliderW / 2) / kGainSliderW;
}

void draw_radar_base() {
  const uint32_t map_revision=maps::revision();
  const auto panel=g_layout.radar_map;
  const auto area=g_layout.radar_map_area;
  const int local_x=area.x-panel.x,local_y=area.y-panel.y;
  const bool stale = g_radar_base->getBuffer() == nullptr ||
                     panel.w!=g_radar_cache_w||panel.h!=g_radar_cache_h||
                     local_x!=g_radar_cache_x||local_y!=g_radar_cache_y||
                     g_radar_cache_latitude_e7 != g_settings.latitude_e7 ||
                     g_radar_cache_longitude_e7 != g_settings.longitude_e7 ||
                     g_radar_cache_range_nm != g_settings.radar_range_nm ||
                     g_radar_map_revision != maps::revision() ||
                     g_radar_location_revision != receiver_location::snapshot().revision ||
                     g_radar_map_labels != maps::preferences::labels(maps::preferences::Consumer::adsb);
  if (!stale) return;
  g_radar_next->deleteSprite();
  g_radar_next->setPsram(true);
  if (g_radar_next->createSprite(panel.w,panel.h)==nullptr)return;
  g_radar_next->fillSprite(kPanel);
  g_radar_next->drawRoundRect(0,0,panel.w,panel.h,12,kBorder);
  maps::RegionalView next_map{};
  if (g_settings.location_configured) {
    const auto location=receiver_location::snapshot();
    next_map=maps::draw_region(*g_radar_next,g_settings.latitude_e7,g_settings.longitude_e7,
        g_settings.radar_range_nm,local_x,local_y,area.w,area.h,kPanel,0x2382,kMuted,
        location.map_pack,maps::preferences::labels(maps::preferences::Consumer::adsb));
    if(next_map.frame.state==maps::Frame::pending&&g_radar_base->getBuffer()) {
      g_radar_next->deleteSprite();
      return;
    }
  }
  g_radar_next->setTextDatum(middle_center);
  g_radar_next->setTextSize(2);
  g_radar_next->setTextColor(TFT_WHITE);
  g_radar_next->drawString("N",local_x+18,local_y+18);
  M5Canvas* old=g_radar_base;g_radar_base=g_radar_next;g_radar_next=old;
  g_radar_next->deleteSprite();
  g_radar_map=next_map;
  g_radar_cache_w=panel.w;g_radar_cache_h=panel.h;
  g_radar_cache_x=local_x;g_radar_cache_y=local_y;
  g_radar_cache_latitude_e7 = g_settings.latitude_e7;
  g_radar_cache_longitude_e7 = g_settings.longitude_e7;
  g_radar_cache_range_nm = g_settings.radar_range_nm;
  g_radar_map_labels=maps::preferences::labels(maps::preferences::Consumer::adsb);
  g_radar_map_revision=map_revision;
  g_radar_location_revision=receiver_location::snapshot().revision;
}

bool radar_position(const DisplayAircraft& aircraft,int* x,int* y) {
  if(!aircraft.has_position||!g_settings.location_configured||
      !std::isfinite(aircraft.latitude)||!std::isfinite(aircraft.longitude)||
      std::abs(aircraft.latitude)>90||std::abs(aircraft.longitude)>180||
      aircraft.range_nm>g_settings.radar_range_nm)return false;
  if(!maps::project(g_radar_map,static_cast<int32_t>(std::llround(aircraft.latitude*1e7)),
      static_cast<int32_t>(std::llround(aircraft.longitude*1e7)),x,y))return false;
  *x+=g_layout.radar_map.x;*y+=g_layout.radar_map.y;return true;
}

bool hit(int32_t x, int32_t y, int bx, int by, int bw, int bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

bool valid_coordinate(EditField field, double value) {
  return field == EditField::latitude ? value >= -90.0 && value <= 90.0
                                      : value >= -180.0 && value <= 180.0;
}

int aircraft_index(uint32_t icao) {
  for (size_t i = 0; i < g_aircraft_count; ++i)
    if (g_aircraft[i].icao == icao) return static_cast<int>(i);
  return -1;
}

bool keep_stale_selection(bool locked, bool stale) { return !stale || locked; }

size_t altitude_bin(int altitude_ft) {
  return static_cast<size_t>(std::clamp(altitude_ft / 5000, 0,
                                        static_cast<int>(kAltitudeBins - 1)));
}

void update_geometry(DisplayAircraft& aircraft) {
  if (!aircraft.has_position || !g_settings.location_configured) return;
  constexpr double kEarthNm = 3440.065;
  const double lat1 = g_settings.latitude_e7 / 10000000.0 * DEG_TO_RAD;
  const double lon1 = g_settings.longitude_e7 / 10000000.0 * DEG_TO_RAD;
  const double lat2 = aircraft.latitude * DEG_TO_RAD;
  const double lon2 = aircraft.longitude * DEG_TO_RAD;
  const double dlat = lat2 - lat1, dlon = lon2 - lon1;
  const double a = sin(dlat / 2) * sin(dlat / 2) +
                   cos(lat1) * cos(lat2) * sin(dlon / 2) * sin(dlon / 2);
  const double bounded_a = std::clamp(a, 0.0, 1.0);
  aircraft.range_nm = static_cast<float>(
      kEarthNm * 2 * atan2(sqrt(bounded_a), sqrt(1 - bounded_a)));
  aircraft.bearing_deg = static_cast<int>(lround(
      fmod(atan2(sin(dlon) * cos(lat2),
                 cos(lat1) * sin(lat2) - sin(lat1) * cos(lat2) * cos(dlon)) /
                   DEG_TO_RAD + 360.0,
           360.0)));
}

void apply_live_snapshot() {
  const uint32_t selected_icao = g_aircraft_count ? g_aircraft[g_selected].icao : 0;
  const DisplayAircraft selected_aircraft = g_aircraft_count ? g_aircraft[g_selected]
                                                              : DisplayAircraft{};
  g_aircraft_count = std::min<size_t>(g_live_snapshot.visible_count, kVisibleAircraft);
  for (size_t i = 0; i < g_aircraft_count; ++i) {
    const auto& source = g_live_snapshot.aircraft[i];
    auto& target = g_aircraft[i];
    target = {};
    target.icao = source.icao;
    strlcpy(target.registration, source.registration, sizeof(target.registration));
    if (source.has_callsign) strlcpy(target.callsign, source.callsign, sizeof(target.callsign));
    else if (source.registration[0])
      strlcpy(target.callsign, source.registration, sizeof(target.callsign));
    else
      snprintf(target.callsign, sizeof(target.callsign), "%06lX",
               static_cast<unsigned long>(source.icao));
    strlcpy(target.type, source.type[0] ? source.type : "--", sizeof(target.type));
    if (source.owner[0]) strlcpy(target.op, source.owner, sizeof(target.op));
    else snprintf(target.op, sizeof(target.op), "ICAO %06lX",
                  static_cast<unsigned long>(source.icao));
    target.altitude_ft = source.altitude_ft;
    target.speed_kts = source.speed_kts;
    target.heading_deg = source.heading_deg;
    target.vertical_rate_fpm = source.vertical_rate_fpm;
    target.latitude = source.latitude;
    target.longitude = source.longitude;
    target.signal_dbfs = source.signal_dbfs;
    target.has_altitude = source.has_altitude;
    target.has_speed = source.has_speed;
    target.has_heading = source.has_heading;
    target.has_vertical_rate = source.has_vertical_rate;
    target.has_position = source.has_position;
    update_geometry(target);
  }
  int selected = aircraft_index(selected_icao);
  if (selected < 0 && g_locked && selected_icao) {
    selected = static_cast<int>(std::min(g_aircraft_count, kVisibleAircraft - 1));
    g_aircraft[selected] = selected_aircraft;
    g_aircraft[selected].stale = true;
    if (g_aircraft_count < kVisibleAircraft) ++g_aircraft_count;
  }
  g_selected = selected >= 0 ? static_cast<size_t>(selected) : 0;
}

void toggle_lock() {
  g_locked = !g_locked;
  if (!g_locked && g_live && g_aircraft_count && g_aircraft[g_selected].stale)
    apply_live_snapshot();
}

void text(const char* value, int x, int y, uint16_t color = TFT_WHITE,
          int size = 2, textdatum_t datum = middle_center) {
  switch (size) {
    case 1: M5.Display.setFont(&fonts::DejaVu18); break;
    case 2: M5.Display.setFont(&fonts::DejaVu24); break;
    case 3: M5.Display.setFont(&fonts::DejaVu24); break;
    default: M5.Display.setFont(&fonts::DejaVu40); break;
  }
  M5.Display.setTextDatum(datum);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(color);
  M5.Display.drawString(value, x, y);
  // Put the built-in font back: the shared header helpers (battery, buttons) draw with it, and a
  // leaked DejaVu font made the battery readout oversized right after navigating here.
  M5.Display.setFont(nullptr);
}

void card(int x, int y, int w, int h) {
  M5.Display.fillRoundRect(x, y, w, h, 12, kPanel);
  M5.Display.drawRoundRect(x, y, w, h, 12, kBorder);
}

void button(const char* label, int x, int y, int w, int h, uint16_t color) {
  orcsdr::focus_nav::note(x, y, w, h);
  M5.Display.fillRoundRect(x, y, w, h, 8, color);
  M5.Display.drawRoundRect(x, y, w, h, 8, TFT_LIGHTGREY);
  text(label, x + w / 2, y + h / 2, TFT_WHITE, 2);
}

void signal_bars(int x, int y, int bars, int count = 5) {
  bars = std::clamp(bars, 0, count);
  for (int i = 0; i < count; ++i) {
    const int h = 6 + i * 5;
    M5.Display.fillRect(x + i * 9, y - h, 6, h,
                        i < bars ? kGreen : TFT_DARKGREY);
  }
}

void metric_card(int x, int y, int w, int h, const char* label,
                 const char* value, const char* secondary = nullptr,
                 uint16_t value_color = TFT_WHITE) {
  card(x, y, w, h);
  text(label, x + 20, y + 27, kBlue, 1, middle_left);
  text(value, x + w / 2, y + h / 2 + 8, value_color, 3);
  if (secondary && secondary[0])
    text(secondary, x + w / 2, y + h - 24, kMuted, 1);
}

void plane(int x, int y, int scale, uint16_t color) {
  M5.Display.fillRect(x - scale / 8, y - scale / 2, scale / 4, scale, color);
  M5.Display.fillTriangle(x, y - scale / 2, x - scale / 5, y - scale / 3,
                          x + scale / 5, y - scale / 3, color);
  M5.Display.fillTriangle(x - scale, y, x + scale, y, x, y + scale / 5, color);
  M5.Display.fillTriangle(x - scale / 2, y + scale / 2,
                          x + scale / 2, y + scale / 2, x, y + scale / 3, color);
}

// Dashboards leave the header's status-bar area (audio_header::kStatusBar*) empty: that space belongs to
// the shared status bar, and the aircraft count and message rate are already in the STATS and radar cards.
void draw_header() {
  M5.Display.setFont(nullptr);   // the dashboards' own text() leaves a DejaVu font selected
  if(!g_layout.legacy) {
    const auto s=g_layout.viewport;
    M5.Display.fillRect(s.x,s.y,s.w,g_layout.content.y-s.y,kBg);
    audio_header::draw_brand("ADS-B 1090");
    audio_header::draw_home_button();
    audio_header::draw_battery(M5.Power.getBatteryLevel());
    audio_header::draw_mute_button(g_live_snapshot.sound_enabled);
    audio_header::draw_visualizer_button(g_live_snapshot.effective_sps!=0);
    audio_header::draw_settings_button();
    return;
  }
  M5.Display.fillRect(0, 0, 1280, kHeaderH, kBg);
  M5.Display.drawFastHLine(20, kHeaderH - 1, 1240, kBorder);
  audio_header::draw_brand("ADS-B 1090");
  audio_header::draw_home_button();
  audio_header::draw_battery(M5.Power.getBatteryLevel());
  audio_header::draw_mute_button(g_live_snapshot.sound_enabled);
  audio_header::draw_visualizer_button(g_live_snapshot.effective_sps != 0);
  audio_header::draw_settings_button();
}

void tab_icon(int index, int x, int y, uint16_t color) {
  if (index == 0) {
    M5.Display.drawCircle(x, y, 13, color);
    M5.Display.drawCircle(x, y, 4, color);
    M5.Display.drawLine(x, y, x + 10, y - 9, color);
  } else if (index == 1) {
    for (int i = -1; i <= 1; ++i) M5.Display.drawFastHLine(x - 12, y + i * 8, 24, color);
  } else if (index == 2) {
    M5.Display.drawCircle(x, y, 12, color);
    M5.Display.drawFastHLine(x - 18, y, 36, color);
    M5.Display.drawFastVLine(x, y - 18, 36, color);
  } else if (index == 3) {
    M5.Display.drawRect(x - 14, y + 2, 6, 13, color);
    M5.Display.drawRect(x - 3, y - 7, 6, 22, color);
    M5.Display.drawRect(x + 8, y - 15, 6, 30, color);
  } else {
    M5.Display.drawCircle(x, y, 13, color);
    M5.Display.drawCircle(x, y, 5, color);
    M5.Display.drawFastHLine(x - 18, y, 36, color);
    M5.Display.drawFastVLine(x, y - 18, 36, color);
  }
}

void draw_tabs() {
  static constexpr const char* labels[] = {"RADAR", "LIST", "TARGET", "STATS", "SETTINGS"};
  if(!g_layout.legacy) {
    for(int i=0;i<5;++i) {
      const auto r=g_layout.tabs[i];const bool selected=static_cast<int>(g_view)==i;
      M5.Display.fillRoundRect(r.x,r.y,r.w,r.h,6,selected?0x08a4:kBg);
      M5.Display.drawRoundRect(r.x,r.y,r.w,r.h,6,selected?kBlue:kBorder);
      text(layout::tab_label(i,r.w),r.x+r.w/2,r.y+r.h/2,selected?kBlue:TFT_LIGHTGREY,
           r.w<80?1:2,middle_center);
    }
    return;
  }
  M5.Display.fillRect(0, kTabsY, 1280, 74, kBg);
  M5.Display.drawFastHLine(0, kTabsY, 1280, kBorder);
  for (int i = 0; i < 5; ++i) {
    const bool selected = static_cast<int>(g_view) == i;
    if (selected) {
      M5.Display.fillRect(i * kTabW, kTabsY + 1, kTabW, 73, 0x08a4);
      M5.Display.fillRect(i * kTabW, kTabsY + 1, kTabW, 3, kBlue);
    }
    if (i) M5.Display.drawFastVLine(i * kTabW, kTabsY, 74, kBorder);
    const uint16_t color = selected ? kBlue : TFT_LIGHTGREY;
    tab_icon(i, i * kTabW + 68, kTabsY + 38, color);
    text(labels[i], i * kTabW + 96, kTabsY + 39, color, 2, middle_left);
  }
}

void draw_selected_summary(int x, int y, int w, int h) {
  if (g_aircraft_count == 0) {
    card(x, y, w, h);
    text("SEARCHING", x + w / 2, y + h / 2 - 20, kBlue, 4);
    text("No aircraft in the last 60 seconds", x + w / 2, y + h / 2 + 35,
         TFT_LIGHTGREY, 2);
    return;
  }
  const DisplayAircraft& a = g_aircraft[g_selected];
  card(x, y, w, h);
  text(a.callsign, x + 28, y + 45, kGreen, 3, middle_left);
  button(a.stale ? "STALE" : (g_locked ? "LOCKED" : "LOCK"),
         x + w - 122, y + 20, 96, 42,
         a.stale ? TFT_MAROON : (g_locked ? TFT_DARKGREEN : TFT_DARKGREY));
  plane(x + 52, y + 116, 22, TFT_WHITE);
  char identity[32];
  snprintf(identity, sizeof(identity), "%s | %06lX",
           a.registration[0] ? a.registration : "--",
           static_cast<unsigned long>(a.icao));
  text(identity, x + 98, y + 91, TFT_WHITE, 1, middle_left);
  text(a.type, x + 98, y + 118, TFT_LIGHTGREY, 1, middle_left);
  text(a.op, x + 98, y + 143, TFT_LIGHTGREY, 1, middle_left);
  M5.Display.drawFastHLine(x + 24, y + 168, w - 48, kBorder);
  char value[32];
  text("ALTITUDE", x + 28, y + 194, kBlue, 1, middle_left);
  if (a.has_altitude) snprintf(value, sizeof(value), "%d ft", a.altitude_ft);
  else strlcpy(value, "--", sizeof(value));
  text(value, x + 28, y + 228, TFT_WHITE, 2, middle_left);
  text("SPEED", x + w / 2 + 12, y + 194, kBlue, 1, middle_left);
  if (a.has_speed) snprintf(value, sizeof(value), "%d kts", a.speed_kts);
  else strlcpy(value, "--", sizeof(value));
  text(value, x + w / 2 + 12, y + 228, TFT_WHITE, 2, middle_left);
  text("RANGE / BEARING", x + 28, y + 278, kBlue, 1, middle_left);
  if (a.has_position && g_settings.location_configured) {
    snprintf(value, sizeof(value), "%.0f NM    %03d deg", a.range_nm, a.bearing_deg);
  } else {
    strlcpy(value, "--", sizeof(value));
  }
  text(value, x + 28, y + 312, TFT_WHITE, 2, middle_left);
  M5.Display.drawFastHLine(x + 24, y + 346, w - 48, kBorder);
  text("VERT RATE", x + 28, y + 374, kBlue, 1, middle_left);
  if (a.has_vertical_rate) snprintf(value, sizeof(value), "%+d ft/min", a.vertical_rate_fpm);
  else strlcpy(value, "--", sizeof(value));
  text(value, x + 28, y + 408, TFT_WHITE, 2, middle_left);
  text("LAST SEEN", x + w / 2 + 12, y + 374, kBlue, 1, middle_left);
  text(a.stale ? "STALE" : "LIVE", x + w / 2 + 12, y + 408,
       a.stale ? TFT_ORANGE : kGreen, 2, middle_left);
  M5.Display.drawFastHLine(x + 24, y + 438, w - 48, kBorder);
  text("SOURCE", x + 28, y + 466, kBlue, 1, middle_left);
  signal_bars(x + 28, y + 504, 4);
  text("ADS-B 1090", x + 82, y + 491, TFT_LIGHTGREY, 1, middle_left);
}

void draw_radar_range() {
  // The sidebar range changes even without a live aircraft snapshot.
  char value[28];snprintf(value,sizeof(value),"%u NM",g_settings.radar_range_nm);
  M5.Display.fillRect(122,520,78,22,kPanel);
  text(value,198,533,kBlue,1,middle_right);
}

void draw_radar_map() {
  const auto panel=g_layout.radar_map;
  const auto area=g_layout.radar_map_area;
  const int cx=area.x+area.w/2,cy=area.y+area.h/2;
  draw_radar_base();
  if (g_radar_base->getBuffer()) g_radar_base->pushSprite(panel.x,panel.y);
  else card(panel.x,panel.y,panel.w,panel.h);
  char title[48];
  snprintf(title,sizeof(title),"LIVE TRAFFIC / %u NM",g_settings.radar_range_nm);
  text(g_layout.legacy?"LIVE TRAFFIC":title,panel.x+22,panel.y+26,kGreen,2,middle_left);
  char location[72];
  if (g_settings.location_configured)
    snprintf(location,sizeof(location),g_layout.legacy?"LAT %.4f   LON %.4f   RANGE %u NM":"%.4f, %.4f",
             g_settings.latitude_e7/10000000.0,g_settings.longitude_e7/10000000.0,g_settings.radar_range_nm);
  else strlcpy(location, "RECEIVER LOCATION NOT SET", sizeof(location));
  text(location,panel.x+22,panel.y+54,kBlue,1,middle_left);
  M5.Display.setClipRect(area.x,area.y,area.w,area.h);
  if (g_settings.location_configured) M5.Display.fillCircle(cx, cy, 7, kGreen);
  for (size_t i = 0; i < g_aircraft_count; ++i) {
    if (!g_aircraft[i].has_position || !g_settings.location_configured) continue;
    int px=0,py=0;
    if(!radar_position(g_aircraft[i],&px,&py))continue;
    plane(px, py, i == g_selected ? 16 : 12, i == g_selected ? kBlue : kGreen);
    text(g_aircraft[i].callsign, px + 18, py - 5,
         i == g_selected ? TFT_WHITE : TFT_LIGHTGREY, 1, middle_left);
  }
  M5.Display.clearClipRect();
  button(g_map_label_error?"SAVE FAILED":
      maps::preferences::labels(maps::preferences::Consumer::adsb)?"LABELS ON":"LABELS OFF",
      g_layout.map_labels.x,g_layout.map_labels.y,g_layout.map_labels.w,g_layout.map_labels.h,
      TFT_DARKCYAN);
  const auto zi=g_layout.map_zoom_in,zo=g_layout.map_zoom_out;
  button("+",zi.x,zi.y,zi.w,zi.h,
         g_settings.radar_range_nm==kRanges[0] ? TFT_DARKGREY : TFT_DARKCYAN);
  button("-",zo.x,zo.y,zo.w,zo.h,
         g_settings.radar_range_nm==kRanges[std::size(kRanges)-1] ? TFT_DARKGREY : TFT_DARKCYAN);

}

void draw_radar() {
  card(14, 88, 210, 226);
  text("STATUS", 32, 113, TFT_WHITE, 2, middle_left);
  M5.Display.drawFastHLine(30, 137, 178, kBorder);
  text(g_live ? "RECEIVING" : "WAITING", 54, 166,
       g_live ? kGreen : TFT_ORANGE, 1, middle_left);
  signal_bars(34, 218, g_live ? 4 : 0);
  text("SIGNAL", 88, 204, TFT_LIGHTGREY, 1, middle_left);
  text(g_live ? "GOOD" : "--", 198, 204,
       g_live ? kGreen : kMuted, 1, middle_right);
  button(g_map_label_error?"SAVE FAILED":
      maps::preferences::labels(maps::preferences::Consumer::adsb)?"LABELS ON":"LABELS OFF",26,252,186,48,TFT_DARKCYAN);

  card(14, 324, 210, 300);
  text("STATS", 32, 349, TFT_WHITE, 2, middle_left);
  M5.Display.drawFastHLine(30, 373, 178, kBorder);
  char side[28];
  const char* side_labels[] = {"AIRCRAFT", "MESSAGES", "MSG / SEC", "RANGE", "DROPS"};
  snprintf(side, sizeof(side), "%u", static_cast<unsigned>(displayed_aircraft_count()));
  char side_messages[28], side_rate[28], side_range[28], side_drops[28];
  snprintf(side_messages, sizeof(side_messages), "%lu", static_cast<unsigned long>(displayed_total_messages()));
  snprintf(side_rate, sizeof(side_rate), "%.1f", displayed_message_rate());
  snprintf(side_range, sizeof(side_range), "%u NM", g_settings.radar_range_nm);
  snprintf(side_drops, sizeof(side_drops), "%lu", static_cast<unsigned long>(g_live_snapshot.consumer_drops));
  const char* side_values[] = {side, side_messages, side_rate, side_range, side_drops};
  for (int i = 0; i < 5; ++i) {
    const int yy = 404 + i * 43;
    text(side_labels[i], 42, yy, kMuted, 1, middle_left);
    text(side_values[i], 198, yy, i == 0 ? kGreen : kBlue, 1, middle_right);
    if (i < 4) M5.Display.drawFastHLine(32, yy + 20, 174, kBorder);
  }

  draw_radar_map();

  card(234, 488, 646, 136);
  text("SIGNAL LEVEL", 250, 512, TFT_WHITE, 1, middle_left);
  const float signal = g_live ? g_live_snapshot.strongest_signal_dbfs : -100.0f;
  const int bars = std::clamp(static_cast<int>((signal + 100.0f) / 5.0f), 0, 12);
  for (int i = 0; i < 12; ++i)
    M5.Display.fillRect(250 + i * 18, 582 - i * 3, 12, 18 + i * 3,
                        i < bars ? (i < 7 ? kGreen : kBlue) : TFT_DARKGREY);
  text("POSITIONS / MIN", 487, 512, TFT_WHITE, 1, middle_left);
  char rate[18];
  snprintf(rate, sizeof(rate), "%.0f", displayed_message_rate());
  text(rate, 487, 551, TFT_WHITE, 3, middle_left);
  text("ALTITUDE DISTRIBUTION", 655, 512, TFT_WHITE, 1, middle_left);
  uint8_t altitude_bins[kAltitudeBins]{};
  uint8_t max_altitude_bin = 0;
  for (size_t i = 0; i < g_aircraft_count; ++i) {
    if (!g_live || !g_aircraft[i].has_altitude || g_aircraft[i].stale) continue;
    auto& count = altitude_bins[altitude_bin(g_aircraft[i].altitude_ft)];
    max_altitude_bin = std::max(max_altitude_bin, ++count);
  }
  if (!max_altitude_bin) {
    text("--", 755, 561, kMuted, 2);
  } else {
    for (size_t i = 0; i < kAltitudeBins; ++i) {
      const int h = altitude_bins[i]
                        ? 8 + altitude_bins[i] * 38 / max_altitude_bin
                        : 0;
      if (h)
        M5.Display.fillRect(660 + static_cast<int>(i) * 16, 594 - h, 11, h,
                            kBlue);
    }
  }

  card(890, 88, 376, 536);
  text("AIRCRAFT LIST", 910, 116, TFT_WHITE, 2, middle_left);
  char received[24];
  snprintf(received, sizeof(received), "%u RECEIVED", static_cast<unsigned>(displayed_aircraft_count()));
  text(received, 1246, 116, kBlue, 1, middle_right);
  M5.Display.drawFastHLine(906, 143, 344, kBorder);
  text("CALLSIGN", 930, 166, kBlue, 1, middle_left);
  text("ALT", 1070, 166, kBlue, 1, middle_left);
  text("SPD", 1148, 166, kBlue, 1, middle_left);
  text("DIST", 1238, 166, kBlue, 1, middle_right);
  for (size_t i = 0; i < g_aircraft_count; ++i) {
    const int yy = 202 + static_cast<int>(i) * 57;
    plane(916, yy, 9, i == g_selected ? kGreen : kBlue);
    text(g_aircraft[i].callsign, 936, yy, i == g_selected ? kGreen : TFT_WHITE, 1, middle_left);
    char value[20];
    if (g_aircraft[i].has_altitude) snprintf(value, sizeof(value), "%dk", g_aircraft[i].altitude_ft / 1000);
    else strlcpy(value, "--", sizeof(value));
    text(value, 1070, yy, TFT_WHITE, 1, middle_left);
    if (g_aircraft[i].has_speed) snprintf(value, sizeof(value), "%d", g_aircraft[i].speed_kts);
    else strlcpy(value, "--", sizeof(value));
    text(value, 1148, yy, TFT_WHITE, 1, middle_left);
    if (g_aircraft[i].has_position && g_settings.location_configured)
      snprintf(value, sizeof(value), "%.0f NM", g_aircraft[i].range_nm);
    else strlcpy(value, "--", sizeof(value));
    text(value, 1238, yy, TFT_WHITE, 1, middle_right);
    M5.Display.drawFastHLine(906, yy + 27, 344, kBorder);
  }
  if (!g_settings.location_configured) {
    button("SET RECEIVER LOCATION", 365, 425, 360, 40, TFT_MAROON);
  }
}

void draw_list() {
  card(14, 88, 842, 536);
  text("CALLSIGN", 84, 120, TFT_WHITE, 1, middle_left);
  text("TAIL / REG", 250, 120, TFT_WHITE, 1, middle_left);
  text("TYPE", 400, 120, TFT_WHITE, 1, middle_left);
  text("ALTITUDE", 520, 120, TFT_WHITE, 1, middle_left);
  text("SPEED", 650, 120, TFT_WHITE, 1, middle_left);
  text("RANGE", 770, 120, TFT_WHITE, 1, middle_left);
  for (size_t i = 0; i < g_aircraft_count; ++i) {
    const int y = 143 + static_cast<int>(i) * 76;
    if (i == g_selected) M5.Display.fillRoundRect(26, y, 818, 68, 8, 0x1362);
    else M5.Display.drawFastHLine(28, y + 68, 812, kBorder);
    plane(52, y + 34, 10, i == g_selected ? kGreen : kBlue);
    M5.Display.setClipRect(78, y, 162, 68);
    text(g_aircraft[i].callsign, 82, y + 25, i == g_selected ? kGreen : TFT_WHITE, 2, middle_left);
    text(g_aircraft[i].op, 82, y + 49, TFT_LIGHTGREY, 1, middle_left);
    M5.Display.clearClipRect();
    M5.Display.setClipRect(246, y, 144, 68);
    text(g_aircraft[i].registration[0] ? g_aircraft[i].registration : "--",
         250, y + 25, TFT_WHITE, 1, middle_left);
    char identity[16];
    snprintf(identity, sizeof(identity), "%06lX", static_cast<unsigned long>(g_aircraft[i].icao));
    text(identity, 250, y + 49, TFT_LIGHTGREY, 1, middle_left);
    M5.Display.clearClipRect();
    M5.Display.setClipRect(396, y, 114, 68);
    text(g_aircraft[i].type, 400, y + 34, TFT_WHITE, 1, middle_left);
    M5.Display.clearClipRect();
    char value[24];
    if (g_aircraft[i].has_altitude) snprintf(value, sizeof(value), "%d ft", g_aircraft[i].altitude_ft);
    else strlcpy(value, "--", sizeof(value));
    text(value, 520, y + 34, TFT_WHITE, 1, middle_left);
    if (g_aircraft[i].has_speed) snprintf(value, sizeof(value), "%d kts", g_aircraft[i].speed_kts);
    else strlcpy(value, "--", sizeof(value));
    text(value, 650, y + 34, TFT_WHITE, 1, middle_left);
    if (g_aircraft[i].has_position && g_settings.location_configured)
      snprintf(value, sizeof(value), "%.0f NM", g_aircraft[i].range_nm);
    else
      strlcpy(value, "--", sizeof(value));
    text(value, 770, y + 34, TFT_WHITE, 1, middle_left);
  }
  draw_selected_summary(874, 88, 392, 536);
}

void draw_target() {
  if (g_aircraft_count == 0) {
    card(18, 92, 1244, 532);
    text("SEARCHING FOR AIRCRAFT", 640, 340, kBlue, 4);
    return;
  }
  const DisplayAircraft& a = g_aircraft[g_selected];
  card(14, 88, 500, 536);
  text(a.callsign, 42, 132, kGreen, 4, middle_left);
  button(a.stale ? "STALE" : (g_locked ? "LOCKED" : "LOCK"), 300, 104, 110, 44,
         a.stale ? TFT_MAROON : (g_locked ? TFT_DARKGREEN : TFT_DARKGREY));
  text(a.op, 42, 177, TFT_WHITE, 2, middle_left);
  M5.Display.drawFastHLine(36, 202, 460, kBorder);
  char value[40];
  const char* identity_labels[] = {"REGISTRATION", "ICAO (HEX)", "MODE-S", "AIRCRAFT", "OWNER / OPERATOR"};
  char identity_values[5][56];
  strlcpy(identity_values[0], a.registration[0] ? a.registration : "--", sizeof(identity_values[0]));
  snprintf(identity_values[1], sizeof(identity_values[1]), "%06lX", static_cast<unsigned long>(a.icao));
  snprintf(identity_values[2], sizeof(identity_values[2]), "%06lX", static_cast<unsigned long>(a.icao));
  strlcpy(identity_values[3], a.type, sizeof(identity_values[3]));
  strlcpy(identity_values[4], a.op, sizeof(identity_values[4]));
  for (int i = 0; i < 5; ++i) {
    const int yy = 228 + i * 35;
    text(identity_labels[i], 44, yy, kMuted, 1, middle_left);
    text(identity_values[i], 248, yy, TFT_WHITE, 1, middle_left);
  }
  M5.Display.drawFastHLine(36, 415, 460, kBorder);
  plane(126, 494, 58, TFT_LIGHTGREY);
  char atc[44];
  if (g_atc_listening) strlcpy(atc, "RESUME ADS-B", sizeof(atc));
  else if (g_settings.atc_frequency_hz) {
    const uint32_t mhz = g_settings.atc_frequency_hz / 1000000;
    const uint32_t khz = (g_settings.atc_frequency_hz % 1000000) / 1000;
    snprintf(atc, sizeof(atc), "LISTEN %.22s %lu.%03lu", g_settings.atc_label,
             static_cast<unsigned long>(mhz), static_cast<unsigned long>(khz));
  }
  else strlcpy(atc, "ATC DATA NOT INSTALLED", sizeof(atc));
  button(atc, 230, 462, 250, 54,
         g_atc_listening ? TFT_DARKGREEN : g_settings.atc_frequency_hz ? TFT_NAVY : TFT_DARKGREY);

  char values[9][32], secondary[9][28];
  memset(secondary, 0, sizeof(secondary));
  if (a.has_altitude) {
    snprintf(values[0], sizeof(values[0]), "%d ft", a.altitude_ft);
    snprintf(secondary[0], sizeof(secondary[0]), "FL%d", a.altitude_ft / 100);
  } else strlcpy(values[0], "--", sizeof(values[0]));
  if (a.has_speed) {
    snprintf(values[1], sizeof(values[1]), "%d kts", a.speed_kts);
    snprintf(secondary[1], sizeof(secondary[1]), "%d km/h", static_cast<int>(a.speed_kts * 1.852f));
  } else strlcpy(values[1], "--", sizeof(values[1]));
  if (a.has_heading) snprintf(values[2], sizeof(values[2]), "%03d deg", a.heading_deg);
  else strlcpy(values[2], "--", sizeof(values[2]));
  if (a.has_vertical_rate) {
    snprintf(values[3], sizeof(values[3]), "%+d ft/min", a.vertical_rate_fpm);
    strlcpy(secondary[3], a.vertical_rate_fpm > 0 ? "CLIMBING" : a.vertical_rate_fpm < 0 ? "DESCENDING" : "LEVEL", sizeof(secondary[3]));
  } else strlcpy(values[3], "--", sizeof(values[3]));
  if (a.has_position && g_settings.location_configured) {
    snprintf(values[4], sizeof(values[4]), "%.0f NM", a.range_nm);
    snprintf(secondary[4], sizeof(secondary[4]), "%.1f km", a.range_nm * 1.852f);
    snprintf(values[5], sizeof(values[5]), "%03d deg", a.bearing_deg);
  } else {
    strlcpy(values[4], "--", sizeof(values[4]));
    strlcpy(values[5], "--", sizeof(values[5]));
  }
  if (a.has_position) {
    snprintf(values[6], sizeof(values[6]), "%.4f", a.latitude);
    snprintf(values[7], sizeof(values[7]), "%.4f", a.longitude);
  } else {
    strlcpy(values[6], "--", sizeof(values[6]));
    strlcpy(values[7], "--", sizeof(values[7]));
  }
  strlcpy(values[8], a.stale ? "STALE" : "LIVE", sizeof(values[8]));
  strlcpy(secondary[8], a.stale ? "LOCKED TARGET" : "JUST NOW", sizeof(secondary[8]));
  const char* labels[] = {"ALTITUDE", "SPEED", "HEADING", "VERTICAL RATE", "RANGE",
                          "BEARING", "LATITUDE", "LONGITUDE", "LAST SEEN"};
  for (int i = 0; i < 9; ++i) {
    const int col = i % 3, row = i / 3;
    metric_card(532 + col * 240, 88 + row * 143, 226, 130,
                labels[i], values[i], secondary[i], i == 8 ? kGreen : TFT_WHITE);
  }
  card(532, 532, 706, 92);
  text("SIGNAL", 552, 558, kBlue, 1, middle_left);
  const float signal = g_live ? g_live_snapshot.strongest_signal_dbfs : -100.0f;
  const int bars = std::clamp(static_cast<int>((signal + 100.0f) / 7.0f), 0, 10);
  for (int i = 0; i < 10; ++i)
    M5.Display.fillRect(625 + i * 23, 586 - i * 3, 16, 18 + i * 3,
                        i < bars ? kGreen : TFT_DARKGREY);
  if (g_live) snprintf(value, sizeof(value), "%.0f dBFS", signal);
  else strlcpy(value, "--", sizeof(value));
  text(value, 880, 579, kGreen, 2, middle_left);
  button("SHOW ON MAP", 1005, 550, 210, 54, TFT_NAVY);
}

void draw_stats() {
  char value[32];
  const float signal = g_live ? g_live_snapshot.strongest_signal_dbfs : -100.0f;
  card(14, 88, 400, 226);
  text("SIGNAL STRENGTH", 36, 116, kBlue, 1, middle_left);
  if (g_live) snprintf(value, sizeof(value), "%.1f dBFS", signal);
  else strlcpy(value, "--", sizeof(value));
  text(value, 36, 157, TFT_WHITE, 3, middle_left);
  const int active_bars = std::clamp(static_cast<int>((signal + 100.0f) / 5.0f), 0, 14);
  for (int i = 0; i < 14; ++i)
    M5.Display.fillRect(38 + i * 24, 270 - i * 4, 17, 22 + i * 4,
                        i < active_bars ? kGreen : TFT_DARKGREY);
  text("-100", 36, 292, kMuted, 1, middle_left);
  text("-50", 208, 292, kMuted, 1);
  text("0 dBFS", 390, 292, kMuted, 1, middle_right);

  card(426, 88, 400, 226);
  text("MESSAGE RATE", 448, 116, kBlue, 1, middle_left);
  snprintf(value, sizeof(value), "%.1f msg/sec", displayed_message_rate());
  text(value, 448, 157, TFT_WHITE, 3, middle_left);
  if (g_history_count > 1) {
    const float max_rate = std::max(1.0f, *std::max_element(g_rate_history,
        g_rate_history + g_history_count));
    for (size_t i = 1; i < g_history_count; ++i) {
      const int x1 = 450 + static_cast<int>((i - 1) * 350 / (kHistorySamples - 1));
      const int x2 = 450 + static_cast<int>(i * 350 / (kHistorySamples - 1));
      const int y1 = 285 - static_cast<int>(g_rate_history[i - 1] * 90 / max_rate);
      const int y2 = 285 - static_cast<int>(g_rate_history[i] * 90 / max_rate);
      M5.Display.drawLine(x1, y1, x2, y2, kBlue);
    }
  }
  card(838, 88, 428, 226);
  text("MODE-S ACTIVITY", 860, 116, kBlue, 1, middle_left);
  const size_t activity_count = g_history_count;
  for (size_t i = 0; i < activity_count; ++i) {
    const int height = std::clamp(
        static_cast<int>((g_signal_history[i] + 100.0f) * 1.2f), 10, 100);
    M5.Display.drawRect(866 + static_cast<int>(i) * 35, 282 - height,
                        20, height, kBlue);
  }
  char aircraft[12], messages[20], strongest[20];
  snprintf(aircraft, sizeof(aircraft), "%u", static_cast<unsigned>(displayed_aircraft_count()));
  snprintf(messages, sizeof(messages), "%lu", static_cast<unsigned long>(displayed_total_messages()));
  snprintf(strongest, sizeof(strongest), "%.1f dBFS", signal);
  struct Metric { const char* label; const char* value; uint16_t color; };
  char sample_rate[20], drops[20];
  snprintf(sample_rate, sizeof(sample_rate), "%.3f MS/s",
           g_live_snapshot.effective_sps / 1000000.0);
  snprintf(drops, sizeof(drops), "%lu / %lu", static_cast<unsigned long>(g_live_snapshot.usb_overruns),
           static_cast<unsigned long>(g_live_snapshot.consumer_drops));
  const Metric metrics[] = {{"AIRCRAFT", aircraft, kGreen},
                            {"MESSAGES", messages, TFT_WHITE},
                            {"STRONGEST", strongest, kGreen},
                            {"SAMPLE RATE", sample_rate, TFT_WHITE}, {"USB / DSP DROPS", drops, kGreen}};
  for (int i = 0; i < 5; ++i) {
    const int x = 14 + i * 250;
    metric_card(x, 326, 238, 120, metrics[i].label, metrics[i].value,
                nullptr, metrics[i].color);
  }

  struct DataCard { const char* title; const char* line1; const char* line2; bool ready; };
  char atc_line[36];
  if (g_settings.atc_frequency_hz)
    snprintf(atc_line, sizeof(atc_line), "%.24s %lu.%03lu", g_settings.atc_label,
             static_cast<unsigned long>(g_settings.atc_frequency_hz / 1000000),
             static_cast<unsigned long>((g_settings.atc_frequency_hz % 1000000) / 1000));
  else strlcpy(atc_line, "NO NEARBY PRESET", sizeof(atc_line));
  const DataCard data[] = {{"FAA AIRCRAFT DB", g_live_snapshot.faa_aircraft_installed ? "INSTALLED" : "NOT INSTALLED", "REGISTRATION LOOKUP", g_live_snapshot.faa_aircraft_installed},
                           {"FAA AVIATION DB", g_live_snapshot.faa_aviation_installed ? "INSTALLED" : "NOT INSTALLED", "AIRPORT / ATC DATA", g_live_snapshot.faa_aviation_installed},
                           {"OFFLINE MAP", maps::installed() ? "LOCAL MAPS AVAILABLE" : "NOT INSTALLED", "WORLD / REGIONAL", maps::installed()},
                           {"LISTEN TO ATC", atc_line, g_atc_listening ? "ADS-B PAUSED" : "MANUAL START", g_settings.atc_frequency_hz != 0}};
  for (int i = 0; i < 4; ++i) {
    const int x = 14 + i * 313;
    card(x, 458, 301, 166);
    text(data[i].title, x + 20, 486, kBlue, 1, middle_left);
    text(data[i].line1, x + 20, 535, TFT_WHITE, 1, middle_left);
    text(data[i].line2, x + 20, 566, kMuted, 1, middle_left);
    text(data[i].ready ? "READY" : "UNAVAILABLE", x + 20, 602,
         data[i].ready ? kGreen : TFT_ORANGE, 1, middle_left);
  }
}

void start_edit(EditField field) {
  g_edit = field;
  const int32_t e7 = field == EditField::latitude ? g_settings.latitude_e7
                                                  : g_settings.longitude_e7;
  snprintf(g_entry, sizeof(g_entry), "%.7f", e7 / 10000000.0);
}

void draw_keypad() {
  if(!g_layout.legacy) {
    const auto p=g_layout.keypad;card(p.x,p.y,p.w,p.h);
    text(g_edit==EditField::latitude?"EDIT LATITUDE":"EDIT LONGITUDE",
         p.x+p.w/2,p.y+28,kBlue,2);
    button(g_entry[0]?g_entry:"0",g_layout.keypad_value.x,g_layout.keypad_value.y,
           g_layout.keypad_value.w,g_layout.keypad_value.h,TFT_NAVY);
    static constexpr const char* keys[]={"1","2","3","4","5","6","7","8","9","+/-","0",".","<"};
    for(int i=0;i<13;++i){const auto r=g_layout.keypad_keys[i];
      button(keys[i],r.x,r.y,r.w,r.h,TFT_DARKGREY);}
    button("CANCEL",g_layout.keypad_cancel.x,g_layout.keypad_cancel.y,
           g_layout.keypad_cancel.w,g_layout.keypad_cancel.h,TFT_MAROON);
    button("SAVE",g_layout.keypad_save.x,g_layout.keypad_save.y,
           g_layout.keypad_save.w,g_layout.keypad_save.h,TFT_DARKGREEN);
    return;
  }
  card(690, 102, 570, 510);
  text(g_edit == EditField::latitude ? "EDIT LATITUDE" : "EDIT LONGITUDE",
       975, 138, kBlue, 3);
  button(g_entry[0] ? g_entry : "0", 730, 170, 490, 55, TFT_NAVY);
  static constexpr const char* keys[] = {"1", "2", "3", "4", "5", "6",
                                          "7", "8", "9", "+/-", "0", ".", "<"};
  for (int i = 0; i < 13; ++i) {
    const int col = i % 4, row = i / 4;
    button(keys[i], 730 + col * 122, 245 + row * 64, 112, 54, TFT_DARKGREY);
  }
  button("CANCEL", 730, 508, 230, 58, TFT_MAROON);
  button("SAVE", 990, 508, 230, 58, TFT_DARKGREEN);
}

void draw_settings() {
  card(14, 88, 1252, 536);
  M5.Display.drawFastVLine(478, 108, 494, kBorder);
  text("ADS-B SETTINGS", 38, 124, kBlue, 3, middle_left);
  text("RECEIVER LATITUDE", 38, 184, kMuted, 1, middle_left);
  text("RECEIVER LONGITUDE", 38, 266, kMuted, 1, middle_left);
  char value[40];
  if (g_latitude_set)
    snprintf(value, sizeof(value), "%.7f", g_settings.latitude_e7 / 10000000.0);
  else
    strlcpy(value, "NOT SET", sizeof(value));
  button(value, 225, 158, 225, 52, TFT_NAVY);
  if (g_longitude_set)
    snprintf(value, sizeof(value), "%.7f", g_settings.longitude_e7 / 10000000.0);
  else
    strlcpy(value, "NOT SET", sizeof(value));
  button(value, 225, 240, 225, 52, TFT_NAVY);
  text("RADAR RANGE", 38, 348, kMuted, 1, middle_left);
  snprintf(value, sizeof(value), "%u NM", g_settings.radar_range_nm);
  button(value, 225, 322, 225, 52, TFT_DARKCYAN);
  text("RF GAIN", 38, 406, kMuted, 1, middle_left);
  button(g_settings.gain_auto ? "AUTO" : "USE AUTO", 38, 430, 112, 48,
         g_settings.gain_auto_supported ? TFT_DARKGREEN : TFT_DARKGREY);
  M5.Display.fillRoundRect(kGainSliderX, kGainSliderY - 5, kGainSliderW, 10, 5,
                           g_settings.gain_supported ? TFT_DARKGREY : kBorder);
  if (g_settings.gain_supported) {
    const int gain = constrain(static_cast<int>(g_settings.gain_tenth_db),
                               kGainMinTenthDb, kGainMaxTenthDb);
    const int knob_x = kGainSliderX + gain * kGainSliderW / kGainMaxTenthDb;
    M5.Display.fillCircle(knob_x, kGainSliderY, 12,
                          g_settings.gain_auto ? kMuted : kBlue);
    if (g_settings.gain_auto)
      strlcpy(value, "TUNER AUTO", sizeof(value));
    else
      snprintf(value, sizeof(value), "%.1f dB", gain / 10.0);
  } else {
    strlcpy(value, "UNAVAILABLE", sizeof(value));
  }
  text(value, kGainSliderX, 486,
       g_settings.gain_supported ? TFT_LIGHTGREY : kMuted, 1, middle_left);
  button("EXIT ADS-B", 38, 538, 412, 58, TFT_MAROON);
  if (g_edit != EditField::none) draw_keypad();
  else {
    card(496, 108, 360, 214);
    text("FAA AIRCRAFT DATABASE", 516, 137, kBlue, 1, middle_left);
    text("Registration, type and owner", 516, 176, TFT_LIGHTGREY, 1, middle_left);
    text(g_live_snapshot.faa_aircraft_installed ? "INSTALLED" : "NOT INSTALLED",
         516, 214, g_live_snapshot.faa_aircraft_installed ? kGreen : TFT_ORANGE, 1, middle_left);
    button("MANAGE FAA DATA", 516, 248, 320, 50, TFT_DARKGREEN);

    card(874, 108, 374, 214);
    text("FAA AVIATION DATABASE", 894, 137, kBlue, 1, middle_left);
    text("Airports and verified frequencies", 894, 176, TFT_LIGHTGREY, 1, middle_left);
    text(g_live_snapshot.faa_aviation_installed ? "INSTALLED" : "NOT INSTALLED",
         894, 214, g_live_snapshot.faa_aviation_installed ? kGreen : TFT_ORANGE, 1, middle_left);
    button("MANAGE AVIATION DATA", 894, 248, 334, 50, TFT_DARKGREEN);

    card(496, 340, 360, 264);
    text("OFFLINE MAP", 516, 370, kBlue, 2, middle_left);
    text(maps::installed() ? "Local maps available" : "No local map discovered",
         516, 414, TFT_WHITE, 1, middle_left);
    text("Firmware overview / compatible SD packs", 516, 448, kMuted, 1, middle_left);
    button("MANAGE OFFLINE MAP", 516, 516, 320, 54, TFT_NAVY);

    card(874, 340, 374, 264);
    text("ATC AUDIO", 894, 370, kBlue, 2, middle_left);
    text(g_settings.atc_frequency_hz ? g_settings.atc_label : "No verified nearby preset",
         894, 414, TFT_WHITE, 1, middle_left);
    if (g_settings.atc_frequency_hz) {
      snprintf(value, sizeof(value), "%lu.%03lu MHz",
               static_cast<unsigned long>(g_settings.atc_frequency_hz / 1000000),
               static_cast<unsigned long>((g_settings.atc_frequency_hz % 1000000) / 1000));
      text(value, 894, 448, kGreen, 2, middle_left);
    }
    button(g_atc_listening ? "RESUME ADS-B" : "LISTEN MANUALLY",
           894, 516, 334, 54,
           g_settings.atc_frequency_hz ? TFT_DARKGREEN : TFT_DARKGREY);
  }
}

void draw_radar_responsive() {
  const auto s=g_layout.radar_status,st=g_layout.radar_stats,l=g_layout.radar_list;
  card(s.x,s.y,s.w,s.h);text("RECEIVER",s.x+10,s.y+18,kBlue,1,middle_left);
  text(g_live?"RECEIVING":"WAITING",s.x+10,s.y+46,g_live?kGreen:TFT_ORANGE,2,middle_left);
  card(st.x,st.y,st.w,st.h);text("LIVE METRICS",st.x+10,st.y+18,kBlue,1,middle_left);
  char value[32];snprintf(value,sizeof(value),"%u aircraft",displayed_aircraft_count());
  text(value,st.x+12,st.y+48,TFT_WHITE,2,middle_left);
  snprintf(value,sizeof(value),"%lu messages",static_cast<unsigned long>(displayed_total_messages()));
  text(value,st.x+12,st.y+78,kBlue,1,middle_left);
  snprintf(value,sizeof(value),"%.1f msg/sec",displayed_message_rate());
  text(value,st.x+12,st.y+104,kBlue,1,middle_left);
  card(l.x,l.y,l.w,l.h);text("NEAREST RECEIVED",l.x+8,l.y+14,kBlue,1,middle_left);
  const int row_h=std::max(24,(l.h-30)/6);
  for(size_t i=0;i<std::min<size_t>(g_aircraft_count,6);++i) {
    const int y=l.y+28+static_cast<int>(i)*row_h;
    text(g_aircraft[i].callsign[0]?g_aircraft[i].callsign:"--",l.x+8,y,kGreen,1,middle_left);
    if(g_aircraft[i].has_position)snprintf(value,sizeof(value),"%.0f NM",g_aircraft[i].range_nm);
    else strlcpy(value,"--",sizeof(value));
    text(value,l.x+l.w-8,y,kMuted,1,middle_right);
  }
  draw_radar_map();
}

void draw_list_responsive() {
  const auto rows=g_layout.list_rows,summary=g_layout.list_summary;
  card(rows.x,rows.y,rows.w,rows.h);
  text("AIRCRAFT RECEIVED",rows.x+10,rows.y+15,kBlue,1,middle_left);
  if(!g_layout.portrait)button(g_locked?"LOCKED":"LOCK",g_layout.list_lock.x,g_layout.list_lock.y,
                               g_layout.list_lock.w,g_layout.list_lock.h,TFT_DARKGREY);
  for(size_t i=0;i<std::min<size_t>(g_aircraft_count,6);++i) {
    const auto r=g_layout.list_items[i];
    if(i==g_selected)M5.Display.fillRoundRect(r.x,r.y,r.w,r.h,6,0x1362);
    char identity[40];snprintf(identity,sizeof(identity),"%s  %06lX",g_aircraft[i].callsign,
                                static_cast<unsigned long>(g_aircraft[i].icao));
    text(identity,r.x+8,r.y+r.h/2-8,i==g_selected?kGreen:TFT_WHITE,1,middle_left);
    char detail[56];snprintf(detail,sizeof(detail),"%s  %d ft  %d kt",g_aircraft[i].registration,
                             g_aircraft[i].altitude_ft,g_aircraft[i].speed_kts);
    text(detail,r.x+8,r.y+r.h/2+10,kMuted,1,middle_left);
  }
  if(summary.w>0&&summary.h>0) {
    card(summary.x,summary.y,summary.w,summary.h);
    if(!g_layout.portrait){text("SELECT AN AIRCRAFT",summary.x+10,summary.y+20,kBlue,1,middle_left);return;}
    if(g_aircraft_count) {
      const auto& a=g_aircraft[g_selected];
      button(g_locked?"LOCKED":"LOCK",g_layout.list_lock.x,g_layout.list_lock.y,
             g_layout.list_lock.w,g_layout.list_lock.h,TFT_DARKGREY);
      text(a.callsign,summary.x+12,summary.y+68,kGreen,3,middle_left);
      char detail[56];snprintf(detail,sizeof(detail),"%06lX  %s",
          static_cast<unsigned long>(a.icao),a.registration[0]?a.registration:"NO REG");
      text(detail,summary.x+12,summary.y+112,TFT_WHITE,1,middle_left);
    }
  }
}

void draw_target_responsive() {
  const auto id=g_layout.target_identity,m=g_layout.target_metrics;
  if(!g_aircraft_count){card(g_layout.content.x,g_layout.content.y,g_layout.content.w,g_layout.content.h);
    text("SEARCHING FOR AIRCRAFT",g_layout.content.x+g_layout.content.w/2,
         g_layout.content.y+g_layout.content.h/2,TFT_WHITE,2);return;}
  const auto& a=g_aircraft[g_selected];card(id.x,id.y,id.w,id.h);
  text(a.callsign,id.x+12,id.y+25,kGreen,3,middle_left);
  button(a.stale?"STALE":g_locked?"LOCKED":"LOCK",g_layout.target_lock.x,
         g_layout.target_lock.y,g_layout.target_lock.w,g_layout.target_lock.h,TFT_DARKGREY);
  char value[56];snprintf(value,sizeof(value),"ICAO %06lX",static_cast<unsigned long>(a.icao));
  text(value,id.x+12,id.y+68,TFT_WHITE,1,middle_left);
  text(a.registration[0]?a.registration:"NO REGISTRATION",id.x+12,id.y+94,kMuted,1,middle_left);
  text(a.type[0]?a.type:"AIRCRAFT TYPE UNKNOWN",id.x+12,id.y+120,kMuted,1,middle_left);
  text(a.op[0]?a.op:"OPERATOR UNKNOWN",id.x+12,id.y+146,kMuted,1,middle_left);
  const char* atc=g_atc_listening?"RESUME ADS-B":g_settings.atc_frequency_hz?"LISTEN TO ATC":"NO ATC PRESET";
  button(atc,g_layout.target_atc_button.x,g_layout.target_atc_button.y,
         g_layout.target_atc_button.w,g_layout.target_atc_button.h,
         g_settings.atc_frequency_hz||g_atc_listening?TFT_DARKGREEN:TFT_DARKGREY);
  const char* labels[]={"ALTITUDE","SPEED","HEADING","VERT RATE","RANGE","BEARING","LATITUDE","LONGITUDE"};
  const int cols=g_layout.portrait?2:3, gap=8;
  const int cell_w=(m.w-(cols-1)*gap)/cols;
  const int cell_h=std::max(52,(m.h-88)/((8+cols-1)/cols));
  for(int i=0;i<8;++i) {
    const int col=i%cols,row=i/cols;const int x=m.x+col*(cell_w+gap),y=m.y+row*cell_h;
    M5.Display.drawRoundRect(x,y,cell_w,cell_h-4,6,kBorder);
    text(labels[i],x+8,y+13,kBlue,1,middle_left);
    switch(i) {
      case 0:if(a.has_altitude)snprintf(value,sizeof(value),"%d ft",a.altitude_ft);else strlcpy(value,"--",sizeof(value));break;
      case 1:if(a.has_speed)snprintf(value,sizeof(value),"%d kt",a.speed_kts);else strlcpy(value,"--",sizeof(value));break;
      case 2:if(a.has_heading)snprintf(value,sizeof(value),"%03d deg",a.heading_deg);else strlcpy(value,"--",sizeof(value));break;
      case 3:if(a.has_vertical_rate)snprintf(value,sizeof(value),"%+d ft/min",a.vertical_rate_fpm);else strlcpy(value,"--",sizeof(value));break;
      case 4:if(a.has_position)snprintf(value,sizeof(value),"%.0f NM",a.range_nm);else strlcpy(value,"--",sizeof(value));break;
      case 5:if(a.has_position)snprintf(value,sizeof(value),"%03d deg",a.bearing_deg);else strlcpy(value,"--",sizeof(value));break;
      case 6:if(a.has_position)snprintf(value,sizeof(value),"%.3f",a.latitude);else strlcpy(value,"--",sizeof(value));break;
      default:if(a.has_position)snprintf(value,sizeof(value),"%.3f",a.longitude);else strlcpy(value,"--",sizeof(value));break;
    }
    text(value,x+8,y+cell_h/2+5,TFT_WHITE,2,middle_left);
  }
  card(g_layout.target_signal.x,g_layout.target_signal.y,g_layout.target_signal.w,g_layout.target_signal.h);
  text("SIGNAL / SOURCE",g_layout.target_signal.x+12,g_layout.target_signal.y+20,kBlue,1,middle_left);
  text(g_live?"ADS-B 1090 MHz":"WAITING",g_layout.target_signal.x+12,
       g_layout.target_signal.y+50,kGreen,2,middle_left);
  button("SHOW ON MAP",g_layout.target_map_button.x,g_layout.target_map_button.y,
         g_layout.target_map_button.w,g_layout.target_map_button.h,TFT_NAVY);
}

void draw_stats_responsive() {
  char value[32];
  const char* titles[]={"SIGNAL","MESSAGE RATE","MODE-S HISTORY"};
  for(int i=0;i<3;++i){const auto r=g_layout.stats_charts[i];card(r.x,r.y,r.w,r.h);text(titles[i],r.x+8,r.y+16,kBlue,1,middle_left);}
  snprintf(value,sizeof(value),"%.1f dBFS",g_live_snapshot.strongest_signal_dbfs);
  text(value,g_layout.stats_charts[0].x+10,g_layout.stats_charts[0].y+50,TFT_WHITE,2,middle_left);
  snprintf(value,sizeof(value),"%.1f / sec",displayed_message_rate());
  text(value,g_layout.stats_charts[1].x+10,g_layout.stats_charts[1].y+50,TFT_WHITE,2,middle_left);
  const int max_h=std::max(16,g_layout.stats_charts[2].h-40);
  for(size_t i=0;i<g_history_count;++i){const auto r=g_layout.stats_charts[2];
    const int x=r.x+8+static_cast<int>(i)*(r.w-16)/kHistorySamples;
    const int h=std::clamp(static_cast<int>((g_signal_history[i]+100)*max_h/100),2,max_h);
    M5.Display.fillRect(x,r.y+r.h-8-h,6,h,kGreen);}
  const char* labels[]={"AIRCRAFT","MESSAGES","SIGNAL","SAMPLE RATE","DROPS"};
  snprintf(value,sizeof(value),"%u",displayed_aircraft_count());
  const char* vals[]={value,nullptr,nullptr,nullptr,nullptr};char message_count[24],signal[24],rate[24],drops[24];
  snprintf(message_count,sizeof(message_count),"%lu",static_cast<unsigned long>(displayed_total_messages()));
  snprintf(signal,sizeof(signal),"%.1f dBFS",g_live_snapshot.strongest_signal_dbfs);
  snprintf(rate,sizeof(rate),"%.3f MS/s",g_live_snapshot.effective_sps/1.0e6);
  snprintf(drops,sizeof(drops),"%lu/%lu",static_cast<unsigned long>(g_live_snapshot.usb_overruns),static_cast<unsigned long>(g_live_snapshot.consumer_drops));
  vals[1]=message_count;vals[2]=signal;vals[3]=rate;vals[4]=drops;
  for(int i=0;i<5;++i){const auto r=g_layout.stats_metrics[i];card(r.x,r.y,r.w,r.h);
    text(labels[i],r.x+6,r.y+12,kBlue,1,middle_left);text(vals[i],r.x+6,r.y+r.h-13,TFT_WHITE,1,middle_left);}
  for(int i=0;i<4;++i){const auto r=g_layout.stats_data[i];card(r.x,r.y,r.w,r.h);
    const char* label=i==0?"FAA AIRCRAFT":i==1?"FAA AVIATION":i==2?"OFFLINE MAP":"ATC PRESET";
    text(label,r.x+6,r.y+12,kBlue,1,middle_left);
    text(i==2?(maps::installed()?"AVAILABLE":"NONE"):i==3?(g_settings.atc_frequency_hz?"AVAILABLE":"NONE"):"DATA STATUS",
         r.x+6,r.y+r.h-12,kMuted,1,middle_left);}
}

void draw_settings_responsive() {
  const auto r=g_layout.settings_main;card(r.x,r.y,r.w,r.h);text("ADS-B SETTINGS",r.x+10,r.y+20,kBlue,2,middle_left);
  char value[40];
  text("RECEIVER LATITUDE",g_layout.latitude.x,g_layout.latitude.y-8,kMuted,1,middle_left);
  if(g_latitude_set)snprintf(value,sizeof(value),"%.5f",g_settings.latitude_e7/1.0e7);else strlcpy(value,"NOT SET",sizeof(value));
  button(value,g_layout.latitude.x,g_layout.latitude.y,g_layout.latitude.w,g_layout.latitude.h,TFT_NAVY);
  text("RECEIVER LONGITUDE",g_layout.longitude.x,g_layout.longitude.y-8,kMuted,1,middle_left);
  if(g_longitude_set)snprintf(value,sizeof(value),"%.5f",g_settings.longitude_e7/1.0e7);else strlcpy(value,"NOT SET",sizeof(value));
  button(value,g_layout.longitude.x,g_layout.longitude.y,g_layout.longitude.w,g_layout.longitude.h,TFT_NAVY);
  snprintf(value,sizeof(value),"RANGE %u NM",g_settings.radar_range_nm);
  button(value,g_layout.range.x,g_layout.range.y,g_layout.range.w,g_layout.range.h,TFT_DARKCYAN);
  button(!g_settings.gain_supported?"GAIN N/A":g_settings.gain_auto?"AUTO / MANUAL":"SET GAIN",
         g_layout.gain.x,g_layout.gain.y,g_layout.gain.w,g_layout.gain.h,TFT_DARKGREEN);
  button("EXIT ADS-B",g_layout.exit.x,g_layout.exit.y,g_layout.exit.w,g_layout.exit.h,TFT_MAROON);
  for(int i=0;i<4;++i){const auto b=g_layout.settings_data[i];
    card(b.x,b.y,b.w,b.h);
    const char* label=i==0?"FAA DB":i==1?"AVIATION":i==2?"OFFLINE MAP":"ATC";
    text(label,b.x+6,b.y+13,kBlue,1,middle_left);
    text(i==2?(maps::installed()?"READY":"NONE"):i==3?(g_settings.atc_frequency_hz?"READY":"NONE"):"STATUS",
         b.x+6,b.y+b.h-12,kMuted,1,middle_left);
  }
  if(g_edit!=EditField::none)draw_keypad();
}

void redraw_content() {
  if(!g_layout.legacy) {
    const auto r=g_layout.content;M5.Display.fillRect(r.x,r.y,r.w,r.h,kBg);
    switch(g_view) {
      case View::radar:draw_radar_responsive();break;
      case View::list:draw_list_responsive();break;
      case View::target:draw_target_responsive();break;
      case View::stats:draw_stats_responsive();break;
      case View::settings:draw_settings_responsive();break;
      default:break;
    }
    return;
  }
  M5.Display.fillRect(0, kHeaderH, 1280, kTabsY - kHeaderH, kBg);
  switch (g_view) {
    case View::radar: draw_radar(); break;
    case View::list: draw_list(); break;
    case View::target: draw_target(); break;
    case View::stats: draw_stats(); break;
    case View::settings: draw_settings(); break;
    default: break;
  }
}

void redraw() {
  if(g_layout.legacy) M5.Display.fillScreen(kBg);
  else M5.Display.fillRect(g_layout.viewport.x,g_layout.viewport.y,
                           g_layout.viewport.w,g_layout.viewport.h,kBg);
  draw_header();
  redraw_content();
  draw_tabs();
}

Action handle_keypad(int32_t x, int32_t y) {
  const auto cancel=g_layout.legacy?layout::Rect{730,508,230,58}:g_layout.keypad_cancel;
  const auto save=g_layout.legacy?layout::Rect{990,508,230,58}:g_layout.keypad_save;
  if (hit(x,y,cancel.x,cancel.y,cancel.w,cancel.h)) {
    g_edit = EditField::none;
    redraw();
    return Action::none;
  }
  if (hit(x,y,save.x,save.y,save.w,save.h)) {
    char* end = nullptr;
    const double value = strtod(g_entry, &end);
    if (end != g_entry && *end == '\0' && valid_coordinate(g_edit, value)) {
      const int32_t e7 = static_cast<int32_t>(llround(value * 10000000.0));
      if (g_edit == EditField::latitude) {
        g_settings.latitude_e7 = e7;
        g_latitude_set = true;
      } else {
        g_settings.longitude_e7 = e7;
        g_longitude_set = true;
      }
      g_settings.location_configured = g_latitude_set && g_longitude_set;
      g_edit = EditField::none;
      if (g_live) apply_live_snapshot();
      redraw();
      return g_settings.location_configured ? Action::settings_changed : Action::none;
    }
    return Action::none;
  }
  static constexpr char keys[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9',
                                   '-', '0', '.', '\b'};
  for (int i = 0; i < 13; ++i) {
    if(!g_layout.legacy) {
      const auto r=g_layout.keypad_keys[i];
      if(!hit(x,y,r.x,r.y,r.w,r.h))continue;
    } else {
    const int col = i % 4, row = i / 4;
      if (!hit(x, y, 730 + col * 122, 245 + row * 64, 112, 54)) continue;
    }
    const size_t len = strlen(g_entry);
    if (keys[i] == '\b') {
      if (len) g_entry[len - 1] = '\0';
    } else if (keys[i] == '-') {
      if (g_entry[0] == '-') memmove(g_entry, g_entry + 1, len);
      else if (len + 1 < sizeof(g_entry)) {
        memmove(g_entry + 1, g_entry, len + 1);
        g_entry[0] = '-';
      }
    } else if (len + 1 < sizeof(g_entry) &&
               (keys[i] != '.' || strchr(g_entry, '.') == nullptr)) {
      g_entry[len] = keys[i];
      g_entry[len + 1] = '\0';
    }
    draw_keypad();
    return Action::none;
  }
  return Action::none;
}

}  // namespace

void enter(const Settings& settings_value) {
  g_settings = settings_value;
  g_view = View::radar;
  g_edit = EditField::none;
  g_selected = 0;
  g_locked = false;
  g_active = true;
  g_latitude_set = settings_value.location_configured;
  g_longitude_set = settings_value.location_configured;
  apply_live_snapshot();
  g_drawn_revision = g_live_snapshot.revision;
  redraw();
}

void leave() {
  g_active = false;
  M5.Display.setFont(nullptr);
}

void draw() { if (g_active) redraw(); }

void update() {
  if (!g_active || !g_live || (g_drawn_revision == g_live_snapshot.revision &&
      (g_view != View::radar || g_radar_map_revision == maps::revision()))) return;
  static uint32_t last_draw_ms = 0;
  if (millis() - last_draw_ms < 1000) return;
  last_draw_ms = millis();
  apply_live_snapshot();
  g_drawn_revision = g_live_snapshot.revision;
  // Live repaint deliberately skips redraw_content()'s full black clear.
  if(!g_layout.legacy) {
    switch(g_view) {
      case View::radar:draw_radar_responsive();break;
      case View::list:draw_list_responsive();break;
      case View::target:draw_target_responsive();break;
      case View::stats:draw_stats_responsive();break;
      case View::settings:draw_settings_responsive();break;
      default:break;
    }
    return;
  }
  switch (g_view) {
    case View::radar: draw_radar(); break;
    case View::list: draw_list(); break;
    case View::target: draw_target(); break;
    case View::stats: draw_stats(); break;
    case View::settings: draw_settings(); break;
    default: break;
  }
}

void set_live_snapshot(const Snapshot& snapshot) {
  if (snapshot.revision != g_live_snapshot.revision) {
    if (g_history_count < kHistorySamples) {
      ++g_history_count;
    } else {
      for (size_t i = 1; i < kHistorySamples; ++i) {
        g_rate_history[i - 1] = g_rate_history[i];
        g_signal_history[i - 1] = g_signal_history[i];
      }
    }
    g_rate_history[g_history_count - 1] = snapshot.message_rate;
    g_signal_history[g_history_count - 1] = snapshot.strongest_signal_dbfs;
  }
  g_live_snapshot = snapshot;
  g_live = true;
}

void set_atc_listening(bool listening, uint32_t frequency_hz) {
  g_atc_listening = listening;
  if (frequency_hz >= 118000000 && frequency_hz <= 137000000)
    g_settings.atc_frequency_hz = frequency_hz;
  if (g_active) redraw();
}

uint32_t atc_frequency_hz() { return g_settings.atc_frequency_hz; }

bool set_map_range(uint16_t range_nm) {
  if(!valid_map_range(range_nm))return false;
  if(g_settings.radar_range_nm!=range_nm) {
    g_settings.radar_range_nm=range_nm;
    maps::invalidate();
  }
  return true;
}

Action handle_touch(int32_t x, int32_t y) {
  if (!g_active) return Action::none;
  if (g_edit != EditField::none) return handle_keypad(x, y);
  if (g_layout.legacy ? y >= kTabsY :
      (y >= g_layout.tabs[0].y && y < g_layout.tabs[0].y+g_layout.tabs[0].h)) {
    int index=g_layout.legacy?constrain(x/kTabW,0,4):-1;
    if(!g_layout.legacy)for(int i=0;i<5;++i)if(hit(x,y,g_layout.tabs[i].x,g_layout.tabs[i].y,
                                                  g_layout.tabs[i].w,g_layout.tabs[i].h)){index=i;break;}
    if(index<0)return Action::none;
    const View requested=static_cast<View>(index);
    g_view=requested;
    redraw();
    return Action::none;
  }
  if (g_view == View::radar) {
    const auto labels=g_layout.legacy?layout::Rect{26,252,186,48}:g_layout.map_labels;
    if(hit(x,y,labels.x,labels.y,labels.w,labels.h)) {
      using maps::preferences::Consumer;
      g_map_label_error=!maps::preferences::set_labels(Consumer::adsb,!maps::preferences::labels(Consumer::adsb));
      redraw_content();return Action::none;
    }
    const auto zin=g_layout.legacy?layout::Rect{kZoomX,kZoomY,kZoomSize,kZoomSize}:g_layout.map_zoom_in;
    const auto zout=g_layout.legacy?layout::Rect{kZoomX,kZoomY+kZoomStep,kZoomSize,kZoomSize}:g_layout.map_zoom_out;
    const bool zoom_in=hit(x,y,zin.x,zin.y,zin.w,zin.h);
    if (zoom_in || hit(x,y,zout.x,zout.y,zout.w,zout.h)) {
      const auto range=zoomed_range(g_settings.radar_range_nm,zoom_in);
      if(range==g_settings.radar_range_nm)return Action::none;
      set_map_range(range);
      draw_radar_map();
      if(g_layout.legacy)draw_radar_range();else redraw_content();
      return Action::settings_changed;
    }
    for (size_t i = 0; i < g_aircraft_count; ++i) {
      if (!g_aircraft[i].has_position || !g_settings.location_configured) continue;
      int px=0,py=0;
      if(!radar_position(g_aircraft[i],&px,&py))continue;
      const int dx = x - px, dy = y - py;
      if (dx * dx + dy * dy <= 32 * 32) {
        g_selected = i;
        redraw_content();
        return Action::none;
      }
    }
    for (size_t i = 0; i < g_aircraft_count && i < 7; ++i) {
      const auto r=g_layout.legacy?layout::Rect{906,175+static_cast<int>(i)*57,344,54}:
        layout::Rect{g_layout.radar_list.x,g_layout.radar_list.y+28+static_cast<int>(i)*
          std::max(24,(g_layout.radar_list.h-30)/6),g_layout.radar_list.w,
          std::max(24,(g_layout.radar_list.h-30)/6)};
      if (hit(x,y,r.x,r.y,r.w,r.h)) {
        g_selected = i;
        redraw_content();
        return Action::none;
      }
    }
  } else if (g_view == View::list) {
    for (size_t i = 0; i < g_aircraft_count; ++i) {
      const auto r=g_layout.legacy?layout::Rect{26,143+static_cast<int>(i)*76,818,68}:
                                  g_layout.list_items[std::min<size_t>(i,5)];
      if (hit(x,y,r.x,r.y,r.w,r.h)) {
        g_selected = i;
        redraw_content();
        return Action::none;
      }
    }
    const auto lock=g_layout.legacy?layout::Rect{1144,108,96,42}:g_layout.list_lock;
    if (hit(x,y,lock.x,lock.y,lock.w,lock.h)) {
      toggle_lock();
      redraw_content();
    }
  } else if (g_view == View::target) {
    const auto lock=g_layout.legacy?layout::Rect{300,104,110,44}:g_layout.target_lock;
    if (hit(x,y,lock.x,lock.y,lock.w,lock.h)) {
      toggle_lock();
      redraw_content();
    }
    const auto atc=g_layout.legacy?layout::Rect{230,462,250,54}:g_layout.target_atc_button;
    if (hit(x,y,atc.x,atc.y,atc.w,atc.h) &&
        (g_atc_listening || g_settings.atc_frequency_hz))
      return g_atc_listening ? Action::atc_resume : Action::atc_listen;
    const auto map=g_layout.legacy?layout::Rect{1005,550,210,54}:g_layout.target_map_button;
    if (hit(x,y,map.x,map.y,map.w,map.h)) {
      g_view = View::radar;
      g_locked = true;
      redraw();
    }
  } else if (g_view == View::settings) {
    const auto lat=g_layout.legacy?layout::Rect{225,158,225,52}:g_layout.latitude;
    const auto lon=g_layout.legacy?layout::Rect{225,240,225,52}:g_layout.longitude;
    const auto range=g_layout.legacy?layout::Rect{225,322,225,52}:g_layout.range;
    const auto gain=g_layout.legacy?layout::Rect{38,430,112,48}:g_layout.gain;
    const auto exit=g_layout.legacy?layout::Rect{38,538,412,58}:g_layout.exit;
    const auto data=g_layout.legacy?layout::Rect{496,108,752,496}:g_layout.settings_open_data;
    if (hit(x,y,lat.x,lat.y,lat.w,lat.h)) { start_edit(EditField::latitude); redraw(); }
    else if (hit(x,y,lon.x,lon.y,lon.w,lon.h)) { start_edit(EditField::longitude); redraw(); }
    else if (hit(x,y,range.x,range.y,range.w,range.h)) {
      size_t index = 0;
      while (index < std::size(kRanges) && kRanges[index] != g_settings.radar_range_nm) ++index;
      set_map_range(kRanges[(index + 1) % std::size(kRanges)]);
      redraw();
      return Action::settings_changed;
    } else if (hit(x,y,gain.x,gain.y,gain.w,gain.h) && g_settings.gain_supported) {
      if(g_settings.gain_auto_supported) {
        if(g_settings.gain_auto) {
          g_settings.gain_auto=false;redraw_content();return Action::gain_tenth_db;
        }
        g_settings.gain_auto=true;redraw_content();return Action::gain_auto;
      }
      const int relative_x=static_cast<int>(x)-gain.x;
      g_settings.gain_tenth_db=static_cast<int16_t>(std::clamp<int>(
          relative_x*kGainMaxTenthDb/std::max(1,gain.w),0,kGainMaxTenthDb));
      redraw_content();return Action::gain_tenth_db;
    } else if (g_layout.legacy&&hit(x,y,kGainSliderX-12,kGainSliderY-28,kGainSliderW+24,56) && g_settings.gain_supported) {
      g_settings.gain_tenth_db = static_cast<int16_t>(gain_from_slider_x(x));
      g_settings.gain_auto = false;
      redraw_content();
      return Action::gain_tenth_db;
    } else if (hit(x,y,exit.x,exit.y,exit.w,exit.h)) {
      g_active = false;
      return Action::exit;
    } else if (hit(x, y, 894, 516, 334, 54) &&
               (g_atc_listening || g_settings.atc_frequency_hz)) {
      return g_atc_listening ? Action::atc_resume : Action::atc_listen;
    } else if (hit(x,y,data.x,data.y,data.w,data.h)) {
      return Action::open_data_settings;
    }
  }
  return Action::none;
}

const Settings& settings() { return g_settings; }
int gain_tenth_db() { return g_settings.gain_tenth_db; }
bool active() { return g_active; }

bool set_viewport(audio_header::HeaderRect safe) {
  const auto next=layout::make_view(safe);
  if(!next.valid||!audio_header::set_viewport(safe))return false;
  const auto previous=g_layout;
  g_layout=next;
  if(g_active) {
    draw_radar_base();
    if(g_radar_cache_w!=g_layout.radar_map.w||g_radar_cache_h!=g_layout.radar_map.h||
       g_radar_cache_x!=g_layout.radar_map_area.x-g_layout.radar_map.x||
       g_radar_cache_y!=g_layout.radar_map_area.y-g_layout.radar_map.y) {
      g_layout=previous;
      (void)audio_header::set_viewport(previous.viewport);
      return false;
    }
    redraw();
  }
  return true;
}

void show_documentation_view(uint8_t requested, const Settings& settings_value) {
  if (requested >= static_cast<uint8_t>(View::count)) return;
  g_settings = settings_value;
  g_view = static_cast<View>(requested);
  g_edit = EditField::none;
  g_selected = 0;
  g_locked = false;
  g_active = true;
  g_latitude_set = settings_value.location_configured;
  g_longitude_set = settings_value.location_configured;
  apply_live_snapshot();
  redraw();
}

uint8_t view() { return static_cast<uint8_t>(g_view); }

bool self_check() {
  DisplayAircraft saved_aircraft[kVisibleAircraft];
  std::copy(std::begin(g_aircraft), std::end(g_aircraft), saved_aircraft);
  const size_t saved_count = g_aircraft_count;
  const Snapshot saved_snapshot = g_live_snapshot;
  float saved_rate_history[kHistorySamples];
  float saved_signal_history[kHistorySamples];
  std::copy(std::begin(g_rate_history), std::end(g_rate_history), saved_rate_history);
  std::copy(std::begin(g_signal_history), std::end(g_signal_history), saved_signal_history);
  const size_t saved_history_count = g_history_count;
  const size_t saved_selected = g_selected;
  const bool saved_locked = g_locked, saved_live = g_live;
  Snapshot test{};
  test.visible_count = test.aircraft_count = 1;
  test.aircraft[0].icao = 0x123456;
  strlcpy(test.aircraft[0].callsign, "TEST123", sizeof(test.aircraft[0].callsign));
  test.aircraft[0].has_callsign = true;
  g_live_snapshot = test;
  g_live = true;
  g_locked = false;
  apply_live_snapshot();
  bool model_ok = g_aircraft_count == 1 && g_aircraft[0].icao == 0x123456 &&
                  aircraft_index(0x123456) == 0;
  test.aircraft[0].has_callsign = false;
  strlcpy(test.aircraft[0].registration, "N12345", sizeof(test.aircraft[0].registration));
  g_live_snapshot = test;
  apply_live_snapshot();
  model_ok = model_ok && strcmp(g_aircraft[0].callsign, "N12345") == 0;
  g_locked = true;
  g_live_snapshot.visible_count = g_live_snapshot.aircraft_count = 0;
  apply_live_snapshot();
  model_ok = model_ok && g_aircraft_count == 1 && g_aircraft[0].stale;
  g_locked = false;
  apply_live_snapshot();
  model_ok = model_ok && g_aircraft_count == 0;
  std::copy(std::begin(saved_aircraft), std::end(saved_aircraft), g_aircraft);
  g_aircraft_count = saved_count;
  g_live_snapshot = saved_snapshot;
  std::copy(std::begin(saved_rate_history), std::end(saved_rate_history), g_rate_history);
  std::copy(std::begin(saved_signal_history), std::end(saved_signal_history), g_signal_history);
  g_history_count = saved_history_count;
  g_selected = saved_selected;
  g_locked = saved_locked;
  g_live = saved_live;

  return model_ok && keep_stale_selection(true, true) &&
         !keep_stale_selection(false, true) &&
         keep_stale_selection(false, false) &&
         altitude_bin(-1000) == 0 && altitude_bin(4999) == 0 &&
         altitude_bin(5000) == 1 && altitude_bin(60000) == kAltitudeBins - 1 &&
         gain_from_slider_x(kGainSliderX - 20) == kGainMinTenthDb &&
         gain_from_slider_x(kGainSliderX + kGainSliderW + 20) == kGainMaxTenthDb &&
         valid_coordinate(EditField::latitude, -90.0) &&
         valid_coordinate(EditField::latitude, 90.0) &&
         !valid_coordinate(EditField::latitude, 90.01) &&
         valid_coordinate(EditField::longitude, -180.0) &&
         !valid_coordinate(EditField::longitude, 180.01);
}

}  // namespace orcsdr::adsb
