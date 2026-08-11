#include "adsb_dashboard.hpp"

#include <M5Unified.h>

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
constexpr int kHeaderH = 76;
constexpr int kTabsY = 646;
constexpr int kTabW = 256;
constexpr uint16_t kRanges[] = {10, 25, 50, 100};

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

constexpr DisplayAircraft kDemoAircraft[] = {
    {0xA1B2C3, "DAL123", "N123DA", "A320", "DELTA AIR LINES", 34000, 452, 45, 1280,
     23.0f, 45, 37.7749f, -122.4194f, -45.0f, true, true, true, true, true, false},
    {0xA4B5C6, "UAL456", "N456UA", "B738", "UNITED AIRLINES", 37000, 468, 310, -320,
     31.0f, 310, 37.9550f, -122.7200f, -50.0f, true, true, true, true, true, false},
    {0xA7B8C9, "AAL789", "N789AA", "A321", "AMERICAN AIRLINES", 28000, 425, 278, 640,
     18.0f, 278, 37.7200f, -122.7300f, -53.0f, true, true, true, true, true, false},
    {0xAA11BB, "SWA234", "N234SW", "B737", "SOUTHWEST AIRLINES", 31000, 410, 122, 0,
     45.0f, 122, 37.2400f, -121.9100f, -58.0f, true, true, true, true, true, false},
    {0xCC22DD, "N12345", "N12345", "C172", "PRIVATE", 12500, 250, 196, -160,
     12.0f, 196, 37.5800f, -122.4100f, -61.0f, true, true, true, true, true, false},
    {0xEE33FF, "FFT567", "N567FF", "A20N", "FRONTIER AIRLINES", 41000, 490, 70, 320,
     67.0f, 70, 38.2100f, -121.6000f, -64.0f, true, true, true, true, false, false},
};

static_assert(static_cast<uint8_t>(View::count) == 5);
static_assert(kRanges[0] == 10 && kRanges[1] == 25 && kRanges[2] == 50 &&
              kRanges[3] == 100);

Settings g_settings;
View g_view = View::radar;
EditField g_edit = EditField::none;
size_t g_selected = 0;
bool g_locked = false;
bool g_active = false;
bool g_latitude_set = false;
bool g_longitude_set = false;
char g_entry[20]{};
DisplayAircraft g_aircraft[kVisibleAircraft]{};
size_t g_aircraft_count = std::size(kDemoAircraft);
Snapshot g_live_snapshot{};
uint32_t g_drawn_revision = 0;
bool g_live = false;

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
  M5.Display.setTextDatum(datum);
  M5.Display.setTextSize(size);
  M5.Display.setTextColor(color, kBg);
  M5.Display.drawString(value, x, y);
}

void card(int x, int y, int w, int h) {
  M5.Display.fillRoundRect(x, y, w, h, 12, kPanel);
  M5.Display.drawRoundRect(x, y, w, h, 12, kBorder);
}

void button(const char* label, int x, int y, int w, int h, uint16_t color) {
  M5.Display.fillRoundRect(x, y, w, h, 8, color);
  M5.Display.drawRoundRect(x, y, w, h, 8, TFT_LIGHTGREY);
  text(label, x + w / 2, y + h / 2, TFT_WHITE, 2);
}

void plane(int x, int y, int scale, uint16_t color) {
  M5.Display.fillRect(x - scale / 8, y - scale / 2, scale / 4, scale, color);
  M5.Display.fillTriangle(x, y - scale / 2, x - scale / 5, y - scale / 3,
                          x + scale / 5, y - scale / 3, color);
  M5.Display.fillTriangle(x - scale, y, x + scale, y, x, y + scale / 5, color);
  M5.Display.fillTriangle(x - scale / 2, y + scale / 2,
                          x + scale / 2, y + scale / 2, x, y + scale / 3, color);
}

void draw_header() {
  M5.Display.fillRect(0, 0, 1280, kHeaderH, kBg);
  M5.Display.drawFastHLine(20, kHeaderH - 1, 1240, kBorder);
  text("OrcSDR", 24, 37, TFT_WHITE, 3, middle_left);
  text("ADS-B 1090", 250, 37, kBlue, 3);
  M5.Display.fillCircle(423, 37, 7, kGreen);
  char count[24];
  snprintf(count, sizeof(count), "%u AIRCRAFT", static_cast<unsigned>(
      g_live ? g_live_snapshot.aircraft_count : g_aircraft_count));
  text(count, 445, 37, TFT_WHITE, 2, middle_left);
  text("MSG RATE", 660, 37, kMuted, 2, middle_left);
  char rate[20];
  snprintf(rate, sizeof(rate), "%.1f/s", g_live ? g_live_snapshot.message_rate : 58.7f);
  text(rate, 785, 37, kGreen, 2, middle_left);
  button(g_live ? "LIVE" : "DEMO", 1045, 14, 100, 44,
         g_live ? TFT_DARKGREEN : TFT_MAROON);
  char range[24];
  snprintf(range, sizeof(range), "%u NM", g_settings.radar_range_nm);
  text(range, 1195, 37, TFT_LIGHTGREY, 2, middle_right);
}

void draw_header_live_values() {
  M5.Display.fillRect(440, 12, 175, 48, kBg);
  M5.Display.fillRect(780, 12, 130, 48, kBg);
  char value[24];
  snprintf(value, sizeof(value), "%u AIRCRAFT", static_cast<unsigned>(
      g_live ? g_live_snapshot.aircraft_count : g_aircraft_count));
  text(value, 445, 37, TFT_WHITE, 2, middle_left);
  snprintf(value, sizeof(value), "%.1f/s", g_live ? g_live_snapshot.message_rate : 58.7f);
  text(value, 785, 37, kGreen, 2, middle_left);
  button(g_live ? "LIVE" : "DEMO", 1045, 14, 100, 44,
         g_live ? TFT_DARKGREEN : TFT_MAROON);
}

void draw_tabs() {
  static constexpr const char* labels[] = {"RADAR", "LIST", "TARGET", "STATS", "SETTINGS"};
  M5.Display.fillRect(0, kTabsY, 1280, 74, kBg);
  M5.Display.drawFastHLine(0, kTabsY, 1280, kBorder);
  for (int i = 0; i < 5; ++i) {
    if (static_cast<int>(g_view) == i) M5.Display.fillRect(i * kTabW, kTabsY + 1, kTabW, 73, 0x08a4);
    text(labels[i], i * kTabW + kTabW / 2, kTabsY + 39,
         static_cast<int>(g_view) == i ? kBlue : TFT_LIGHTGREY, 2);
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
  text(a.callsign, x + 28, y + 45, kGreen, 4, middle_left);
  button(a.stale ? "STALE" : (g_locked ? "LOCKED" : "LOCK"),
         x + w - 122, y + 20, 96, 42,
         a.stale ? TFT_MAROON : (g_locked ? TFT_DARKGREEN : TFT_DARKGREY));
  plane(x + 55, y + 115, 26, TFT_WHITE);
  char identity[32];
  snprintf(identity, sizeof(identity), "%s | %06lX",
           a.registration[0] ? a.registration : "--",
           static_cast<unsigned long>(a.icao));
  text(identity, x + 105, y + 90, TFT_WHITE, 2, middle_left);
  text(a.type, x + 105, y + 116, TFT_LIGHTGREY, 2, middle_left);
  text(a.op, x + 105, y + 140, TFT_LIGHTGREY, 2, middle_left);
  M5.Display.drawFastHLine(x + 24, y + 158, w - 48, kBorder);
  char value[32];
  text("ALTITUDE", x + 28, y + 185, kBlue, 2, middle_left);
  if (a.has_altitude) snprintf(value, sizeof(value), "%d ft", a.altitude_ft);
  else strlcpy(value, "--", sizeof(value));
  text(value, x + 28, y + 221, TFT_WHITE, 3, middle_left);
  text("SPEED", x + w / 2 + 12, y + 185, kBlue, 2, middle_left);
  if (a.has_speed) snprintf(value, sizeof(value), "%d kts", a.speed_kts);
  else strlcpy(value, "--", sizeof(value));
  text(value, x + w / 2 + 12, y + 221, TFT_WHITE, 3, middle_left);
  text("RANGE / BEARING", x + 28, y + 270, kBlue, 2, middle_left);
  if (a.has_position && g_settings.location_configured) {
    snprintf(value, sizeof(value), "%.0f NM   %03d deg", a.range_nm, a.bearing_deg);
  } else {
    strlcpy(value, "--", sizeof(value));
  }
  text(value, x + 28, y + 307, TFT_WHITE, 3, middle_left);
}

void draw_radar() {
  const int cx = 345, cy = 354, radius = 245;
  card(18, 92, 675, 532);
  for (int ring = 1; ring <= 4; ++ring) M5.Display.drawCircle(cx, cy, radius * ring / 4, 0x2382);
  M5.Display.drawFastHLine(cx - radius, cy, radius * 2, 0x2382);
  M5.Display.drawFastVLine(cx, cy - radius, radius * 2, 0x2382);
  text("N", cx, cy - radius - 17, TFT_WHITE, 2);
  text("S", cx, cy + radius + 17, TFT_WHITE, 2);
  text("W", cx - radius - 20, cy, TFT_WHITE, 2);
  text("E", cx + radius + 20, cy, TFT_WHITE, 2);
  plane(cx, cy, 22, kBlue);
  for (size_t i = 0; i < g_aircraft_count; ++i) {
    if (!g_aircraft[i].has_position || !g_settings.location_configured) continue;
    const float distance = fminf(g_aircraft[i].range_nm / g_settings.radar_range_nm, 1.0f);
    const float angle = (g_aircraft[i].bearing_deg - 90.0f) * DEG_TO_RAD;
    const int px = cx + static_cast<int>(cosf(angle) * distance * (radius - 18));
    const int py = cy + static_cast<int>(sinf(angle) * distance * (radius - 18));
    plane(px, py, i == g_selected ? 16 : 12, i == g_selected ? kBlue : kGreen);
    if (i == g_selected) M5.Display.drawRect(px - 25, py - 25, 50, 50, kGreen);
  }
  draw_selected_summary(720, 112, 542, 472);
  if (!g_settings.location_configured) {
    button("SET RECEIVER LOCATION FOR RANGE / BEARING", 160, 560, 500, 42, TFT_MAROON);
  }
}

void draw_list() {
  card(18, 92, 820, 532);
  text("AIRCRAFT", 54, 122, kBlue, 2, middle_left);
  text("ALTITUDE", 315, 122, kBlue, 2, middle_left);
  text("SPEED", 500, 122, kBlue, 2, middle_left);
  text("RANGE", 650, 122, kBlue, 2, middle_left);
  for (size_t i = 0; i < g_aircraft_count; ++i) {
    const int y = 150 + static_cast<int>(i) * 72;
    if (i == g_selected) M5.Display.fillRoundRect(30, y, 795, 62, 8, TFT_NAVY);
    plane(58, y + 31, 10, kGreen);
    text(g_aircraft[i].callsign, 86, y + 20, kGreen, 2, middle_left);
    char identity[32];
    snprintf(identity, sizeof(identity), "%s | %06lX",
             g_aircraft[i].registration[0] ? g_aircraft[i].registration : "--",
             static_cast<unsigned long>(g_aircraft[i].icao));
    text(identity, 86, y + 45, TFT_LIGHTGREY, 2, middle_left);
    char value[24];
    if (g_aircraft[i].has_altitude) snprintf(value, sizeof(value), "%d ft", g_aircraft[i].altitude_ft);
    else strlcpy(value, "--", sizeof(value));
    text(value, 315, y + 31, TFT_WHITE, 2, middle_left);
    if (g_aircraft[i].has_speed) snprintf(value, sizeof(value), "%d kts", g_aircraft[i].speed_kts);
    else strlcpy(value, "--", sizeof(value));
    text(value, 500, y + 31, TFT_WHITE, 2, middle_left);
    if (g_aircraft[i].has_position && g_settings.location_configured)
      snprintf(value, sizeof(value), "%.0f NM", g_aircraft[i].range_nm);
    else
      strlcpy(value, "--", sizeof(value));
    text(value, 650, y + 31, TFT_WHITE, 2, middle_left);
  }
  draw_selected_summary(860, 112, 402, 472);
}

void draw_target() {
  if (g_aircraft_count == 0) {
    card(18, 92, 1244, 532);
    text("SEARCHING FOR AIRCRAFT", 640, 340, kBlue, 4);
    return;
  }
  const DisplayAircraft& a = g_aircraft[g_selected];
  card(18, 92, 1244, 532);
  text(a.callsign, 48, 145, kGreen, 5, middle_left);
  button(a.stale ? "STALE" : (g_locked ? "LOCKED" : "LOCK"), 305, 112, 110, 46,
         a.stale ? TFT_MAROON : (g_locked ? TFT_DARKGREEN : TFT_DARKGREY));
  text(a.op, 48, 185, TFT_WHITE, 2, middle_left);
  char identity[64];
  snprintf(identity, sizeof(identity), "%s | %06lX",
           a.registration[0] ? a.registration : "--",
           static_cast<unsigned long>(a.icao));
  text(identity, 48, 215, TFT_LIGHTGREY, 2, middle_left);
  text(a.type, 48, 245, TFT_LIGHTGREY, 2, middle_left);
  plane(380, 330, 95, TFT_LIGHTGREY);
  card(610, 118, 620, 455);
  const char* labels[] = {"ALTITUDE", "SPEED", "HEADING", "VERT RATE",
                          "RANGE", "BEARING", "LATITUDE", "LONGITUDE"};
  char values[8][32];
  if (a.has_altitude) snprintf(values[0], sizeof(values[0]), "%d ft", a.altitude_ft);
  else strlcpy(values[0], "--", sizeof(values[0]));
  if (a.has_speed) snprintf(values[1], sizeof(values[1]), "%d kts", a.speed_kts);
  else strlcpy(values[1], "--", sizeof(values[1]));
  if (a.has_heading) snprintf(values[2], sizeof(values[2]), "%03d deg", a.heading_deg);
  else strlcpy(values[2], "--", sizeof(values[2]));
  if (a.has_vertical_rate)
    snprintf(values[3], sizeof(values[3]), "%+d ft/min", a.vertical_rate_fpm);
  else
    strlcpy(values[3], "--", sizeof(values[3]));
  if (a.has_position && g_settings.location_configured) {
    snprintf(values[4], sizeof(values[4]), "%.0f NM", a.range_nm);
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
  for (int i = 0; i < 8; ++i) {
    const int col = i % 4, row = i / 4;
    const int x = 635 + col * 145, y = 155 + row * 190;
    text(labels[i], x, y, kBlue, 2, middle_left);
    text(values[i], x, y + 48, TFT_WHITE, 2, middle_left);
  }
}

void draw_stats() {
  char value[32];
  card(18, 92, 392, 270);
  text("SIGNAL STRENGTH", 42, 124, kBlue, 2, middle_left);
  snprintf(value, sizeof(value), "%.1f dBFS",
           g_live ? g_live_snapshot.strongest_signal_dbfs : -48.0f);
  text(value, 42, 174, TFT_WHITE, 4, middle_left);
  for (int i = 0; i < 10; ++i) M5.Display.fillRect(48 + i * 28, 290 - i * 10, 20, 25 + i * 10,
                                                   i < 7 ? kGreen : TFT_DARKGREY);
  card(428, 92, 402, 270);
  text("MESSAGE RATE", 452, 124, kBlue, 2, middle_left);
  snprintf(value, sizeof(value), "%.1f msg/sec",
           g_live ? g_live_snapshot.message_rate : 58.7f);
  text(value, 452, 174, TFT_WHITE, 3, middle_left);
  int py = 300;
  for (int x = 458; x < 805; x += 28) {
    const int ny = 250 + ((x / 28) % 4) * 12;
    M5.Display.drawLine(x - 28, py, x, ny, kBlue);
    py = ny;
  }
  card(848, 92, 414, 270);
  text("MODE-S ACTIVITY", 872, 124, kBlue, 2, middle_left);
  for (int i = 0; i < 8; ++i) {
    const int x = 880 + i * 43;
    M5.Display.drawRect(x, 185, 20, i % 3 == 0 ? 90 : 55, kBlue);
  }
  char aircraft[12], messages[20], strongest[20];
  snprintf(aircraft, sizeof(aircraft), "%u", static_cast<unsigned>(
      g_live ? g_live_snapshot.aircraft_count : g_aircraft_count));
  snprintf(messages, sizeof(messages), "%lu", static_cast<unsigned long>(
      g_live ? g_live_snapshot.total_messages : 15892));
  snprintf(strongest, sizeof(strongest), "%.1f dBFS",
           g_live ? g_live_snapshot.strongest_signal_dbfs : -45.0f);
  struct Metric { const char* label; const char* value; uint16_t color; };
  const Metric metrics[] = {{"AIRCRAFT", aircraft, TFT_WHITE},
                            {"MESSAGES", messages, TFT_WHITE},
                            {"STRONGEST", strongest, kGreen},
                            {"CPU", "N/A", TFT_LIGHTGREY}, {"GAIN", "AUTO", kGreen}};
  for (int i = 0; i < 5; ++i) {
    const int x = 18 + i * 250;
    card(x, 382, 230, 220);
    text(metrics[i].label, x + 18, 420, kBlue, 2, middle_left);
    text(metrics[i].value, x + 115, 510, metrics[i].color, 3);
  }
}

void start_edit(EditField field) {
  g_edit = field;
  const int32_t e7 = field == EditField::latitude ? g_settings.latitude_e7
                                                  : g_settings.longitude_e7;
  snprintf(g_entry, sizeof(g_entry), "%.7f", e7 / 10000000.0);
}

void draw_keypad() {
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
  card(18, 92, 620, 532);
  text("ADS-B SETTINGS", 48, 130, kBlue, 3, middle_left);
  text("RECEIVER LATITUDE", 48, 190, kMuted, 2, middle_left);
  text("RECEIVER LONGITUDE", 48, 285, kMuted, 2, middle_left);
  char value[40];
  if (g_latitude_set)
    snprintf(value, sizeof(value), "%.7f", g_settings.latitude_e7 / 10000000.0);
  else
    strlcpy(value, "NOT SET", sizeof(value));
  button(value, 300, 165, 300, 58, TFT_NAVY);
  if (g_longitude_set)
    snprintf(value, sizeof(value), "%.7f", g_settings.longitude_e7 / 10000000.0);
  else
    strlcpy(value, "NOT SET", sizeof(value));
  button(value, 300, 260, 300, 58, TFT_NAVY);
  text("RADAR RANGE", 48, 380, kMuted, 2, middle_left);
  snprintf(value, sizeof(value), "%u NM", g_settings.radar_range_nm);
  button(value, 300, 355, 300, 58, TFT_DARKCYAN);
  text("RF GAIN", 48, 475, kMuted, 2, middle_left);
  button("AUTO  (READ ONLY)", 300, 450, 300, 58, TFT_DARKGREY);
  button("EXIT ADS-B", 300, 545, 300, 58, TFT_MAROON);
  if (g_edit != EditField::none) draw_keypad();
  else {
    card(690, 102, 570, 510);
    text(g_live ? "LIVE RECEIVER" : "DEMO MODE", 975, 190,
         g_live ? kGreen : TFT_ORANGE, 4);
    text(g_live ? "1090 MHz Mode-S capture active" : "Waiting for a CRC-valid live frame",
         975, 260, TFT_LIGHTGREY, 2);
    text("Gain is automatic and read only.", 975, 380, TFT_LIGHTGREY, 2);
  }
}

void redraw_content() {
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
  M5.Display.fillScreen(kBg);
  draw_header();
  redraw_content();
  draw_tabs();
}

Action handle_keypad(int32_t x, int32_t y) {
  if (hit(x, y, 730, 508, 230, 58)) {
    g_edit = EditField::none;
    redraw();
    return Action::none;
  }
  if (hit(x, y, 990, 508, 230, 58)) {
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
    const int col = i % 4, row = i / 4;
    if (!hit(x, y, 730 + col * 122, 245 + row * 64, 112, 54)) continue;
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
  if (g_live) {
    apply_live_snapshot();
    g_drawn_revision = g_live_snapshot.revision;
  } else {
    std::copy(std::begin(kDemoAircraft), std::end(kDemoAircraft), g_aircraft);
    g_aircraft_count = std::size(kDemoAircraft);
  }
  redraw();
}

void draw() { if (g_active) redraw(); }

void update() {
  if (!g_active || !g_live || g_drawn_revision == g_live_snapshot.revision) return;
  static uint32_t last_draw_ms = 0;
  if (millis() - last_draw_ms < 1000) return;
  last_draw_ms = millis();
  apply_live_snapshot();
  g_drawn_revision = g_live_snapshot.revision;
  draw_header_live_values();
  // Live repaint deliberately skips redraw_content()'s full black clear.
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
  g_live_snapshot = snapshot;
  if (snapshot.visible_count) g_live = true;
}

Action handle_touch(int32_t x, int32_t y) {
  if (!g_active) return Action::none;
  if (g_edit != EditField::none) return handle_keypad(x, y);
  if (y >= kTabsY) {
    const View requested = static_cast<View>(constrain(x / kTabW, 0, 4));
    if (requested == View::settings) return Action::open_global_settings;
    g_view = requested;
    redraw();
    return Action::none;
  }
  if (g_view == View::radar) {
    const int cx = 345, cy = 354, radius = 245;
    for (size_t i = 0; i < g_aircraft_count; ++i) {
      if (!g_aircraft[i].has_position || !g_settings.location_configured) continue;
      const float distance = fminf(g_aircraft[i].range_nm / g_settings.radar_range_nm, 1.0f);
      const float angle = (g_aircraft[i].bearing_deg - 90.0f) * DEG_TO_RAD;
      const int px = cx + static_cast<int>(cosf(angle) * distance * (radius - 18));
      const int py = cy + static_cast<int>(sinf(angle) * distance * (radius - 18));
      const int dx = x - px, dy = y - py;
      if (dx * dx + dy * dy <= 32 * 32) {
        g_selected = i;
        redraw_content();
        return Action::none;
      }
    }
    if (hit(x, y, 720 + 542 - 122, 132, 96, 42)) {
      toggle_lock();
      redraw_content();
    }
  } else if (g_view == View::list) {
    for (size_t i = 0; i < g_aircraft_count; ++i) {
      if (hit(x, y, 30, 150 + static_cast<int>(i) * 72, 795, 62)) {
        g_selected = i;
        redraw_content();
        return Action::none;
      }
    }
    if (hit(x, y, 860 + 402 - 122, 132, 96, 42)) {
      toggle_lock();
      redraw_content();
    }
  } else if (g_view == View::target) {
    if (hit(x, y, 305, 112, 110, 46)) {
      toggle_lock();
      redraw_content();
    }
  } else if (g_view == View::settings) {
    if (hit(x, y, 300, 165, 300, 58)) { start_edit(EditField::latitude); redraw(); }
    else if (hit(x, y, 300, 260, 300, 58)) { start_edit(EditField::longitude); redraw(); }
    else if (hit(x, y, 300, 355, 300, 58)) {
      size_t index = 0;
      while (index < std::size(kRanges) && kRanges[index] != g_settings.radar_range_nm) ++index;
      g_settings.radar_range_nm = kRanges[(index + 1) % std::size(kRanges)];
      redraw();
      return Action::settings_changed;
    } else if (hit(x, y, 300, 545, 300, 58)) {
      g_active = false;
      return Action::exit;
    }
  }
  return Action::none;
}

const Settings& settings() { return g_settings; }
bool active() { return g_active; }

bool self_check() {
  for (size_t i = 0; i < std::size(kDemoAircraft); ++i) {
    if (kDemoAircraft[i].icao == 0 || kDemoAircraft[i].callsign[0] == '\0') return false;
    for (size_t j = i + 1; j < std::size(kDemoAircraft); ++j)
      if (kDemoAircraft[i].icao == kDemoAircraft[j].icao) return false;
  }
  DisplayAircraft saved_aircraft[kVisibleAircraft];
  std::copy(std::begin(g_aircraft), std::end(g_aircraft), saved_aircraft);
  const size_t saved_count = g_aircraft_count;
  const Snapshot saved_snapshot = g_live_snapshot;
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
  g_selected = saved_selected;
  g_locked = saved_locked;
  g_live = saved_live;

  return model_ok && kDemoAircraft[2].icao == 0xA7B8C9 &&
         keep_stale_selection(true, true) && !keep_stale_selection(false, true) &&
         keep_stale_selection(false, false) &&
         valid_coordinate(EditField::latitude, -90.0) &&
         valid_coordinate(EditField::latitude, 90.0) &&
         !valid_coordinate(EditField::latitude, 90.01) &&
         valid_coordinate(EditField::longitude, -180.0) &&
         !valid_coordinate(EditField::longitude, 180.01);
}

}  // namespace orcsdr::adsb
