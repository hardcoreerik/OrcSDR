#include "rf24_dashboard.hpp"

#include "dashboard_audio_control.hpp"
#include "orc_badge.hpp"

#include <M5Unified.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace orcsdr::rf24 {
namespace {
constexpr uint16_t kBg = TFT_BLACK, kPanel = 0x0841, kCyan = 0x2e7f, kGreen = 0x6fe8;
constexpr uint16_t kMuted = 0x8c71, kYellow = 0xffe0, kRed = 0xf800;
constexpr int kTabY = 648, kTabW = 242, kTabH = 52;
Page g_page = Page::overview;
bool g_active = false;
bool g_scanning = false;
uint32_t g_revision = 0;

bool hit(int32_t x, int32_t y, int bx, int by, int bw, int bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

Page tab_at(int32_t x) {
  if (x < 20 || x >= 20 + 248 * static_cast<int>(Page::count)) return Page::count;
  return static_cast<Page>((x - 20) / 248);
}

void text(const char* value, int x, int y, uint16_t color = TFT_WHITE, uint8_t size = 2,
          textdatum_t datum = middle_left) {
  M5.Display.setFont(nullptr); M5.Display.setTextDatum(datum); M5.Display.setTextSize(size);
  M5.Display.setTextColor(color, kBg); M5.Display.drawString(value, x, y);
}

void panel(int x, int y, int w, int h, uint16_t border = kCyan) {
  M5.Display.fillRoundRect(x, y, w, h, 8, kPanel);
  M5.Display.drawRoundRect(x, y, w, h, 8, border);
}

void bssid(const uint8_t mac[6], char* out, size_t size) {
  snprintf(out, size, "%02X:%02X:%02X", mac[3], mac[4], mac[5]);
}

const char* page_name(Page page) {
  switch (page) {
    case Page::overview: return "OVERVIEW";
    case Page::channels: return "CHANNELS";
    case Page::devices: return "DEVICES";
    case Page::csi: return "CSI";
    case Page::settings: return "SETTINGS";
    default: return "";
  }
}

void draw_header(const Snapshot& snapshot) {
  M5.Display.fillRect(0, 0, 1280, 82, kBg);
  M5.Display.drawFastHLine(20, 80, 1240, kCyan);
  if (!badge::draw(12, 6, 70)) text("ORC", 46, 38, kGreen, 2, middle_center);
  text("OrcSDR", 96, 34, kGreen, 2);
  text("2.4 GHz ANALYZER", 270, 34, TFT_WHITE, 3);
  text(snapshot.ready ? "READY" : "OFFLINE", 808, 34,
       snapshot.scanning ? kYellow : snapshot.ready ? kGreen : kRed, 1, middle_right);
  panel(830, 16, 190, 46, snapshot.scanning ? kMuted : kGreen);
  text(snapshot.scanning ? "SCANNING" : "RESCAN", 925, 39,
       snapshot.scanning ? kMuted : kGreen, 1, middle_center);
  audio_header::draw_home_button();
  audio_header::draw_mute_button(snapshot.sound_enabled);
  audio_header::draw_visualizer_button(snapshot.visualizer_available);
  audio_header::draw_settings_button();
  text(snapshot.message, 270, 66, kMuted, 1);
}

void draw_tabs() {
  for (uint8_t i = 0; i < static_cast<uint8_t>(Page::count); ++i) {
    const Page page = static_cast<Page>(i); const int x = 20 + i * 248;
    const bool selected = page == g_page;
    panel(x, kTabY, kTabW, kTabH, selected ? kGreen : kCyan);
    text(page_name(page), x + kTabW / 2, kTabY + kTabH / 2,
         selected ? kGreen : kCyan, 2, middle_center);
  }
}

void draw_overview(const Snapshot& snapshot) {
  panel(20, 100, 400, 180); panel(438, 100, 822, 180); panel(20, 298, 1240, 326);
  text("SCAN STATUS", 40, 126, kCyan, 1);
  char value[48]{}; snprintf(value, sizeof(value), "%u ACCESS POINTS", snapshot.access_point_count);
  text(value, 40, 172, kGreen, 3);
  const auto* strongest = snapshot.access_point_count ? &snapshot.access_points[0] : nullptr;
  text("STRONGEST OBSERVED", 40, 220, kMuted, 1);
  text(strongest ? (strongest->ssid[0] ? strongest->ssid : "<hidden>") : "--", 40, 250, TFT_WHITE, 2);
  uint8_t secured = 0;
  for (uint8_t i = 0; i < snapshot.access_point_count; ++i) secured += snapshot.access_points[i].secure;
  snprintf(value, sizeof(value), "SECURITY: %u SECURED / %u OPEN", secured,
           snapshot.access_point_count - secured);
  text(value, 40, 272, kMuted, 1);
  text("AP OBSERVATIONS BY CHANNEL", 458, 126, kCyan, 1);
  uint8_t counts[15]{};
  for (uint8_t i = 0; i < snapshot.access_point_count; ++i)
    if (snapshot.access_points[i].channel <= 14 && snapshot.access_points[i].channel > 0)
      ++counts[snapshot.access_points[i].channel];
  uint8_t observed = 0;
  for (int channel = 1; channel <= 14; ++channel) {
    if (!counts[channel]) continue;
    const int x = 458 + observed++ * 56; char label[4]{};
    snprintf(label, sizeof(label), "%d", channel); text(label, x + 18, 254, kMuted, 1, middle_center);
    M5.Display.drawRect(x, 158, 38, 82, kMuted);
    const int height = std::min<int>(72, counts[channel] * 12);
    if (height) M5.Display.fillRect(x + 2, 238 - height, 34, height, kGreen);
  }
  text("TOP OBSERVED ACCESS POINTS", 40, 324, kCyan, 1);
  text("SSID", 48, 354, kMuted, 1); text("BSSID", 560, 354, kMuted, 1);
  text("CH", 730, 354, kMuted, 1); text("RSSI", 840, 354, kMuted, 1); text("SECURITY", 1010, 354, kMuted, 1);
  const uint8_t shown = std::min<uint8_t>(snapshot.access_point_count, 6);
  for (uint8_t i = 0; i < shown; ++i) {
    const auto& ap = snapshot.access_points[i]; char short_bssid[12]{}, rssi[16]{}, channel[8]{};
    const int y = 392 + i * 36; bssid(ap.bssid, short_bssid, sizeof(short_bssid));
    snprintf(channel, sizeof(channel), "%u", ap.channel); snprintf(rssi, sizeof(rssi), "%d dBm", ap.rssi);
    text(ap.ssid[0] ? ap.ssid : "<hidden>", 48, y, TFT_WHITE, 2); text(short_bssid, 560, y, kMuted, 2);
    text(channel, 730, y, kCyan, 2); text(rssi, 840, y, ap.rssi >= -67 ? kGreen : kYellow, 2);
    text(ap.secure ? "SECURED" : "OPEN", 1010, y, ap.secure ? kGreen : kRed, 2);
  }
}

void draw_channels(const Snapshot& snapshot) {
  panel(20, 100, 1240, 524); text("CHANNEL OBSERVATIONS", 40, 126, kCyan, 1);
  text("AP count and strongest scan RSSI. Not airtime or occupancy.", 40, 154, kMuted, 1);
  uint8_t row = 0;
  for (uint8_t channel = 1; channel <= 14; ++channel) {
    uint8_t count = 0; int16_t strongest = -127;
    for (uint8_t i = 0; i < snapshot.access_point_count; ++i) {
      const auto& ap = snapshot.access_points[i];
      if (ap.channel == channel) { ++count; strongest = std::max(strongest, ap.rssi); }
    }
    if (!count) continue;
    const int y = 184 + row++ * 30; char label[48]{};
    snprintf(label, sizeof(label), "CH %2u   %u AP%s", channel, count, count == 1 ? "" : "s");
    text(label, 52, y, TFT_WHITE, 2); M5.Display.drawRect(330, y - 9, 600, 18, kMuted);
    M5.Display.fillRect(332, y - 7, std::min<int>(596, count * 100), 14, kGreen);
    snprintf(label, sizeof(label), "%d dBm strongest", strongest); text(label, 960, y, strongest >= -67 ? kGreen : kYellow, 1);
  }
  if (!row) text("No channel observations yet.", 52, 190, kMuted, 2);
}

void draw_devices(const Snapshot& snapshot) {
  panel(20, 100, 1240, 524); text("OBSERVED ACCESS POINTS", 40, 126, kCyan, 1);
  text("Station discovery requires separately proven promiscuous capture.", 40, 154, kMuted, 1);
  text("SSID", 48, 194, kMuted, 1); text("BSSID", 560, 194, kMuted, 1);
  text("CHANNEL", 730, 194, kMuted, 1); text("RSSI", 880, 194, kMuted, 1); text("SECURITY", 1050, 194, kMuted, 1);
  const uint8_t shown = std::min<uint8_t>(snapshot.access_point_count, 10);
  for (uint8_t i = 0; i < shown; ++i) {
    const auto& ap = snapshot.access_points[i]; char short_bssid[12]{}, value[16]{}; const int y = 232 + i * 36;
    bssid(ap.bssid, short_bssid, sizeof(short_bssid)); snprintf(value, sizeof(value), "%u", ap.channel);
    text(ap.ssid[0] ? ap.ssid : "<hidden>", 48, y, TFT_WHITE, 2); text(short_bssid, 560, y, kMuted, 2);
    text(value, 730, y, kCyan, 2); snprintf(value, sizeof(value), "%d dBm", ap.rssi);
    text(value, 880, y, ap.rssi >= -67 ? kGreen : kYellow, 2); text(ap.secure ? "SECURED" : "OPEN", 1050, y, ap.secure ? kGreen : kRed, 2);
  }
}

void draw_csi() {
  panel(20, 100, 1240, 524, kYellow); text("CSI / EXPERIMENTAL", 40, 132, kYellow, 2);
  text("CSI is unavailable in Real Survey v1.", 40, 202, TFT_WHITE, 3);
  text("Next proof: stock ESP-Hosted 3.0.6 remote CSI APIs, bounded callback queue,", 40, 250, kMuted, 2);
  text("and measured hardware callbacks with controlled traffic.", 40, 282, kMuted, 2);
  text("No CSI, motion, presence, or spectrum values are shown until that proof exists.", 40, 344, kYellow, 2);
}

void draw_settings() {
  panel(20, 100, 1240, 524); text("ANALYZER SETTINGS", 40, 132, kCyan, 2);
  text("Real Survey v1 uses existing Connectivity settings.", 40, 210, TFT_WHITE, 3);
  text("Wi-Fi power, saved profiles, and antenna selection remain in Global Settings.", 40, 256, kMuted, 2);
  panel(930, 520, 290, 62, kGreen); text("OPEN CONNECTIVITY", 1075, 551, kGreen, 2, middle_center);
}

void draw_page(const Snapshot& snapshot) {
  M5.Display.fillRect(0, 82, 1280, 554, kBg);
  switch (g_page) {
    case Page::overview: draw_overview(snapshot); break;
    case Page::channels: draw_channels(snapshot); break;
    case Page::devices: draw_devices(snapshot); break;
    case Page::csi: draw_csi(); break;
    case Page::settings: draw_settings(); break;
    default: break;
  }
}

void draw_all(const Snapshot& snapshot) {
  M5.Display.fillScreen(kBg); draw_header(snapshot); draw_page(snapshot); draw_tabs();
}
}  // namespace

void enter(const Snapshot& snapshot) {
  g_revision = snapshot.revision; g_scanning = snapshot.scanning;
  g_page = Page::overview; g_active = true; draw_all(snapshot);
}
void update(const Snapshot& snapshot) {
  if (!g_active || snapshot.revision == g_revision) return;
  g_revision = snapshot.revision; g_scanning = snapshot.scanning;
  draw_header(snapshot); draw_page(snapshot);
}
void update_header(const Snapshot& snapshot) {
  if (g_active) draw_header(snapshot);
}
void leave() { g_active = false; }
bool active() { return g_active; }
Action handle_touch(int32_t x, int32_t y, const Snapshot& snapshot) {
  if (!g_active) return {};
  if (audio_header::home_hit(x, y)) return {ActionKind::close};
  if (audio_header::mute_hit(x, y)) return {ActionKind::toggle_mute};
  if (audio_header::visualizer_hit(x, y)) return {ActionKind::open_visualizer};
  if (audio_header::settings_hit(x, y)) return {ActionKind::open_settings};
  if (hit(x, y, 20, kTabY, kTabW * 5 + 24, kTabH)) {
    const Page page = tab_at(x);
    if (page != Page::count && g_page != page) {
      g_page = page; draw_page(snapshot); draw_tabs();
    }
    return {};
  }
  if (g_page == Page::settings && hit(x, y, 930, 520, 290, 62)) return {ActionKind::open_settings};
  if (!g_scanning && hit(x, y, 830, 16, 190, 46)) return {ActionKind::rescan};
  return {};
}
bool self_check() {
  return kAccessPointCapacity == 16 && static_cast<uint8_t>(Page::count) == 5 &&
         tab_at(21) == Page::overview && tab_at(20 + 248 * 4) == Page::settings &&
         tab_at(1280) == Page::count && audio_header::self_check() &&
         static_cast<uint8_t>(ActionKind::open_visualizer) == 5;
}

}  // namespace orcsdr::rf24
