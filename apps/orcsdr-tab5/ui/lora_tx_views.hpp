#pragma once
// OrcSDR LoRa TX Lab. Native M5GFX-compatible drawing calls, no HTML/web artwork.
// Shared between Tab5-compatible landscape UI and the future LilyGO portrait
// display. All RF data and TX admission are supplied by the caller's Snapshot.
// A profile-only radio cannot arm or send; no controls can bypass that gate.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <M5Unified.h>
#include "lora_dashboard.hpp"

namespace orcsdr::lora::tx_views {

enum class Page : uint8_t { operator_panel = 0, messenger = 1, dual_radio = 2 };
constexpr uint16_t kBg = 0x0000, kPanel = 0x0841, kCyan = 0x2e7f;
constexpr uint16_t kGreen = 0x6fe8, kYellow = 0xff24, kMuted = 0x8c71;
constexpr uint16_t kGrid = 0x2945, kWhite = 0xffff, kActive = 0x0a43;
constexpr int kPortraitWidth = 568, kPortraitHeight = 1232;
constexpr int kLandscapeWidth = 1280, kLandscapeHeight = 720;
constexpr size_t kBenchPacketBytes = 13;  // reserved for future explicit test backend

constexpr bool portrait(int width, int height) {
  return width < height && width >= kPortraitWidth && height >= kPortraitHeight;
}
constexpr bool hit(int x, int y, int bx, int by, int w, int h) {
  return x >= bx && y >= by && x < bx + w && y < by + h;
}
constexpr int page_at(int x, int y, bool vertical) {
  if (vertical) {
    if (y < 155 || y >= 211 || x < 16 || x >= 552) return -1;
    return x < 194 ? 0 : (x < 372 ? 1 : 2);
  }
  if (y < 174 || y >= 222 || x < 680 || x >= 1222) return -1;
  return x < 860 ? 0 : (x < 1040 ? 1 : 2);
}
constexpr bool arm_rect(int x, int y, bool vertical) {
  return vertical ? hit(x, y, 28, 1026, 244, 72)
                  : hit(x, y, 52, 540, 286, 62);
}
constexpr bool send_rect(int x, int y, bool vertical) {
  return vertical ? hit(x, y, 290, 1026, 244, 72)
                  : hit(x, y, 356, 540, 286, 62);
}
constexpr const char* page_name(Page page) {
  switch (page) {
    case Page::operator_panel: return "OPERATOR";
    case Page::messenger: return "MESSAGES";
    case Page::dual_radio: return "DUAL RADIO";
  }
  return "OPERATOR";
}

template <typename G> void label(G& gfx, const char* s, int x, int y,
                                 uint16_t color = kWhite, int sz = 2) {
  gfx.setTextDatum(middle_left);
  gfx.setTextColor(color);
  gfx.setTextSize(sz);
  gfx.drawString(s, x, y);
}
template <typename G> void card(G& gfx, int x, int y, int w, int h,
                                uint16_t stroke = kCyan) {
  gfx.fillRoundRect(x, y, w, h, 9, kPanel);
  gfx.drawRoundRect(x, y, w, h, 9, stroke);
}
template <typename G> void control(G& gfx, int x, int y, int w, int h,
                                   const char* name, bool enabled) {
  gfx.fillRoundRect(x, y, w, h, 9, kPanel);
  gfx.drawRoundRect(x, y, w, h, 9, enabled ? kYellow : kMuted);
  label(gfx, name, x + 16, y + h / 2, enabled ? kWhite : kMuted, 2);
}
template <typename G> void menu(G& gfx, Page page, bool vertical) {
  static constexpr const char* titles[] = {"OPERATOR", "MESSAGES", "DUAL RADIO"};
  const int y = vertical ? 155 : 174;
  const int h = vertical ? 56 : 48;
  const int x0 = vertical ? 16 : 680;
  const int w = vertical ? 178 : 180;
  for (int i = 0; i < 3; ++i) {
    const int x = x0 + i * w;
    const bool active = i == static_cast<int>(page);
    gfx.fillRoundRect(x, y, w, h, 6, active ? kActive : kPanel);
    gfx.drawRoundRect(x, y, w, h, 6, active ? kGreen : kGrid);
    label(gfx, titles[i], x + (vertical ? 12 : 18), y + h / 2,
          active ? kGreen : kWhite, vertical ? 1 : 2);
  }
}
template <typename G> void field(G& gfx, int x, int y, int w,
                                 const char* name, const char* value) {
  label(gfx, name, x, y + 10, kCyan, 1);
  label(gfx, value, x, y + 36, kWhite, 2);
  gfx.drawFastHLine(x, y + 55, w, kGrid);
}
template <typename G> void status(G& gfx, const Snapshot& s, int x, int y, int w, int h) {
  card(gfx, x, y, w, h, kYellow);
  label(gfx, "SX1262 / TX STATUS", x + 16, y + 23, kCyan, 1);
  label(gfx, lora_tx::readiness_name(s.tx.readiness), x + 16, y + 56,
        s.tx.readiness == lora_tx::Readiness::tx_verified ? kGreen : kYellow, 2);
  label(gfx, lora_tx::denial_name(lora_tx::can_arm(s.tx)), x + 16, y + h - 24,
        lora_tx::can_arm(s.tx) == lora_tx::Denial::none ? kGreen : kMuted, 1);
}
template <typename G> void action_row(G& gfx, const Snapshot& s, bool vertical,
                                      bool bench_packet) {
  const int y = vertical ? 1026 : 540;
  const int ax = vertical ? 28 : 52, sx = vertical ? 290 : 356;
  const int w = vertical ? 244 : 286, h = vertical ? 72 : 62;
  const bool can_arm = lora_tx::can_arm(s.tx) == lora_tx::Denial::none;
  const bool can_send = bench_packet &&
      lora_tx::can_send(s.tx, kBenchPacketBytes) == lora_tx::Denial::none;
  control(gfx, ax, y, w, h, !can_arm ? "ARM LOCKED"
                    : (s.tx.operator_armed ? "DISARM TX" : "ARM TX"), can_arm);
  control(gfx, sx, y, w, h, !can_send ? "SEND LOCKED" : "SEND TEST", can_send);
}
template <typename G> void grid(G& gfx, int x, int y, int w, int h) {
  gfx.drawRect(x, y, w, h, kGrid);
  for (int i = 1; i < 5; ++i) gfx.drawFastVLine(x + w * i / 5, y, h, kGrid);
  for (int i = 1; i < 4; ++i) gfx.drawFastHLine(x, y + h * i / 4, w, kGrid);
}
template <typename G> void draw_static(G& gfx, const Snapshot& s, Page page,
                                       bool vertical) {
  if (vertical) {
    // The native portrait header and footer belong to lora_dashboard.cpp;
    // these cards fit entirely between y=225 and y=1115.
    card(gfx, 16, 225, 536, 900);
    label(gfx, page == Page::operator_panel ? "SX1262 PACKET TX" :
               page == Page::messenger ? "LORA MESSENGER" : "TWO RADIOS",
          34, 256, kGreen, 3);
    if (page == Page::operator_panel) {
      card(gfx, 28, 291, 512, 266);
      label(gfx, "PACKET PROFILE", 48, 316, kCyan, 2);
      field(gfx, 48, 346, 472, "MODULATION", "LORA / SX1262");
      field(gfx, 48, 415, 472, "RX DEFAULT", "SF11  /  BW 250k");
      card(gfx, 28, 575, 512, 254, kYellow);
      label(gfx, "TRANSMISSION SAFETY", 48, 605, kYellow, 2);
      label(gfx, "Separate TX profile required", 48, 738, kMuted, 1);
      label(gfx, "No TX on startup or connection", 48, 780, kMuted, 1);
      card(gfx, 28, 849, 512, 153);
      label(gfx, "BENCH PACKET ONLY", 48, 875, kCyan, 2);
      label(gfx, "No editable payload configured", 48, 967, kMuted, 1);
    } else if (page == Page::messenger) {
      card(gfx, 28, 291, 512, 260);
      label(gfx, "PACKET COMPOSER", 48, 319, kCyan, 2);
      field(gfx, 48, 354, 472, "DESTINATION", "NOT CONFIGURED");
      field(gfx, 48, 427, 472, "MESSAGE", "COMPOSER PENDING");
      card(gfx, 28, 570, 512, 250);
      label(gfx, "PACKET ACTIVITY", 48, 598, kCyan, 2);
      gfx.drawFastHLine(48, 620, 472, kGrid);
      card(gfx, 28, 838, 512, 164, kYellow);
      label(gfx, "MESSAGING TX NOT AVAILABLE", 48, 866, kYellow, 2);
    } else {
      card(gfx, 28, 291, 512, 338);
      label(gfx, "RTL-SDR / IQ MONITOR", 48, 319, kCyan, 2);
      grid(gfx, 48, 378, 470, 212);
      card(gfx, 28, 647, 512, 355, kYellow);
      label(gfx, "SX1262 / PACKET RADIO", 48, 676, kCyan, 2);
      field(gfx, 48, 715, 472, "TX ANTENNA", "NOT VERIFIED");
      field(gfx, 48, 803, 472, "TRANSMISSION", "LOCKED");
    }
    action_row(gfx, s, true, page == Page::operator_panel);
    return;
  }
  card(gfx, 24, 138, 1224, 480);
  label(gfx, "LORA / SX1262 TX LAB", 49, 169, kGreen, 3);
  if (page == Page::operator_panel) {
    card(gfx, 45, 237, 573, 285);
    label(gfx, "PACKET SETTINGS", 65, 261, kCyan, 2);
    field(gfx, 65, 287, 533, "MODULATION", "LORA / SX1262");
    field(gfx, 65, 353, 533, "DEFAULT RX PROFILE", "SF11 / BW 250k");
    card(gfx, 635, 237, 588, 285, kYellow);
    label(gfx, "TRANSMISSION GATE", 655, 261, kYellow, 2);
    label(gfx, "Antenna / TX region unverified", 655, 446, kMuted, 1);
    action_row(gfx, s, false, true);
  } else if (page == Page::messenger) {
    card(gfx, 45, 237, 578, 282);
    label(gfx, "COMPOSE PACKET", 65, 263, kCyan, 2);
    field(gfx, 65, 299, 530, "DESTINATION", "NOT CONFIGURED");
    field(gfx, 65, 374, 530, "PAYLOAD", "COMPOSER PENDING");
    card(gfx, 640, 237, 583, 282);
    label(gfx, "TX / RX ACTIVITY", 660, 263, kCyan, 2);
    gfx.drawFastHLine(660, 291, 542, kGrid);
    action_row(gfx, s, false, false);
  } else {
    card(gfx, 45, 237, 754, 282);
    label(gfx, "RTL-SDR / IQ MONITOR", 65, 263, kCyan, 2);
    grid(gfx, 65, 304, 713, 186);
    card(gfx, 815, 237, 408, 282, kYellow);
    label(gfx, "SX1262 / PACKET", 835, 263, kCyan, 2);
    action_row(gfx, s, false, false);
  }
}

template <typename G> void draw_values(G& gfx, const Snapshot& s, Page page,
                                       bool vertical) {
  char freq[64] = {};
  const bool tx_known = s.tx.frequency_hz != 0;
  if (tx_known) std::snprintf(freq, sizeof(freq), "TX %.3f MHz",
                               s.tx.frequency_hz / 1000000.0);
  else std::snprintf(freq, sizeof(freq), "TX FREQ: NOT SET");
  if (vertical) {
    if (page == Page::operator_panel) {
      gfx.fillRect(48, 504, 470, 40, kPanel);
      label(gfx, freq, 48, 521, kYellow, 2);
      gfx.fillRect(48, 625, 466, 94, kPanel);
      label(gfx, lora_tx::readiness_name(s.tx.readiness), 48, 646, kYellow, 2);
      label(gfx, lora_tx::denial_name(lora_tx::can_arm(s.tx)), 48, 689, kMuted, 1);
    } else if (page == Page::messenger) {
      gfx.fillRect(48, 636, 470, 160, kPanel);
      if (s.event_count) {
        label(gfx, "RX: VERIFIED EVENT PRESENT", 48, 663, kGreen, 1);
        label(gfx, "Select TRAFFIC for payload details", 48, 701, kMuted, 1);
      } else label(gfx, "NO VERIFIED PACKETS", 48, 663, kMuted, 2);
      label(gfx, "TX: NO PACKETS SENT", 48, 753, kMuted, 1);
    } else {
      gfx.fillRect(48, 879, 470, 99, kPanel);
      label(gfx, freq, 48, 905, kYellow, 2);
      label(gfx, lora_tx::denial_name(lora_tx::can_arm(s.tx)), 48, 949, kMuted, 1);
    }
    gfx.fillRect(31, 1103, 510, 16, kPanel);
    label(gfx, s.tx.operator_armed ? "OPERATOR ARMED" : "TX LOCKED / RX INDEPENDENT",
          34, 1111, s.tx.operator_armed ? kYellow : kMuted, 1);
    return;
  }
  if (page == Page::operator_panel) {
    gfx.fillRect(65, 432, 531, 76, kPanel);
    label(gfx, freq, 65, 449, kYellow, 2);
    label(gfx, lora_tx::readiness_name(s.tx.readiness), 65, 487, kMuted, 1);
    gfx.fillRect(654, 306, 544, 110, kPanel);
    label(gfx, lora_tx::denial_name(lora_tx::can_arm(s.tx)), 655, 335, kYellow, 2);
    label(gfx, s.tx.operator_armed ? "OPERATOR ARMED" : "NO TX ARM", 655, 378,
          s.tx.operator_armed ? kYellow : kMuted, 2);
  } else if (page == Page::messenger) {
    gfx.fillRect(658, 318, 544, 175, kPanel);
    label(gfx, s.event_count ? "RX: VERIFIED EVENT PRESENT" : "RX: NO VERIFIED PACKETS",
          660, 343, s.event_count ? kGreen : kMuted, 2);
    label(gfx, "TX: NO PACKETS SENT", 660, 393, kMuted, 2);
    label(gfx, lora_tx::denial_name(lora_tx::can_arm(s.tx)),
          660, 451, kYellow, 1);
  } else {
    gfx.fillRect(835, 309, 368, 180, kPanel);
    label(gfx, freq, 835, 334, kYellow, 2);
    label(gfx, lora_tx::readiness_name(s.tx.readiness), 835, 379, kYellow, 1);
    label(gfx, lora_tx::denial_name(lora_tx::can_arm(s.tx)), 835, 423, kMuted, 1);
    label(gfx, "No fabricated IQ spectrum", 67, 503, kMuted, 1);
  }
}
}  // namespace orcsdr::lora::tx_views
