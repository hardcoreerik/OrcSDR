// ADS-B screen: the part of OrcSDR's main.cpp that builds the dashboard's snapshot (visible aircraft, message rate, strength) and
// owns its settings (receiver location, radar range, gain), driven by the node's aircraft table instead of an on-board decoder.
#include <M5Unified.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "adsb_dashboard.hpp"
#include "dashboard_audio_control.hpp"
#include "data_screens.hpp"

namespace {

using namespace orcsdr;
using namespace dscreens;

constexpr uint32_t kAdsbHz = 1090000000;
constexpr size_t kMax = 64;

struct Track {
  uint32_t icao = 0;
  char callsign[9]{};
  int alt = 0, speed = 0, heading = 0, vrate = 0;
  float lat = 0, lon = 0, signal_dbfs = -90;
  bool has_callsign = false, has_alt = false, has_speed = false, has_heading = false, has_vrate = false, has_pos = false;
};

adsb::Settings g_settings;
Track g_tracks[kMax];
Track g_building[kMax];
size_t g_count = 0, g_building_count = 0;
uint32_t g_total = 0, g_last_total = 0, g_last_rate_ms = 0, g_revision = 0, g_last_publish = 0;
float g_rate = 0;
bool g_receiving = false;
uint32_t g_sps = 2048000;
char g_request[200];
bool g_request_pending = false;

void request(const char* text) {
  snprintf(g_request, sizeof g_request, "%s", text);
  g_request_pending = true;
}

void request_start() {
  char r[96];
  snprintf(r, sizeof r, "{\"op\":\"start\",\"hz\":%u,\"options\":\"\"}", (unsigned)kAdsbHz);
  request(r);
}

float to_dbfs(double signal) {   // the decoder's pulse-minus-noise magnitude, full scale about 254 (|I|+|Q| of CU8)
  return 20.0f * log10f((float)(signal < 1 ? 1 : signal) / 254.0f);
}

void publish(uint32_t now) {
  adsb::Snapshot s{};
  for (size_t i = 0; i < g_count && s.visible_count < adsb::kVisibleAircraft; ++i) {
    const Track& t = g_tracks[i];
    adsb::Aircraft& a = s.aircraft[s.visible_count++];
    a.icao = t.icao;
    snprintf(a.callsign, sizeof a.callsign, "%s", t.callsign);
    a.altitude_ft = t.alt;
    a.speed_kts = t.speed;
    a.heading_deg = t.heading;
    a.vertical_rate_fpm = t.vrate;
    a.latitude = t.lat;
    a.longitude = t.lon;
    a.signal_dbfs = t.signal_dbfs;
    a.has_callsign = t.has_callsign;
    a.has_altitude = t.has_alt;
    a.has_speed = t.has_speed;
    a.has_heading = t.has_heading;
    a.has_vertical_rate = t.has_vrate;
    a.has_position = t.has_pos;
    if (t.signal_dbfs > s.strongest_signal_dbfs || s.visible_count == 1) s.strongest_signal_dbfs = t.signal_dbfs;
  }
  s.aircraft_count = (uint8_t)(g_count > 255 ? 255 : g_count);
  s.total_messages = g_total;
  s.message_rate = g_rate;
  s.effective_sps = g_receiving ? g_sps : 0;
  s.sound_enabled = false;
  s.revision = ++g_revision;
  adsb::set_live_snapshot(s);
  g_last_publish = now;
}

void enter(uint32_t now) {
  g_settings.gain_supported = true;
  g_settings.gain_auto_supported = true;
  publish(now);
  adsb::enter(g_settings);
  request_start();
}

void leave() { adsb::leave(); }

void update(uint32_t now) {
  if (now - g_last_rate_ms >= 1000) {
    g_rate = (g_total - g_last_total) * 1000.0f / (float)(now - g_last_rate_ms);
    g_last_total = g_total;
    g_last_rate_ms = now;
  }
  if (now - g_last_publish >= 1000) publish(now);
  adsb::update();
}

Result touch(int x, int y, bool pressed, uint32_t now) {
  Result r;
  if (!pressed) return r;
  if (audio_header::home_hit(x, y)) { r.exit = true; return r; }
  const adsb::Action a = adsb::handle_touch(x, y);
  char text[120];
  switch (a) {
    case adsb::Action::exit: r.exit = true; break;
    case adsb::Action::settings_changed:
      g_settings = adsb::settings();
      snprintf(text, sizeof text, "{\"op\":\"setting\",\"key\":\"adsb\",\"lat_e7\":%d,\"lon_e7\":%d,\"range\":%u,\"loc\":%d}", (int)g_settings.latitude_e7,
               (int)g_settings.longitude_e7, (unsigned)g_settings.radar_range_nm, g_settings.location_configured ? 1 : 0);
      request(text);
      break;
    case adsb::Action::gain_auto:
      request("{\"op\":\"gain\",\"auto\":1}");
      break;
    case adsb::Action::gain_tenth_db:
      snprintf(text, sizeof text, "{\"op\":\"gain\",\"auto\":0,\"tenth\":%d}", adsb::gain_tenth_db());
      request(text);
      break;
    default: break;
  }
  if (a != adsb::Action::none) publish(now);
  return r;
}

void event(const char* line, uint32_t) {
  char type[24] = {};
  str(line, "type", type, sizeof type);
  if (!strcmp(type, "aircraft_begin")) {
    g_building_count = 0;
    g_total = (uint32_t)num(line, "messages");
  } else if (!strcmp(type, "aircraft") && g_building_count < kMax) {
    Track& t = g_building[g_building_count++];
    t = {};
    char icao[8] = {};
    str(line, "icao", icao, sizeof icao);
    t.icao = (uint32_t)strtoul(icao, nullptr, 16);
    t.has_callsign = str(line, "callsign", t.callsign, sizeof t.callsign) > 0;
    t.signal_dbfs = to_dbfs(num(line, "signal"));
    if (field(line, "alt_ft")) { t.alt = (int)num(line, "alt_ft"); t.has_alt = true; }
    if (field(line, "speed_kts")) { t.speed = (int)num(line, "speed_kts"); t.has_speed = true; }
    if (field(line, "heading_deg")) { t.heading = (int)num(line, "heading_deg"); t.has_heading = true; }
    if (field(line, "vrate_fpm")) { t.vrate = (int)num(line, "vrate_fpm"); t.has_vrate = true; }
    if (field(line, "lat")) { t.lat = (float)num(line, "lat"); t.lon = (float)num(line, "lon"); t.has_pos = true; }
  } else if (!strcmp(type, "aircraft_end")) {
    memcpy(g_tracks, g_building, sizeof(Track) * g_building_count);
    g_count = g_building_count;
  }
}

void set_num(const char* key, double v) {
  if (!strcmp(key, "receiving")) g_receiving = v != 0;
  else if (!strcmp(key, "loc_set")) g_settings.location_configured = v != 0;
  else if (!strcmp(key, "lat_e7")) g_settings.latitude_e7 = (int32_t)v;
  else if (!strcmp(key, "lon_e7")) g_settings.longitude_e7 = (int32_t)v;
  else if (!strcmp(key, "range")) g_settings.radar_range_nm = (uint16_t)v;
  else if (!strcmp(key, "sps")) g_sps = (uint32_t)v;
}

const char* take_request() {
  if (!g_request_pending) return nullptr;
  g_request_pending = false;
  return g_request;
}

}  // namespace

namespace dscreens {
extern const Screen kAdsb = {dashboards::Id::adsb, "adsb", enter, leave, update, touch, event, set_num, take_request, nullptr};
}
