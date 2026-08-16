#include "lora_dashboard.hpp"

#include "dashboard_audio_control.hpp"

#include <M5Unified.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace orcsdr::lora {
namespace {

extern const uint8_t orc_badge_start[] asm("_binary_orc_badge_104_png_start");
extern const uint8_t orc_badge_end[] asm("_binary_orc_badge_104_png_end");

constexpr uint16_t kBg = TFT_BLACK;
constexpr uint16_t kPanel = 0x0841;
constexpr uint16_t kCyan = 0x2e7f;
constexpr uint16_t kGreen = 0x6fe8;
constexpr uint16_t kYellow = 0xff24;
constexpr uint16_t kMuted = 0x8c71;
constexpr uint16_t kGrid = 0x2945;
constexpr int kHeaderH = 120;
constexpr int kTabsY = 640;
constexpr int kTabW = 256;
constexpr int kPlotX = 44;
constexpr int kPlotY = 270;
constexpr int kPlotW = 780;
constexpr int kPlotH = 124;
constexpr int kWaterfallY = 402;
constexpr int kWaterfallH = 126;

Snapshot g_snapshot{};
View g_view = View::overview;
bool g_active = false;
bool g_follow_node = false;
uint8_t g_filter = 0;
uint32_t g_last_dynamic_ms = 0;
uint16_t g_waterfall_row[kPlotW]{};

bool hit(int32_t x, int32_t y, int bx, int by, int bw, int bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

void text(const char* value, int x, int y, uint16_t color = TFT_WHITE, int size = 2,
          textdatum_t datum = middle_center) {
  M5.Display.setTextDatum(datum);
  M5.Display.setTextSize(size);
  M5.Display.setTextColor(color, kBg);
  M5.Display.drawString(value, x, y);
}

void card(int x, int y, int w, int h, uint16_t border = kCyan) {
  M5.Display.fillRoundRect(x, y, w, h, 10, kPanel);
  M5.Display.drawRoundRect(x, y, w, h, 10, border);
}

void button(int x, int y, int w, int h, const char* title, uint16_t color = kCyan,
            bool selected = false) {
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

void draw_header() {
  M5.Display.fillRect(0, 0, 1280, kHeaderH, kBg);
  M5.Display.drawFastHLine(8, kHeaderH - 1, 1264, kCyan);
  const size_t badge_size = static_cast<size_t>(orc_badge_end - orc_badge_start);
  if (!M5.Display.drawPng(orc_badge_start, badge_size, 18, 8))
    M5.Display.drawRoundRect(18, 8, 104, 104, 18, kGreen);
  text("OrcSDR", 142, 38, TFT_WHITE, 4, middle_left);
  text("M5STACK TAB5", 142, 81, kCyan, 2, middle_left);
  M5.Display.drawFastVLine(362, 20, 80, kCyan);
  draw_radio_icon(458, 64, kGreen);
  text("LORA MESH", 520, 47, TFT_WHITE, 4, middle_left);
  text("Meshtastic RX Monitor", 520, 83, kCyan, 2, middle_left);
  M5.Display.drawFastVLine(852, 20, 80, kCyan);
  text(g_snapshot.wifi_connected ? "WI-FI ONLINE" : "WI-FI OFFLINE", 885, 44,
       g_snapshot.wifi_connected ? kGreen : kMuted, 1, middle_left);
  text(g_snapshot.running ? "RX ONLY" : "RX STOPPED", 885, 73,
       g_snapshot.running ? kGreen : kMuted, 2, middle_left);
  text(g_snapshot.native_decoder_ready ? "DECODE" : "PHY PENDING", 1050, 44,
       g_snapshot.native_decoder_ready ? kGreen : kYellow, 1, middle_left);
  text(g_snapshot.key_loaded ? "KEY LOADED" : "PUBLIC ONLY", 1050, 73,
       g_snapshot.key_loaded ? kGreen : kMuted, 1, middle_left);
  audio_header::draw_home_button();
  audio_header::draw_settings_button();
}

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
  } else {
    const int16_t xs[] = {-27, -18, -10, -2, 5, 12, 20, 28};
    const int16_t ys[] = {0, 0, -19, 19, -24, 13, 0, 0};
    for (int i = 1; i < 8; ++i)
      M5.Display.drawLine(cx + xs[i - 1], cy + ys[i - 1], cx + xs[i], cy + ys[i], color);
  }
}

void draw_tabs() {
  static constexpr const char* names[] = {"OVERVIEW", "NODES", "TRAFFIC", "MAP", "RF HEALTH"};
  for (uint8_t i = 0; i < static_cast<uint8_t>(View::count); ++i) {
    const int x = i * kTabW;
    const bool selected = i == static_cast<uint8_t>(g_view);
    M5.Display.fillRect(x, kTabsY, kTabW, 80, selected ? 0x0a43 : kPanel);
    M5.Display.drawRect(x, kTabsY, kTabW, 80, selected ? kGreen : kGrid);
    draw_tab_icon(static_cast<View>(i), x + 50, kTabsY + 38,
                  selected ? (i == 0 ? kGreen : kCyan) : TFT_WHITE);
    text(names[i], x + 91, kTabsY + 40, selected ? (i == 0 ? kGreen : kCyan) : TFT_WHITE,
         2, middle_left);
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

void draw_metric(int x, int y, int w, const char* title, const char* value,
                 uint16_t color = TFT_WHITE) {
  card(x, y, w, 88);
  text(title, x + 16, y + 20, kCyan, 1, middle_left);
  text(value, x + 16, y + 56, color, 3, middle_left);
}

void draw_plot_static() {
  card(24, 138, 1128, 430);
  text("PASSIVE LORA MONITOR", 60, 164, kGreen, 3, middle_left);
  char value[64];
  snprintf(value, sizeof(value), "CENTER %.3f MHz", g_snapshot.frequency_hz / 1000000.0);
  text(value, 60, 198, kCyan, 2, middle_left);
  snprintf(value, sizeof(value), "SF%u  BW %luk", g_snapshot.sf,
           static_cast<unsigned long>(g_snapshot.bandwidth_hz / 1000u));
  text(value, 520, 198, kGreen, 2, middle_left);
  text("RX ONLY", 1020, 164, kGreen, 2, middle_left);
  M5.Display.fillRect(kPlotX, kPlotY, kPlotW, kPlotH, kBg);
  M5.Display.drawRect(kPlotX, kPlotY, kPlotW, kPlotH, kGrid);
  for (int i = 1; i < 5; ++i) {
    M5.Display.drawFastHLine(kPlotX, kPlotY + i * kPlotH / 5, kPlotW, kGrid);
    M5.Display.drawFastVLine(kPlotX + i * kPlotW / 5, kPlotY, kPlotH, kGrid);
  }
  M5.Display.drawRect(kPlotX, kWaterfallY, kPlotW, kWaterfallH, kCyan);
  M5.Display.setScrollRect(kPlotX + 1, kWaterfallY + 1, kPlotW - 2, kWaterfallH - 2,
                           kBg);
  card(846, 270, 280, 258);
  text("RECENT", 868, 294, kCyan, 2, middle_left);
  button(34, 578, 250, 48, "SCAN BAND", kCyan, g_snapshot.survey_active);
  button(300, 578, 250, 48, "RECORD IQ", kCyan);
  button(566, 578, 250, 48, "CHANNELS", kCyan);
  button(832, 578, 294, 48, g_snapshot.sd_logging ? "SD LOG ON" : "SD LOG OFF",
         g_snapshot.sd_logging ? kGreen : kCyan, g_snapshot.sd_logging);
}

void draw_overview_static() { draw_plot_static(); }

void draw_nodes_static() {
  text("RECENTLY SEEN NODES", 40, 154, kCyan, 2, middle_left);
  card(32, 175, 770, 414);
  card(822, 175, 426, 190);
  text("SELECTED NODE", 844, 199, kCyan, 2, middle_left);
  card(822, 382, 426, 207);
  text("VERIFIED LINKS", 844, 406, kCyan, 2, middle_left);
  button(32, 604, 228, 42, "FILTER", kCyan);
  button(278, 604, 228, 42, "FAVORITE", kCyan);
  button(524, 604, 228, 42, "VIEW DETAILS", kCyan);
  button(770, 604, 228, 42, "EXPORT LOG", kCyan);
}

void draw_traffic_static() {
  card(28, 144, 335, 464);
  text("SOURCES", 52, 170, kGreen, 2, middle_left);
  card(386, 144, 862, 464);
  text("DECODED TRAFFIC", 412, 170, kGreen, 2, middle_left);
  button(386, 616, 202, 42, "VIEW RAW", kCyan);
  button(604, 616, 202, 42, "SAVE LOG", kCyan);
  button(822, 616, 202, 42, "FILTER TYPE", kCyan);
  button(1040, 616, 202, 42, "CLEAR EVENTS", kCyan);
}

void draw_map_static() {
  card(24, 138, 896, 452);
  text("TOPOLOGY MAP", 48, 165, kCyan, 2, middle_left);
  for (int i = 1; i < 8; ++i) {
    M5.Display.drawFastHLine(48, 183 + i * 47, 846, kGrid);
    M5.Display.drawFastVLine(48 + i * 106, 183, 330, kGrid);
  }
  card(942, 138, 306, 452);
  text("SELECTED NODE", 966, 165, kCyan, 2, middle_left);
  button(26, 604, 210, 42, "CENTER MAP", kCyan);
  button(252, 604, 210, 42, "FOLLOW NODE", kCyan, g_follow_node);
  button(478, 604, 210, 42, "MARK POINT", kCyan);
  button(704, 604, 210, 42, "SAVE SNAPSHOT", kCyan);
}

void draw_health_static() {
  const char* titles[] = {"FREQUENCY", "REGION", "MONITOR", "ENCRYPTED", "NODES", "MODE"};
  const int widths[] = {230, 220, 240, 230, 180, 170};
  int x = 24;
  for (int i = 0; i < 6; ++i) {
    card(x, 138, widths[i], 92);
    text(titles[i], x + widths[i] / 2, 161, kGreen, 1);
    x += widths[i] + 8;
  }
  card(24, 248, 810, 310);
  text("SPECTRUM", 48, 273, kGreen, 2, middle_left);
  card(854, 248, 394, 310);
  text("RECENT EVENTS", 878, 273, kGreen, 2, middle_left);
  button(24, 578, 242, 48, "SCAN BAND", kCyan);
  button(284, 578, 242, 48, "RECORD IQ", kCyan);
  button(544, 578, 242, 48, "EXPORT LOG", kCyan);
  button(804, 578, 242, 48, "CLEAR EVENTS", kCyan);
}

void draw_event_row(const Event& event, int x, int y, int w, bool detailed) {
  M5.Display.fillRoundRect(x, y, w, detailed ? 66 : 58, 8, kPanel);
  M5.Display.drawRoundRect(x, y, w, detailed ? 66 : 58, 8, event.verified ? kGrid : kYellow);
  char sender[16];
  format_id(sender, sizeof(sender), event.sender);
  text(sender, x + 16, y + 19, event.verified ? kGreen : kYellow, 2, middle_left);
  text(event.text[0] ? event.text : (event.encrypted ? "ENCRYPTED FRAME" : "WAITING"),
       x + 16, y + (detailed ? 45 : 40), TFT_WHITE, detailed ? 2 : 1, middle_left);
  char age[20];
  const uint32_t seconds = event.sender == 0 ? 0 : (millis() - event.received_ms) / 1000u;
  snprintf(age, sizeof(age), "%lus", static_cast<unsigned long>(seconds));
  text(age, x + w - 16, y + 19, kMuted, 1, middle_right);
}

void draw_overview_dynamic() {
  M5.Display.fillRect(854, 310, 260, 205, kPanel);
  for (size_t i = 0; i < 3 && i < g_snapshot.event_count; ++i)
    draw_event_row(g_snapshot.events[i], 862, 318 + static_cast<int>(i) * 62, 244, false);
  if (g_snapshot.event_count == 0)
    text("WAITING FOR VERIFIED TRAFFIC", 980, 410, kMuted, 1);
}

void draw_nodes_dynamic() {
  M5.Display.fillRect(42, 195, 748, 380, kPanel);
  text("NAME", 62, 210, kCyan, 1, middle_left);
  text("HOPS", 380, 210, kCyan, 1, middle_left);
  text("BATTERY", 480, 210, kCyan, 1, middle_left);
  text("LAST HEARD", 600, 210, kCyan, 1, middle_left);
  text("RSSI / SNR", 700, 210, kCyan, 1, middle_left);
  for (size_t i = 0; i < g_snapshot.node_count && i < 6; ++i) {
    const Node& node = g_snapshot.nodes[i];
    const int y = 236 + static_cast<int>(i) * 54;
    const bool selected = i == g_snapshot.selected_node;
    M5.Display.fillRoundRect(48, y, 736, 46, 7, selected ? 0x1264 : kBg);
    M5.Display.drawRoundRect(48, y, 736, 46, 7, selected ? kGreen : kGrid);
    char value[48];
    node_name(node, value, sizeof(value));
    text(value, 70, y + 23, selected ? kGreen : TFT_WHITE, 2, middle_left);
    snprintf(value, sizeof(value), "%s", node.hops == UINT8_MAX ? "—" : "HOPS");
    if (node.hops != UINT8_MAX) snprintf(value, sizeof(value), "%u", node.hops);
    text(value, 380, y + 23, TFT_WHITE, 2, middle_left);
    if (node.battery_percent == UINT8_MAX) strlcpy(value, "—", sizeof(value));
    else snprintf(value, sizeof(value), "%u%%", node.battery_percent);
    text(value, 480, y + 23, node.battery_percent == UINT8_MAX ? kMuted : kGreen, 2, middle_left);
    snprintf(value, sizeof(value), "%lus", static_cast<unsigned long>(
        node.id == 0 ? 0 : (millis() - node.seen_ms) / 1000u));
    text(value, 600, y + 23, kMuted, 1, middle_left);
    if (node.signal_tenths == INT16_MAX) strlcpy(value, "—", sizeof(value));
    else snprintf(value, sizeof(value), "%.0f / %.1f", node.signal_tenths / 10.0,
                  node.snr_tenths == INT16_MAX ? 0.0 : node.snr_tenths / 10.0);
    text(value, 700, y + 23, TFT_WHITE, 1, middle_left);
  }
  if (g_snapshot.node_count == 0) text("NO VERIFIED NODES", 416, 400, kMuted, 2);
  M5.Display.fillRect(842, 218, 388, 132, kPanel);
  const Node* node = g_snapshot.node_count ? &g_snapshot.nodes[
      std::min<size_t>(g_snapshot.selected_node, g_snapshot.node_count - 1)] : nullptr;
  char value[64];
  if (node) node_name(*node, value, sizeof(value));
  else strlcpy(value, "—", sizeof(value));
  text(value, 854, 236, node ? kGreen : kMuted, 3, middle_left);
  if (node) {
    format_id(value, sizeof(value), node->id); text(value, 854, 268, TFT_WHITE, 1, middle_left);
    format_coord(value, sizeof(value), node->latitude_e7); text(value, 854, 302, kCyan, 1, middle_left);
  }
  M5.Display.fillRect(842, 426, 388, 146, kPanel);
  text("NO INFERRED LINKS", 1036, 486, kMuted, 2);
  text("Links appear only when verified", 1036, 518, kMuted, 1);
}

void draw_traffic_dynamic() {
  M5.Display.fillRect(42, 192, 307, 400, kPanel);
  M5.Display.fillRect(400, 192, 834, 400, kPanel);
  for (size_t i = 0; i < std::min<size_t>(5, g_snapshot.event_count); ++i) {
    const Event& event = g_snapshot.events[i];
    draw_event_row(event, 408, 204 + static_cast<int>(i) * 72, 814, true);
    char sender[16]; format_id(sender, sizeof(sender), event.sender);
    text(sender, 62, 215 + static_cast<int>(i) * 72, event.verified ? kGreen : kYellow,
         2, middle_left);
    text(event.text[0] ? event.text : (event.encrypted ? "ENCRYPTED" : "—"),
         62, 241 + static_cast<int>(i) * 72, TFT_WHITE, 1, middle_left);
  }
  if (g_snapshot.event_count == 0) text("WAITING FOR DECODED TRAFFIC", 820, 390, kMuted, 2);
  char value[48];
  snprintf(value, sizeof(value), "FILTER: %s", g_filter == 0 ? "ALL" : "SUPPORTED");
  text(value, 1150, 170, kCyan, 1, middle_right);
}

void draw_map_dynamic() {
  M5.Display.fillRect(50, 182, 842, 330, kBg);
  for (int i = 1; i < 8; ++i) {
    M5.Display.drawFastHLine(48, 183 + i * 47, 846, kGrid);
    M5.Display.drawFastVLine(48 + i * 106, 183, 330, kGrid);
  }
  const Node* center = g_snapshot.node_count ? &g_snapshot.nodes[
      std::min<size_t>(g_snapshot.selected_node, g_snapshot.node_count - 1)] : nullptr;
  if (!center || center->latitude_e7 == INT32_MAX || center->longitude_e7 == INT32_MAX) {
    text("WAITING FOR VERIFIED POSITION", 470, 350, kMuted, 2);
  } else {
    const float lat = center->latitude_e7 / 10000000.0f;
    const float lon = center->longitude_e7 / 10000000.0f;
    const float lon_scale = std::max(0.1f, cosf(lat * 0.01745329252f));
    for (size_t i = 0; i < g_snapshot.node_count; ++i) {
      const Node& node = g_snapshot.nodes[i];
      if (node.latitude_e7 == INT32_MAX || node.longitude_e7 == INT32_MAX) continue;
      const float east = (node.longitude_e7 / 10000000.0f - lon) * lon_scale;
      const float north = node.latitude_e7 / 10000000.0f - lat;
      const int x = 470 + static_cast<int>(east * 43000.0f);
      const int y = 350 - static_cast<int>(north * 61000.0f);
      if (!hit(x, y, 60, 193, 820, 310)) continue;
      const uint16_t color = i == g_snapshot.selected_node ? kGreen : kCyan;
      M5.Display.drawCircle(x, y, i == g_snapshot.selected_node ? 13 : 9, color);
      M5.Display.fillCircle(x, y, 3, color);
      char value[32]; node_name(node, value, sizeof(value));
      text(value, x + 14, y - 6, color, 1, middle_left);
    }
  }
  M5.Display.fillRect(960, 190, 270, 370, kPanel);
  char value[64];
  if (center) {
    node_name(*center, value, sizeof(value)); text(value, 970, 210, kGreen, 2, middle_left);
    format_coord(value, sizeof(value), center->latitude_e7); text(value, 970, 252, TFT_WHITE, 1, middle_left);
    format_coord(value, sizeof(value), center->longitude_e7); text(value, 970, 284, TFT_WHITE, 1, middle_left);
  } else text("NO NODE SELECTED", 1095, 320, kMuted, 2);
  text(g_follow_node ? "FOLLOWING" : "CENTERED", 970, 350,
       g_follow_node ? kGreen : kCyan, 2, middle_left);
  text("TOPOLOGY GRID", 970, 390, kMuted, 1, middle_left);
  text("No online map tiles", 970, 420, kMuted, 1, middle_left);
}

void draw_health_dynamic() {
  char value[48];
  snprintf(value, sizeof(value), "%.3f MHz", g_snapshot.frequency_hz / 1000000.0);
  text(value, 139, 196, TFT_WHITE, 2);
  text(g_snapshot.region[0] ? g_snapshot.region : "US 902-928", 358, 196, TFT_WHITE, 2);
  text(g_snapshot.running ? "PASSIVE RX" : "STOPPED", 596, 196,
       g_snapshot.running ? kGreen : kMuted, 2);
  snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(g_snapshot.encrypted_frames));
  text(value, 839, 196, g_snapshot.encrypted_frames ? kYellow : TFT_WHITE, 2);
  snprintf(value, sizeof(value), "%u", g_snapshot.node_count); text(value, 1052, 196, kGreen, 2);
  text("RX ONLY", 1200, 196, kGreen, 2);
  M5.Display.fillRect(40, 290, 780, 240, kPanel);
  M5.Display.fillRect(870, 290, 360, 240, kPanel);
  draw_event_row(g_snapshot.events[0], 880, 306, 340, true);
  draw_event_row(g_snapshot.events[1], 880, 382, 340, true);
  snprintf(value, sizeof(value), "RATE %.3f MSPS", g_snapshot.effective_sps / 1000000.0);
  text(value, 60, 548, kGreen, 1, middle_left);
  snprintf(value, sizeof(value), "USB %lu  DROP %lu  CRC %lu",
           static_cast<unsigned long>(g_snapshot.usb_overruns),
           static_cast<unsigned long>(g_snapshot.consumer_drops),
           static_cast<unsigned long>(g_snapshot.crc_ok));
  text(value, 420, 548, kCyan, 1, middle_left);
}

void draw_dynamic() {
  if (g_view == View::overview) draw_overview_dynamic();
  else if (g_view == View::nodes) draw_nodes_dynamic();
  else if (g_view == View::traffic) draw_traffic_dynamic();
  else if (g_view == View::map) draw_map_dynamic();
  else draw_health_dynamic();
}

void draw_static() {
  M5.Display.fillScreen(kBg);
  draw_header();
  if (g_view == View::overview) draw_overview_static();
  else if (g_view == View::nodes) draw_nodes_static();
  else if (g_view == View::traffic) draw_traffic_static();
  else if (g_view == View::map) draw_map_static();
  else draw_health_static();
  draw_tabs();
  draw_dynamic();
}

}  // namespace

void enter(const Snapshot& snapshot) {
  g_snapshot = snapshot;
  g_active = true;
  g_last_dynamic_ms = 0;
  draw_static();
}

void leave() {
  if (!g_active) return;
  M5.Display.clearScrollRect();
  g_active = false;
}

void draw() {
  if (!g_active) return;
  draw_static();
}

void update(const Snapshot& snapshot) {
  if (!g_active) return;
  g_snapshot = snapshot;
  const uint32_t now = millis();
  if (now - g_last_dynamic_ms < 200) return;
  g_last_dynamic_ms = now;
  draw_dynamic();
}

void draw_spectrum(const float* levels, size_t first_bin, size_t visible_bins, float floor) {
  if (!spectrum_active() || levels == nullptr || visible_bins < 2) return;
  const int x = g_view == View::rf_health ? 42 : kPlotX;
  const int y = g_view == View::rf_health ? 300 : kPlotY;
  const int w = g_view == View::rf_health ? 770 : kPlotW;
  const int h = g_view == View::rf_health ? 130 : kPlotH;
  M5.Display.fillRect(x + 1, y + 1, w - 2, h - 2, kBg);
  for (int i = 1; i < 5; ++i) {
    M5.Display.drawFastHLine(x, y + i * h / 5, w, kGrid);
    M5.Display.drawFastVLine(x + i * w / 5, y, h, kGrid);
  }
  int px = x;
  int py = y + h - 2;
  for (int i = 0; i < w; ++i) {
    const size_t source = first_bin + std::min<size_t>(visible_bins - 1,
        static_cast<size_t>(i) * visible_bins / static_cast<size_t>(w));
    const float normalized = std::clamp((levels[source] - floor) / 70.0f, 0.0f, 1.0f);
    const int next_y = y + h - 2 - static_cast<int>(normalized * (h - 4));
    M5.Display.drawLine(px, py, x + i, next_y, kGreen);
    px = x + i; py = next_y;
  }
  if (g_view == View::overview) {
    for (int i = 0; i < kPlotW; ++i) {
      const size_t source = first_bin + std::min<size_t>(visible_bins - 1,
          static_cast<size_t>(i) * visible_bins / kPlotW);
      const float level = std::clamp((levels[source] - floor) / 70.0f, 0.0f, 1.0f);
      const uint8_t r = static_cast<uint8_t>(std::clamp(level * 300.0f, 0.0f, 255.0f));
      const uint8_t g = static_cast<uint8_t>(std::clamp(level * 255.0f, 0.0f, 255.0f));
      const uint8_t b = static_cast<uint8_t>(std::clamp(180.0f - level * 180.0f, 0.0f, 255.0f));
      g_waterfall_row[i] = M5.Display.color565(r, g, b);
    }
    M5.Display.scroll(0, -1);
    M5.Display.pushImage(kPlotX, kWaterfallY + kWaterfallH - 2, kPlotW, 1, g_waterfall_row);
  }
}

Action handle_touch(int32_t x, int32_t y) {
  if (!g_active) return {};
  if (audio_header::home_hit(x, y)) return {ActionKind::exit_home};
  if (audio_header::settings_hit(x, y)) return {ActionKind::open_settings};
  if (hit(x, y, 0, kTabsY, 1280, 80))
    return {ActionKind::select_view, static_cast<uint32_t>(x / kTabW)};
  if (g_view == View::overview) {
    if (hit(x, y, 34, 578, 250, 48)) return {ActionKind::scan_toggle};
    if (hit(x, y, 300, 578, 250, 48)) return {ActionKind::record_iq_toggle};
    if (hit(x, y, 566, 578, 250, 48)) return {ActionKind::open_channels};
    if (hit(x, y, 832, 578, 294, 48)) return {ActionKind::logging_toggle};
  } else if (g_view == View::nodes) {
    if (hit(x, y, 48, 236, 736, 324)) return {ActionKind::select_node,
        static_cast<uint32_t>((y - 236) / 54)};
    if (hit(x, y, 32, 604, 228, 42)) return {ActionKind::filter_next};
    if (hit(x, y, 278, 604, 228, 42)) return {ActionKind::toggle_favorite};
    if (hit(x, y, 770, 604, 228, 42)) return {ActionKind::export_log};
  } else if (g_view == View::traffic) {
    if (hit(x, y, 604, 616, 202, 42)) return {ActionKind::export_log};
    if (hit(x, y, 822, 616, 202, 42)) return {ActionKind::filter_next};
    if (hit(x, y, 1040, 616, 202, 42)) return {ActionKind::clear_events};
  } else if (g_view == View::map) {
    if (hit(x, y, 26, 604, 210, 42)) return {ActionKind::center_map};
    if (hit(x, y, 252, 604, 210, 42)) return {ActionKind::follow_node};
    if (hit(x, y, 478, 604, 210, 42)) return {ActionKind::mark_point};
    if (hit(x, y, 704, 604, 210, 42)) return {ActionKind::save_snapshot};
  } else {
    if (hit(x, y, 24, 578, 242, 48)) return {ActionKind::scan_toggle};
    if (hit(x, y, 284, 578, 242, 48)) return {ActionKind::record_iq_toggle};
    if (hit(x, y, 544, 578, 242, 48)) return {ActionKind::export_log};
    if (hit(x, y, 804, 578, 242, 48)) return {ActionKind::clear_events};
  }
  return {};
}

bool active() { return g_active; }
bool spectrum_active() { return g_active && (g_view == View::overview || g_view == View::rf_health); }
View view() { return g_view; }

void show_documentation_view(View view_value, const Snapshot& snapshot) {
  g_view = view_value;
  enter(snapshot);
}

void toggle_filter() {
  g_filter = (g_filter + 1) % 2;
  if (g_active) draw_static();
}

void toggle_follow_node() {
  g_follow_node = !g_follow_node;
  if (g_active) draw_static();
}

bool self_check() {
  Snapshot snapshot{};
  snapshot.frequency_hz = 906875000;
  snapshot.sf = 11;
  snapshot.bandwidth_hz = 250000;
  snapshot.node_count = 1;
  snapshot.nodes[0].id = 0xA1B2C3D4;
  snapshot.event_count = 1;
  snapshot.events[0].sender = snapshot.nodes[0].id;
  if (static_cast<uint8_t>(View::count) != 5 || kTabW * 5 != 1280) return false;
  char value[16];
  format_id(value, sizeof(value), snapshot.nodes[0].id);
  if (strcmp(value, "!A1B2C3D4") != 0) return false;
  return audio_header::home_hit(1117, 9) && !audio_header::home_hit(1115, 9);
}

}  // namespace orcsdr::lora
