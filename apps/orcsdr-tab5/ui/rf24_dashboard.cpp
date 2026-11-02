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
Page g_page = Page::overview;
bool g_active = false;
bool g_scanning = false;
uint32_t g_revision = 0;
Snapshot g_snapshot{};
layout::View g_layout = layout::make_view({0, 0, 1280, 720});

bool hit(int32_t x, int32_t y, int bx, int by, int bw, int bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

Page tab_at(int32_t x) {
  for (uint8_t i = 0; i < static_cast<uint8_t>(Page::count); ++i) {
    const auto r = g_layout.tabs[i];
    if (hit(x, r.y + r.h / 2, r.x, r.y, r.w, r.h)) return static_cast<Page>(i);
  }
  return Page::count;
}

bool hit(int32_t x, int32_t y, layout::Rect r) {
  return hit(x, y, r.x, r.y, r.w, r.h);
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

uint16_t signal_color(int16_t rssi) {
  return rssi >= -60 ? kGreen : rssi >= -75 ? kYellow : kMuted;
}

void channel_footprint(const AccessPoint& ap, int* low, int* high) {
  *low = static_cast<int>(ap.channel) - 2;
  *high = static_cast<int>(ap.channel) + 2;
  if (ap.secondary_channel_offset > 0) *high += 4;
  if (ap.secondary_channel_offset < 0) *low -= 4;
}

void scan_detail(const Snapshot& snapshot, char* out, size_t size) {
  if (snapshot.scanning) {
    snprintf(out, size, "SCAN IN PROGRESS");
  } else if (snapshot.scan_complete) {
    snprintf(out, size, "LAST SCAN %us AGO  |  %ums", snapshot.scan_age_seconds,
             snapshot.scan_duration_ms);
  } else {
    snprintf(out, size, "NO COMPLETED SCAN");
  }
}

void draw_header(const Snapshot& snapshot) {
  if (!g_layout.legacy) {
    audio_header::draw_brand("2.4 GHz ANALYZER");
    audio_header::draw_home_button();
    audio_header::draw_battery(M5.Power.getBatteryLevel());
    audio_header::draw_mute_button(snapshot.sound_enabled);
    audio_header::draw_visualizer_button(snapshot.visualizer_available);
    audio_header::draw_settings_button();
    const auto band = g_layout.scan_band;
    M5.Display.fillRect(band.x, band.y, band.w, band.h, kBg);
    panel(band.x, band.y, band.w, band.h, snapshot.scanning ? kMuted : kCyan);
    text(snapshot.ready ? "READY" : "OFFLINE", g_layout.state.x, g_layout.state.y,
         snapshot.scanning ? kYellow : snapshot.ready ? kGreen : kRed, 2);
    panel(g_layout.rescan.x, g_layout.rescan.y, g_layout.rescan.w, g_layout.rescan.h,
          snapshot.scanning ? kMuted : kGreen);
    text(snapshot.scanning ? "SCAN..." : "RESCAN",
         g_layout.rescan.x + g_layout.rescan.w / 2,
         g_layout.rescan.y + g_layout.rescan.h / 2,
         snapshot.scanning ? kMuted : kGreen, 2, middle_center);
    char detail[64]{};
    scan_detail(snapshot, detail, sizeof(detail));
    text(detail, g_layout.detail.x, g_layout.detail.y, kMuted, 1);
    snprintf(detail, sizeof(detail), "FOUND %u / SHOWING %u", snapshot.total_access_point_count,
             snapshot.access_point_count);
    text(detail, g_layout.counts.x, g_layout.counts.y, kCyan, 1);
    return;
  }
  M5.Display.fillRect(0, 0, 1280, 100, kBg);
  M5.Display.drawFastHLine(20, 98, 1240, kCyan);
  audio_header::draw_brand("2.4 GHz ANALYZER");
  text(snapshot.ready ? "READY" : "OFFLINE", 808, 34,
       snapshot.scanning ? kYellow : snapshot.ready ? kGreen : kRed, 1, middle_right);
  panel(830, 16, 190, 46, snapshot.scanning ? kMuted : kGreen);
  text(snapshot.scanning ? "SCANNING" : "RESCAN", 925, 39,
       snapshot.scanning ? kMuted : kGreen, 1, middle_center);
  audio_header::draw_home_button();
  audio_header::draw_battery(M5.Power.getBatteryLevel());
  audio_header::draw_mute_button(snapshot.sound_enabled);
  audio_header::draw_visualizer_button(snapshot.visualizer_available);
  audio_header::draw_settings_button();
  char detail[64]{}; scan_detail(snapshot, detail, sizeof(detail));
  text(detail, 370, 72, kMuted, 1);
  snprintf(detail, sizeof(detail), "FOUND %u / SHOWING %u", snapshot.total_access_point_count,
           snapshot.access_point_count);
  text(detail, 1240, 66, kCyan, 1, middle_right);
}

void draw_tabs() {
  for (uint8_t i = 0; i < static_cast<uint8_t>(Page::count); ++i) {
    const Page page = static_cast<Page>(i); const auto r = g_layout.tabs[i];
    const bool selected = page == g_page;
    panel(r.x, r.y, r.w, r.h, selected ? kGreen : kCyan);
    const char* label = g_layout.legacy ? page_name(page) : layout::tab_label(i, r.w);
    text(label, r.x + r.w / 2, r.y + r.h / 2,
         selected ? kGreen : kCyan, 2, middle_center);
  }
}

void draw_overview(const Snapshot& snapshot) {
  panel(20, 100, 400, 180); panel(438, 100, 822, 180); panel(20, 298, 1240, 326);
  text("SCAN STATUS", 40, 126, kCyan, 1);
  char value[48]{}; snprintf(value, sizeof(value), "%u FOUND", snapshot.total_access_point_count);
  text(value, 40, 172, kGreen, 3);
  snprintf(value, sizeof(value), "SHOWING STRONGEST %u OF %u", snapshot.access_point_count,
           snapshot.total_access_point_count);
  text(value, 40, 196, kMuted, 1);
  const auto* strongest = snapshot.access_point_count ? &snapshot.access_points[0] : nullptr;
  text("STRONGEST OBSERVED", 40, 220, kMuted, 1);
  text(strongest ? (strongest->ssid[0] ? strongest->ssid : "<hidden>") : "--", 40, 248, TFT_WHITE, 2);
  uint8_t secured = 0;
  for (uint8_t i = 0; i < snapshot.access_point_count; ++i) secured += snapshot.access_points[i].secure;
  snprintf(value, sizeof(value), "SECURITY: %u SECURED / %u OPEN", secured,
           snapshot.access_point_count - secured);
  text(value, 40, 270, kMuted, 1);
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
  text("SSID", 48, 354, kMuted, 1); text("BSSID", 500, 354, kMuted, 1);
  text("CH", 670, 354, kMuted, 1); text("RSSI", 760, 354, kMuted, 1);
  text("SECURITY", 900, 354, kMuted, 1); text("PHY", 1100, 354, kMuted, 1);
  const uint8_t shown = std::min<uint8_t>(snapshot.access_point_count, 6);
  for (uint8_t i = 0; i < shown; ++i) {
    const auto& ap = snapshot.access_points[i]; char short_bssid[12]{}, rssi[16]{}, channel[8]{};
    const int y = 392 + i * 36; bssid(ap.bssid, short_bssid, sizeof(short_bssid));
    snprintf(channel, sizeof(channel), "%u", ap.channel); snprintf(rssi, sizeof(rssi), "%d dBm", ap.rssi);
    text(ap.ssid[0] ? ap.ssid : "<hidden>", 48, y, TFT_WHITE, 2); text(short_bssid, 500, y, kMuted, 2);
    text(channel, 670, y, kCyan, 2); text(rssi, 760, y, signal_color(ap.rssi), 2);
    text(ap.security, 900, y, ap.secure ? kGreen : kRed, 2); text(ap.phy, 1100, y, kMuted, 2);
  }
}

void draw_channels(const Snapshot& snapshot) {
  panel(20, 100, 1240, 524); text("CHANNEL LANDSCAPE", 40, 126, kCyan, 1);
  text("Observed AP channel footprints from scan beacons — not RF power, airtime, or occupancy.",
       40, 154, kMuted, 1);
  constexpr int kPlotLeft = 220, kPlotRight = 1230, kPlotTop = 190, kPlotBottom = 510;
  constexpr int kSpacing = (kPlotRight - kPlotLeft) / 13;
  const auto channel_x = [&](int channel) { return kPlotLeft + (channel - 1) * kSpacing; };
  M5.Display.drawRect(kPlotLeft, kPlotTop, kPlotRight - kPlotLeft, kPlotBottom - kPlotTop, kMuted);
  for (int channel = 1; channel <= 14; ++channel) {
    const int x = channel_x(channel); char label[4]{}; snprintf(label, sizeof(label), "%d", channel);
    M5.Display.drawFastVLine(x, kPlotTop + 1, kPlotBottom - kPlotTop - 2, 0x2104);
    text(label, x, 174, kCyan, 1, middle_center);
  }
  const uint8_t shown = std::min<uint8_t>(snapshot.access_point_count, kAccessPointCapacity);
  for (uint8_t i = 0; i < shown; ++i) {
    const auto& ap = snapshot.access_points[i];
    if (ap.channel == 0 || ap.channel > 14) continue;
    int low = 0, high = 0; channel_footprint(ap, &low, &high);
    const int left = std::max(kPlotLeft, channel_x(std::max(1, low)));
    const int right = std::min(kPlotRight, channel_x(std::min(14, high)));
    const int y = kPlotTop + 12 + i * 18;
    char label[42]{};
    snprintf(label, sizeof(label), "%s  %d", ap.ssid[0] ? ap.ssid : "<hidden>", ap.rssi);
    text(label, 34, y + 5, signal_color(ap.rssi), 1);
    M5.Display.fillRoundRect(left, y, std::max(4, right - left), 12, 4, signal_color(ap.rssi));
    M5.Display.drawFastVLine(channel_x(ap.channel), y - 2, 16, TFT_WHITE);
  }
  uint16_t overlap[15]{}; uint16_t max_overlap = 1;
  for (int channel = 1; channel <= 14; ++channel) {
    for (uint8_t i = 0; i < shown; ++i) {
      int low = 0, high = 0; channel_footprint(snapshot.access_points[i], &low, &high);
      if (channel >= low && channel <= high)
        overlap[channel] += static_cast<uint16_t>(std::max<int>(1, (snapshot.access_points[i].rssi + 100) / 8));
    }
    max_overlap = std::max(max_overlap, overlap[channel]);
  }
  text("AP-FOOTPRINT OVERLAP ESTIMATE", 40, 548, kCyan, 1);
  for (int channel = 1; channel <= 14; ++channel) {
    const int x = channel_x(channel);
    const int height = overlap[channel] * 48 / max_overlap;
    const uint16_t color = overlap[channel] * 3 > max_overlap * 2 ? kRed :
                           overlap[channel] * 3 > max_overlap ? kYellow : kGreen;
    M5.Display.fillRect(x - 20, 608 - height, 40, height, color);
  }
  text("Higher means more/stronger observed AP footprints; it is not channel utilization.",
       40, 586, kMuted, 1);
}

void draw_devices(const Snapshot& snapshot) {
  panel(20, 100, 1240, 524); text("OBSERVED ACCESS POINTS", 40, 126, kCyan, 1);
  text("Station discovery requires separately proven promiscuous capture.", 40, 154, kMuted, 1);
  text("SSID", 48, 194, kMuted, 1); text("BSSID", 430, 194, kMuted, 1);
  text("CH", 600, 194, kMuted, 1); text("RSSI", 680, 194, kMuted, 1);
  text("SECURITY", 820, 194, kMuted, 1); text("PHY / WIDTH", 1060, 194, kMuted, 1);
  const uint8_t shown = std::min<uint8_t>(snapshot.access_point_count, 10);
  for (uint8_t i = 0; i < shown; ++i) {
    const auto& ap = snapshot.access_points[i]; char short_bssid[12]{}, value[16]{}; const int y = 232 + i * 36;
    bssid(ap.bssid, short_bssid, sizeof(short_bssid)); snprintf(value, sizeof(value), "%u", ap.channel);
    text(ap.ssid[0] ? ap.ssid : "<hidden>", 48, y, TFT_WHITE, 2); text(short_bssid, 430, y, kMuted, 2);
    text(value, 600, y, kCyan, 2); snprintf(value, sizeof(value), "%d dBm", ap.rssi);
    text(value, 680, y, signal_color(ap.rssi), 2); text(ap.security, 820, y, ap.secure ? kGreen : kRed, 2);
    snprintf(value, sizeof(value), "%s / HT%d", ap.phy, ap.secondary_channel_offset ? 40 : 20);
    text(value, 1060, y, kMuted, 2);
  }
}

void draw_csi() {
  panel(20, 100, 1240, 524, kYellow); text("CSI / EXPERIMENTAL", 40, 132, kYellow, 2);
  text("CSI is not available.", 40, 202, TFT_WHITE, 3);
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

int draw_wrapped(const char* value, int x, int y, int width, uint8_t size,
                 uint16_t color, int line_height, int max_lines = 0) {
  M5.Display.setFont(nullptr);
  M5.Display.setTextSize(size);
  M5.Display.setTextColor(color, kBg);
  const char* source = value ? value : "";
  char line[256]{};
  int lines=0;
  while(*source && (!max_lines || lines<max_lines)) {
    size_t used=0;
    while(source[used] && source[used]!='\n' && used+1<sizeof(line)) {
      line[used]=source[used];line[used+1]='\0';
      if(used && M5.Display.textWidth(line)>width) break;
      ++used;
    }
    if(source[used] && source[used]!='\n' && source[used]!=' ') {
      size_t split=used;
      while(split && source[split]!=' ') --split;
      if(split) used=split;
    }
    // A single overwide token still advances; ordinary words wrap at spaces.
    if(!used && *source!='\n') used=1;
    std::memcpy(line,source,used);line[used]='\0';
    M5.Display.setTextDatum(middle_left);
    M5.Display.drawString(line,x,y+lines*line_height);++lines;
    source+=used;
    while(*source==' ') ++source;
    if(*source=='\n') ++source;
  }
  return lines;
}

void ap_details(const AccessPoint& ap, char* out, size_t capacity) {
  char mac[12]{};
  bssid(ap.bssid, mac, sizeof(mac));
  snprintf(out, capacity, "%s  CH %u  %d dBm  %s  %s/HT%d", mac, ap.channel, ap.rssi,
           ap.security, ap.phy, ap.secondary_channel_offset ? 40 : 20);
}

void draw_ap_card(const AccessPoint& ap, layout::Rect r, bool portrait) {
  const int pad = portrait ? 10 : 8;
  draw_wrapped(ap.ssid[0] ? ap.ssid : "<hidden>", r.x + pad, r.y + 2,
               r.w - 2 * pad, 2, TFT_WHITE, 18);
  char detail[96]{};
  ap_details(ap, detail, sizeof(detail));
  text(detail, r.x + pad, r.y + r.h - 10, kMuted, 1);
}

void draw_overview_responsive(const Snapshot& snapshot, bool structural) {
  const auto s = g_layout.overview_summary;
  const auto c = g_layout.overview_channels;
  const auto t = g_layout.overview_table;
  if (structural) {
    panel(s.x, s.y, s.w, s.h);
    panel(c.x, c.y, c.w, c.h);
    panel(t.x, t.y, t.w, t.h);
  } else {
    M5.Display.fillRect(s.x + 4, s.y + 4, s.w - 8, s.h - 8, kPanel);
    M5.Display.fillRect(c.x + 4, c.y + 4, c.w - 8, c.h - 8, kPanel);
    M5.Display.fillRect(t.x + 4, t.y + 4, t.w - 8, t.h - 8, kPanel);
  }
  text("SCAN STATUS", s.x + 10, s.y + 14, kCyan, 1);
  char value[64]{};
  snprintf(value, sizeof(value), "%u FOUND", snapshot.total_access_point_count);
  const bool landscape = !g_layout.portrait;
  text(value, s.x + 10, s.y + (landscape ? 40 : 42), kGreen, landscape ? 2 : 3);
  const auto* strongest = snapshot.access_point_count ? &snapshot.access_points[0] : nullptr;
  if (landscape) {
    text(strongest ? "STRONGEST:" : "NO ACCESS POINTS", s.x + 10, s.y + 58, kMuted, 1);
    if (strongest)
      draw_wrapped(strongest->ssid[0] ? strongest->ssid : "<hidden>", s.x + 10, s.y + 74,
                   s.w - 20, 2, TFT_WHITE, 18);
  } else {
    text("STRONGEST OBSERVED", s.x + 10, s.y + 68, kMuted, 1);
    if (strongest)
      draw_wrapped(strongest->ssid[0] ? strongest->ssid : "<hidden>", s.x + 10, s.y + 88,
                   s.w - 20, 2, TFT_WHITE, 18);
  }
  text("AP OBSERVATIONS BY CHANNEL", c.x + 10, c.y + 14, kCyan, 1);
  uint8_t counts[15]{};
  for (uint8_t i = 0; i < snapshot.access_point_count; ++i)
    if (snapshot.access_points[i].channel <= 14 && snapshot.access_points[i].channel > 0)
      ++counts[snapshot.access_points[i].channel];
  const int inset = 10;
  const int left = c.x + inset;
  const int chart_w = c.w - 2 * inset;
  const int top = c.y + 26;
  const int bottom = c.y + c.h - (landscape ? 10 : 18);
  const int slot = std::max(1, chart_w / 14);
  const int max_bar = std::max(1, bottom - top - 18);
  for (int channel = 1; channel <= 14; ++channel) {
    const int x = left + (channel - 1) * slot;
    const int h = std::min(max_bar, counts[channel] * 8);
    M5.Display.drawRect(x + 2, top, std::max(1, slot - 5), max_bar, kMuted);
    if (h) M5.Display.fillRect(x + 3, bottom - h - 16, std::max(1, slot - 7), h, kGreen);
    char label[4]{};
    snprintf(label, sizeof(label), "%d", channel);
    text(label, x + slot / 2, bottom - 4, kMuted, 1, middle_center);
  }
  text("TOP OBSERVED ACCESS POINTS", t.x + 10, t.y + 14, kCyan, 1);
  if (snapshot.access_point_count == 0) {
    text(snapshot.scanning ? "Waiting for scan results" : "No access points in the last scan",
         t.x + 12, t.y + 46, kMuted, 2);
    return;
  }
  const int data_y = t.y + 25;
  const int data_h = t.h - 31;
  const bool two_columns = !g_layout.portrait;
  const int columns = two_columns ? 2 : 1;
  const int rows = two_columns ? 3 : 6;
  const int cell_gap = two_columns ? 6 : 3;
  const int cell_w = (t.w - 16 - (columns - 1) * cell_gap) / columns;
  const int cell_h = std::max(40, (data_h - (rows - 1) * cell_gap) / rows);
  const uint8_t shown = std::min<uint8_t>(snapshot.access_point_count, 6);
  for (uint8_t i = 0; i < shown; ++i) {
    const int row = two_columns ? i / 2 : i;
    const int col = two_columns ? i % 2 : 0;
    const layout::Rect card{t.x + 8 + col * (cell_w + cell_gap),
                            data_y + row * (cell_h + cell_gap), cell_w, cell_h};
    draw_ap_card(snapshot.access_points[i], card, !two_columns);
  }
}

void draw_channels_responsive(const Snapshot& snapshot, bool structural) {
  const auto p = g_layout.page;
  if (structural) panel(p.x, p.y, p.w, p.h);
  else M5.Display.fillRect(p.x + 4, p.y + 4, p.w - 8, p.h - 8, kPanel);
  text("CHANNEL LANDSCAPE", p.x + 10, p.y + 14, kCyan, 1);
  text("Beacon footprint estimate; not RF power, airtime, or occupancy.",
       p.x + 10, p.y + 31, kMuted, 1);
  const auto plot = g_layout.channel_plot;
  const int label_w = 72;
  const int left = plot.x + label_w + 8;
  const int right = plot.x + plot.w - 8;
  const int top = plot.y + 4;
  const int bottom = plot.y + plot.h - 4;
  const auto channel_x = [&](int channel) {
    return left + (channel - 1) * (right - left) / 13;
  };
  M5.Display.drawRect(left, top, right - left, bottom - top, kMuted);
  for (int channel = 1; channel <= 14; ++channel) {
    const int x = channel_x(channel);
    M5.Display.drawFastVLine(x, top + 1, bottom - top - 2, 0x2104);
    char label[4]{};
    snprintf(label, sizeof(label), "%d", channel);
    text(label, x, plot.y - 1, kCyan, 1, middle_center);
  }
  const uint8_t shown = std::min<uint8_t>(snapshot.access_point_count, kAccessPointCapacity);
  const int row_h = std::max(6, (bottom - top - 8) / std::max<int>(1, shown));
  for (uint8_t i = 0; i < shown; ++i) {
    const auto& ap = snapshot.access_points[i];
    if (ap.channel == 0 || ap.channel > 14) continue;
    int low = 0, high = 0;
    channel_footprint(ap, &low, &high);
    const int y = top + 4 + i * row_h;
    char label[8]{};
    snprintf(label, sizeof(label), "AP %02u", static_cast<unsigned>(i + 1));
    text(label, plot.x + 8, y + row_h / 2, signal_color(ap.rssi), 1);
    const int x0 = std::max(left, channel_x(std::max(1, low)));
    const int x1 = std::min(right, channel_x(std::min(14, high)));
    const int bar_h = std::min(12, std::max(5, row_h - 2));
    M5.Display.fillRoundRect(x0, y + (row_h - bar_h) / 2,
                             std::max(4, x1 - x0), bar_h, 3, signal_color(ap.rssi));
    M5.Display.drawFastVLine(channel_x(ap.channel), y + 1, std::max(4, row_h - 2), TFT_WHITE);
  }
  const auto overlap = g_layout.channel_overlap;
  const uint16_t weights[15]{};
  (void)weights;
  uint16_t levels[15]{};
  uint16_t max_level = 1;
  for (int channel = 1; channel <= 14; ++channel) {
    for (uint8_t i = 0; i < shown; ++i) {
      int low = 0, high = 0;
      channel_footprint(snapshot.access_points[i], &low, &high);
      if (channel >= low && channel <= high)
        levels[channel] += static_cast<uint16_t>(std::max<int>(1, (snapshot.access_points[i].rssi + 100) / 8));
    }
    max_level = std::max(max_level, levels[channel]);
  }
  text("AP-FOOTPRINT OVERLAP ESTIMATE", overlap.x + 8, overlap.y + 12, kCyan, 1);
  const int chart_top = overlap.y + 22;
  const int chart_h = std::max(1, overlap.h - 30);
  for (int channel = 1; channel <= 14; ++channel) {
    const int x = left + (channel - 1) * (right - left) / 13;
    const int h = levels[channel] * chart_h / max_level;
    const uint16_t color = levels[channel] * 3 > max_level * 2 ? kRed :
                           levels[channel] * 3 > max_level ? kYellow : kGreen;
    M5.Display.fillRect(x - 4, chart_top + chart_h - h, 8, h, color);
  }
}

void draw_devices_responsive(const Snapshot& snapshot, bool structural) {
  const auto p = g_layout.page;
  if (structural) panel(p.x, p.y, p.w, p.h);
  else M5.Display.fillRect(p.x + 4, p.y + 4, p.w - 8, p.h - 8, kPanel);
  text("OBSERVED ACCESS POINTS", p.x + 10, p.y + 14, kCyan, 1);
  text("Station discovery requires separately proven promiscuous capture.",
       p.x + 10, p.y + 31, kMuted, 1);
  if (snapshot.access_point_count == 0) {
    text(snapshot.scanning ? "Waiting for scan results" : "No access points in the last scan",
         p.x + 12, p.y + 64, kMuted, 2);
    return;
  }
  const auto d = g_layout.devices_data;
  const int columns = g_layout.portrait ? 1 : 2;
  const int rows = columns == 1 ? 10 : 5;
  const int gap = 4;
  const int cell_w = (d.w - (columns - 1) * gap) / columns;
  const int cell_h = (d.h - (rows - 1) * gap) / rows;
  const uint8_t shown = std::min<uint8_t>(snapshot.access_point_count, 10);
  for (uint8_t i = 0; i < shown; ++i) {
    const int row = columns == 1 ? i : i / 2;
    const int col = columns == 1 ? 0 : i % 2;
    const layout::Rect card{d.x + col * (cell_w + gap), d.y + row * (cell_h + gap), cell_w, cell_h};
    M5.Display.fillRoundRect(card.x, card.y, card.w, card.h, 5, kPanel);
    M5.Display.drawRoundRect(card.x, card.y, card.w, card.h, 5, kMuted);
    draw_ap_card(snapshot.access_points[i], card, g_layout.portrait);
  }
}

void draw_csi_responsive() {
  const auto p = g_layout.page;
  panel(p.x, p.y, p.w, p.h, kYellow);
  text("CSI / EXPERIMENTAL", p.x + 12, p.y + 24, kYellow, 2);
  draw_wrapped("CSI is not available.",p.x+12,p.y+56,p.w-24,3,TFT_WHITE,30);
  const int lines=draw_wrapped("This build shows Wi-Fi scan results. CSI capture is not enabled.",
               p.x+12,p.y+94,p.w-24,2,kMuted,24);
  draw_wrapped("Motion and presence sensing are not available.",
               p.x+12,p.y+94+lines*24+8,p.w-24,2,kYellow,24);
}

void draw_settings_responsive() {
  const auto p = g_layout.page;
  panel(p.x, p.y, p.w, p.h);
  text("ANALYZER SETTINGS", p.x + 12, p.y + 24, kCyan, 2);
  const int lines=draw_wrapped("Manage Wi-Fi in Connectivity.",p.x+12,p.y+56,p.w-24,3,TFT_WHITE,30);
  draw_wrapped("Set Wi-Fi power, saved networks and antenna there.",
               p.x+12,p.y+56+lines*30+8,p.w-24,2,kMuted,24);
  const auto b = g_layout.settings_button;
  panel(b.x, b.y, b.w, b.h, kGreen);
  text("OPEN CONNECTIVITY", b.x + b.w / 2, b.y + b.h / 2, kGreen, 2, middle_center);
}

void draw_page(const Snapshot& snapshot, bool structural = true) {
  if (g_layout.legacy) {
    if (structural) M5.Display.fillRect(0, 82, 1280, 554, kBg);
    switch (g_page) {
      case Page::overview: draw_overview(snapshot); break;
      case Page::channels: draw_channels(snapshot); break;
      case Page::devices: draw_devices(snapshot); break;
      case Page::csi: if (structural) draw_csi(); break;
      case Page::settings: if (structural) draw_settings(); break;
      default: break;
    }
    return;
  }
  if (structural) M5.Display.fillRect(g_layout.page.x, g_layout.page.y,
                                      g_layout.page.w, g_layout.page.h, kBg);
  switch (g_page) {
    case Page::overview: draw_overview_responsive(snapshot, structural); break;
    case Page::channels: draw_channels_responsive(snapshot, structural); break;
    case Page::devices: draw_devices_responsive(snapshot, structural); break;
    case Page::csi: if (structural) draw_csi_responsive(); break;
    case Page::settings: if (structural) draw_settings_responsive(); break;
    default: break;
  }
}

void draw_all(const Snapshot& snapshot) {
  M5.Display.fillScreen(kBg); draw_header(snapshot); draw_page(snapshot); draw_tabs();
}
}  // namespace

void enter(const Snapshot& snapshot) {
  g_revision = snapshot.revision; g_scanning = snapshot.scanning;
  g_snapshot = snapshot;
  g_page = Page::overview; g_active = true; draw_all(snapshot);
}
void update(const Snapshot& snapshot) {
  if (!g_active || snapshot.revision == g_revision) return;
  g_revision = snapshot.revision; g_scanning = snapshot.scanning;
  g_snapshot = snapshot;
  draw_header(snapshot); draw_page(snapshot, false);
}
void update_header(const Snapshot& snapshot) {
  if (g_active) draw_header(snapshot);
}
bool select_page(uint8_t page, const Snapshot& snapshot) {
  if (!g_active || page >= static_cast<uint8_t>(Page::count)) return false;
  g_page = static_cast<Page>(page); draw_page(snapshot); draw_tabs(); return true;
}
void leave() { g_active = false; }
bool active() { return g_active; }
Action handle_touch(int32_t x, int32_t y, const Snapshot& snapshot) {
  if (!g_active) return {};
  if (audio_header::home_hit(x, y)) return {ActionKind::close};
  if (audio_header::mute_hit(x, y)) return {ActionKind::toggle_mute};
  if (audio_header::visualizer_hit(x, y)) return {ActionKind::open_visualizer};
  if (audio_header::settings_hit(x, y)) return {ActionKind::open_settings};
  for (uint8_t i = 0; i < static_cast<uint8_t>(Page::count); ++i) {
    if (!hit(x, y, g_layout.tabs[i])) continue;
    if (g_page != static_cast<Page>(i)) {
      g_page = static_cast<Page>(i); draw_page(snapshot); draw_tabs();
    }
    return {};
  }
  if (g_page == Page::settings && hit(x, y, g_layout.settings_button)) return {ActionKind::open_settings};
  if (!g_scanning && hit(x, y, g_layout.rescan)) return {ActionKind::rescan};
  return {};
}
bool self_check() {
  AccessPoint ht40{};
  ht40.channel = 6;
  ht40.secondary_channel_offset = 4;
  int low = 0, high = 0;
  channel_footprint(ht40, &low, &high);
  const auto legacy = layout::make_view({0, 0, 1280, 720});
  return kAccessPointCapacity == 16 && static_cast<uint8_t>(Page::count) == 5 &&
         tab_at(21) == Page::overview && tab_at(20 + 248 * 4) == Page::settings &&
         tab_at(1280) == Page::count && legacy.tabs[4].x == 1012 && low == 4 && high == 12 &&
         static_cast<uint8_t>(ActionKind::open_visualizer) == 5;
}

bool set_viewport(audio_header::HeaderRect safe) {
  const auto next = layout::make_view(safe);
  if (!next.valid) return false;
  if (!audio_header::set_viewport(safe)) return false;
  g_layout = next;
  return true;
}

void draw(const Snapshot& snapshot) {
  if (g_active) draw_all(snapshot);
}

}  // namespace orcsdr::rf24
