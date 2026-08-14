#include "p25_dashboard.hpp"

#include "dashboard_audio_control.hpp"

#include <M5Unified.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace orcsdr::p25 {
namespace {

extern const uint8_t orc_badge_start[] asm("_binary_orc_badge_104_png_start");
extern const uint8_t orc_badge_end[] asm("_binary_orc_badge_104_png_end");

constexpr uint16_t kBg = TFT_BLACK;
constexpr uint16_t kPanel = 0x0841;
constexpr uint16_t kCyan = 0x2e7f;
constexpr uint16_t kGreen = 0x6fe8;
constexpr uint16_t kYellow = 0xff24;
constexpr uint16_t kRed = 0xf9a7;
constexpr uint16_t kMuted = 0x8c71;
constexpr uint16_t kGrid = 0x2945;
constexpr int kHeaderH = 132;
constexpr int kTabsY = 630;
constexpr int kTabW = 256;
constexpr int kSpectrumX = 46;
constexpr int kSpectrumY = 246;
constexpr int kSpectrumW = 1188;
constexpr int kSpectrumH = 145;
constexpr int kWaterfallY = 420;
constexpr int kWaterfallH = 130;
constexpr uint32_t kP25MinHz = 450000000;
constexpr uint32_t kP25MaxHz = 470000000;
constexpr uint32_t kControlChannels[] = {453812500, 453925000, 460187500, 460312500};

struct Talkgroup {
  uint16_t id;
  const char* alias;
  bool may_encrypt;
  uint8_t priority;
};

// Lane County/SW7 metadata verified against the public system listing on 2026-08-14.
constexpr Talkgroup kTalkgroups[] = {
    {20001, "LCSO DISP 1", true, 1},    {20003, "LCSO SEC 2", true, 2},
    {20051, "EPD DISP", true, 1},       {20101, "SPD DISP", true, 1},
    {20204, "LCF East 8", false, 2},    {20391, "LCF Firecom 1", false, 1},
    {20411, "Eugene PW Disp", false, 3}, {20440, "SPW Ch 1", false, 3},
};

static_assert(static_cast<uint8_t>(View::count) == 5);
static_assert(std::size(kControlChannels) == 4);

Snapshot g_snapshot{};
View g_view = View::monitor;
bool g_active = false;
audio_header::Control g_audio_control{};
uint32_t g_last_dynamic_ms = 0;
uint16_t g_waterfall_row[kSpectrumW]{};

bool hit(int32_t x, int32_t y, int bx, int by, int bw, int bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

void text(const char* value, int x, int y, uint16_t color = TFT_WHITE,
          int size = 2, textdatum_t datum = middle_center) {
  M5.Display.setTextDatum(datum);
  M5.Display.setTextSize(size);
  M5.Display.setTextColor(color, kBg);
  M5.Display.drawString(value, x, y);
}

void card(int x, int y, int w, int h, uint16_t border = kCyan) {
  M5.Display.fillRoundRect(x, y, w, h, 12, kPanel);
  M5.Display.drawRoundRect(x, y, w, h, 12, border);
}

void label(const char* value, int x, int y) {
  text(value, x, y, kCyan, 2, top_left);
}

void button(int x, int y, int w, int h, const char* title, uint16_t color = kCyan,
            bool selected = false) {
  M5.Display.fillRoundRect(x, y, w, h, 9, selected ? 0x1264 : kPanel);
  M5.Display.drawRoundRect(x, y, w, h, 9, color);
  text(title, x + w / 2, y + h / 2, selected ? color : TFT_WHITE, 2);
}

void draw_gear(int cx, int cy, uint16_t color) {
  M5.Display.drawCircle(cx, cy, 20, color);
  M5.Display.drawCircle(cx, cy, 7, color);
  for (int i = 0; i < 8; ++i) {
    const float a = i * 3.14159265f / 4.0f;
    M5.Display.drawLine(cx + static_cast<int>(cosf(a) * 21),
                        cy + static_cast<int>(sinf(a) * 21),
                        cx + static_cast<int>(cosf(a) * 29),
                        cy + static_cast<int>(sinf(a) * 29), color);
  }
}

void draw_radio_icon(int cx, int cy, uint16_t color) {
  M5.Display.drawRoundRect(cx - 28, cy - 20, 56, 42, 8, color);
  M5.Display.drawCircle(cx + 10, cy + 2, 10, color);
  M5.Display.drawFastHLine(cx - 18, cy - 9, 31, color);
  M5.Display.drawFastHLine(cx - 18, cy, 15, color);
  M5.Display.drawLine(cx - 22, cy - 22, cx + 20, cy - 38, color);
}

void draw_header() {
  M5.Display.fillRect(0, 0, 1280, kHeaderH, kBg);
  M5.Display.drawFastHLine(8, kHeaderH - 1, 1264, kCyan);
  const size_t badge_size = static_cast<size_t>(orc_badge_end - orc_badge_start);
  if (!M5.Display.drawPng(orc_badge_start, badge_size, 18, 13))
    M5.Display.drawRoundRect(18, 13, 104, 104, 18, kGreen);
  text("OrcSDR", 142, 38, TFT_WHITE, 4, middle_left);
  text("P25 Trunking", 142, 82, kCyan, 2, middle_left);
  M5.Display.drawFastVLine(365, 25, 82, kCyan);
  draw_radio_icon(456, 70, kCyan);
  text("P25 Trunking", 530, 66, TFT_WHITE, 4, middle_left);
  M5.Display.drawFastVLine(865, 25, 82, kCyan);
  audio_header::draw(g_audio_control, g_snapshot.volume, g_snapshot.sound_enabled,
                     g_snapshot.battery_percent);
  draw_gear(1220, 66, kCyan);
}

void draw_tab_icon(View view, int cx, int cy, uint16_t color) {
  if (view == View::monitor) draw_radio_icon(cx, cy, color);
  else if (view == View::spectrum) {
    for (int i = 0; i < 6; ++i)
      M5.Display.fillRect(cx - 24 + i * 9, cy + 14 - (i % 3 + 1) * 9, 5,
                          (i % 3 + 1) * 9, color);
  } else if (view == View::talkgroups) {
    for (int i = 0; i < 3; ++i) {
      M5.Display.drawCircle(cx - 19, cy - 15 + i * 15, 4, color);
      M5.Display.drawFastHLine(cx - 8, cy - 15 + i * 15, 34, color);
    }
  } else if (view == View::program) {
    M5.Display.drawRect(cx - 25, cy - 22, 50, 44, color);
    M5.Display.drawFastHLine(cx - 15, cy - 10, 30, color);
    M5.Display.drawFastHLine(cx - 15, cy, 30, color);
    M5.Display.drawFastHLine(cx - 15, cy + 10, 20, color);
  } else {
    const int16_t xs[] = {-28, -18, -10, -3, 5, 12, 20, 29};
    const int16_t ys[] = {0, 0, -18, 20, -24, 12, 0, 0};
    for (int i = 1; i < 8; ++i)
      M5.Display.drawLine(cx + xs[i - 1], cy + ys[i - 1], cx + xs[i], cy + ys[i], color);
  }
}

void draw_tabs() {
  static constexpr const char* names[] = {
      "MONITOR", "SPECTRUM", "TALKGROUPS", "PROGRAM", "RF HEALTH"};
  for (uint8_t i = 0; i < static_cast<uint8_t>(View::count); ++i) {
    const int x = i * kTabW;
    const bool selected = i == static_cast<uint8_t>(g_view);
    M5.Display.fillRect(x, kTabsY, kTabW, 90, selected ? 0x0a43 : kPanel);
    M5.Display.drawRect(x, kTabsY, kTabW, 90, selected ? kCyan : kGrid);
    draw_tab_icon(static_cast<View>(i), x + 52, kTabsY + 43,
                  selected ? (i == 0 ? kGreen : kCyan) : TFT_WHITE);
    text(names[i], x + 92, kTabsY + 45, selected ? kCyan : TFT_WHITE, 2, middle_left);
  }
}

void format_mhz(char* output, size_t size, uint32_t frequency_hz) {
  snprintf(output, size, "%.4f", frequency_hz / 1000000.0);
}

const char* talkgroup_alias(uint16_t id) {
  for (const auto& talkgroup : kTalkgroups)
    if (talkgroup.id == id) return talkgroup.alias;
  return "Unknown talkgroup";
}

bool grant_live(const p25decoder::Grant& grant) {
  return grant.valid && millis() - grant.seen_ms < 5000;
}

void draw_meter(int x, int y, int w, float dbfs, int segments = 18) {
  const float normalized = std::clamp((dbfs + 90.0f) / 70.0f, 0.0f, 1.0f);
  const int lit = static_cast<int>(normalized * segments);
  const int gap = 3;
  const int sw = (w - (segments - 1) * gap) / segments;
  for (int i = 0; i < segments; ++i) {
    const uint16_t color = i < lit ? (i >= segments - 4 ? kYellow : kGreen) : kGrid;
    M5.Display.fillRect(x + i * (sw + gap), y, sw, 28, color);
  }
}

void draw_monitor_static() {
  card(24, 150, 420, 118);
  label("CONTROL CHANNEL", 44, 166);
  card(458, 150, 798, 118);
  label("SYSTEM PROFILE", 478, 166);
  card(24, 282, 800, 205);
  label("CURRENT VOICE GRANT", 44, 298);
  card(838, 282, 418, 205);
  label("CONTROL SIGNAL", 858, 298);
  button(24, 505, 232, 92, "<  CHANNEL");
  button(270, 505, 232, 92, "CHANNEL  >");
  button(516, 505, 232, 92, "SURVEY", kGreen, g_snapshot.survey_active);
  button(762, 505, 232, 92, "HOLD", kYellow, g_snapshot.hold);
  button(1008, 505, 248, 92, "SKIP / NEXT");
}

void draw_monitor_dynamic() {
  char value[96];
  M5.Display.fillRect(40, 160, 390, 30, kPanel);
  label(g_snapshot.following_voice ? "VOICE CHANNEL" : "CONTROL CHANNEL", 44, 166);
  M5.Display.fillRect(42, 198, 384, 55, kPanel);
  format_mhz(value, sizeof(value), g_snapshot.frequency_hz);
  text(value, 52, 227, TFT_WHITE, 4, middle_left);
  text("MHz", 340, 230, TFT_WHITE, 2, middle_left);
  M5.Display.fillRect(476, 198, 760, 55, kPanel);
  text("SW7 / LRIG   Lane County Simulcast", 478, 215, TFT_WHITE, 3, middle_left);
  if (g_snapshot.decoded.identity_valid) {
    snprintf(value, sizeof(value), "DECODED: WACN %05lX  SYSID %03X  NAC %03X",
             static_cast<unsigned long>(g_snapshot.decoded.wacn),
             g_snapshot.decoded.system_id, g_snapshot.decoded.nac);
    text(value, 478, 246, kGreen, 1, middle_left);
  } else {
    text("PROFILE: WACN BEE00  SYSID 1F3  NAC 1F0", 478, 246, kMuted, 1, middle_left);
  }

  M5.Display.fillRect(42, 330, 764, 135, kPanel);
  const auto& grant = g_snapshot.decoded.current_grant;
  if (grant.valid) {
    text(g_snapshot.following_voice ? "FOLLOWING CLEAR PHASE I VOICE" :
         grant_live(grant) ? "LIVE CONTROL-CHANNEL GRANT" : "LAST GRANT — STALE",
         424, 362, (g_snapshot.following_voice || grant_live(grant)) ? kGreen : kYellow, 2);
    snprintf(value, sizeof(value), "TGID  %u     %s     SOURCE  %lu", grant.talkgroup,
             talkgroup_alias(grant.talkgroup), static_cast<unsigned long>(grant.source_id));
    text(value, 58, 410, TFT_WHITE, 2, middle_left);
    if (grant.frequency_hz)
      snprintf(value, sizeof(value), "VOICE  %.4f MHz     MODE  %s%s",
               grant.frequency_hz / 1000000.0, grant.tdma ? "PHASE II" : "PHASE I",
               grant.encrypted ? "  ENCRYPTED" : "  CLEAR");
    else
      snprintf(value, sizeof(value), "VOICE  AWAITING BAND PLAN     MODE  %s%s",
               grant.tdma ? "PHASE II" : "PHASE I",
               grant.encrypted ? "  ENCRYPTED" : "  CLEAR");
    text(value, 58, 447, grant.encrypted ? kRed : kGreen, 2, middle_left);
  } else {
    text(g_snapshot.decoded.frame_sync ? "P25 CONTROL CHANNEL LOCKED" :
         "SEARCHING FOR P25 CONTROL CHANNEL", 424, 362,
         g_snapshot.decoded.frame_sync ? kGreen : kYellow, 2);
    text("TGID  —     ALIAS  —     SOURCE  —", 58, 410, TFT_WHITE, 2, middle_left);
    text("VOICE FREQUENCY  —     MODE  —", 58, 447, kMuted, 2, middle_left);
  }

  M5.Display.fillRect(856, 330, 382, 135, kPanel);
  draw_meter(872, 350, 345, g_snapshot.relative_dbfs, 16);
  snprintf(value, sizeof(value), "RELATIVE  %.1f dBFS", static_cast<double>(g_snapshot.relative_dbfs));
  text(value, 872, 404, TFT_WHITE, 2, middle_left);
  text(g_snapshot.following_voice ? "IMBE VOICE DECODING" :
       g_snapshot.survey_active ? "SURVEYING KNOWN CHANNELS" :
       g_snapshot.decoded.frame_sync ? "P25 FRAME SYNC" : "NO P25 FRAME SYNC",
       872, 444, g_snapshot.following_voice ? kGreen : g_snapshot.survey_active ? kYellow :
       g_snapshot.decoded.frame_sync ? kGreen : kMuted, 1, middle_left);
}

void draw_spectrum_static() {
  label("CENTER FREQUENCY", 46, 153);
  card(510, 145, 250, 80);
  label("P25 FILTER BW", 535, 158);
  text("12.5 kHz", 635, 198, kGreen, 3);
  card(880, 145, 354, 80);
  label("RF ACTIVITY", 900, 158);
  M5.Display.drawRect(kSpectrumX, kSpectrumY, kSpectrumW, kSpectrumH, kGrid);
  for (int i = 1; i < 4; ++i) {
    M5.Display.drawFastVLine(kSpectrumX + i * kSpectrumW / 4, kSpectrumY, kSpectrumH, kGrid);
    M5.Display.drawFastHLine(kSpectrumX, kSpectrumY + i * kSpectrumH / 4, kSpectrumW, kGrid);
  }
  M5.Display.drawRect(kSpectrumX, kWaterfallY, kSpectrumW, kWaterfallH, kCyan);
  M5.Display.setScrollRect(kSpectrumX + 1, kWaterfallY + 1, kSpectrumW - 2,
                           kWaterfallH - 2);
  button(390, 565, 80, 45, "—");
  button(810, 565, 80, 45, "+");
  text("TAP SPECTRUM TO TUNE", 1050, 582, kCyan, 2);
}

void draw_spectrum_dynamic() {
  char value[64];
  M5.Display.fillRect(42, 180, 430, 55, kBg);
  format_mhz(value, sizeof(value), g_snapshot.frequency_hz);
  text(value, 48, 207, TFT_WHITE, 4, middle_left);
  text("MHz", 335, 210, TFT_WHITE, 2, middle_left);
  M5.Display.fillRect(898, 184, 316, 30, kPanel);
  draw_meter(902, 185, 306, g_snapshot.relative_dbfs, 14);
  snprintf(value, sizeof(value), "SPAN  %.3f MHz", g_snapshot.span_hz / 1000000.0);
  M5.Display.fillRect(46, 565, 300, 45, kBg);
  text(value, 52, 586, kCyan, 2, middle_left);
}

void draw_talkgroups_static() {
  card(24, 148, 1232, 86);
  label("PROGRAMMED SYSTEM", 44, 164);
  text("SW7 / LRIG — Lane County Simulcast", 44, 205, TFT_WHITE, 3, middle_left);
  M5.Display.drawFastVLine(770, 160, 62, kGrid);
  label("CURRENT TG", 798, 164);
  label("TGID", 44, 250);
  label("ALIAS", 190, 250);
  label("MODE", 690, 250);
  label("PRIORITY", 850, 250);
  label("STATUS", 1050, 250);
  for (int row = 0; row < 8; ++row) {
    const int y = 280 + row * 37;
    M5.Display.drawFastHLine(32, y + 30, 1216, kGrid);
    char id[8];
    snprintf(id, sizeof(id), "%u", kTalkgroups[row].id);
    text(id, 44, y + 14, TFT_WHITE, 2, middle_left);
    text(kTalkgroups[row].alias, 190, y + 14, TFT_WHITE, 2, middle_left);
    text(kTalkgroups[row].may_encrypt ? "MIXED" : "CLEAR", 690, y + 14,
         kTalkgroups[row].may_encrypt ? kYellow : kGreen, 2, middle_left);
    char priority[8];
    snprintf(priority, sizeof(priority), "P%u", kTalkgroups[row].priority);
    text(priority, 850, y + 14, kCyan, 2, middle_left);
  }
  text("LIVE DECODED ACTIVITY  •  TAP A ROW TO HOLD / RELEASE", 640, 602, kMuted, 1);
}

void draw_talkgroups_dynamic() {
  char current[80];
  M5.Display.fillRect(796, 194, 438, 30, kPanel);
  const auto& current_grant = g_snapshot.decoded.current_grant;
  if (current_grant.valid) {
    snprintf(current, sizeof(current), "%u  %s", current_grant.talkgroup,
             talkgroup_alias(current_grant.talkgroup));
    text(current, 798, 207, current_grant.encrypted ? kYellow : kGreen, 2, middle_left);
  } else {
    text("—", 798, 207, kMuted, 2, middle_left);
  }
  for (int row = 0; row < 8; ++row) {
    bool active = false;
    bool encrypted_now = false;
    for (const auto& grant : g_snapshot.decoded.recent_grants) {
      if (grant_live(grant) && grant.talkgroup == kTalkgroups[row].id) {
        active = true;
        encrypted_now |= grant.encrypted;
      }
    }
    const int y = 280 + row * 37;
    M5.Display.fillRect(1038, y, 190, 28, kBg);
    const bool held = g_snapshot.hold && g_snapshot.hold_talkgroup == kTalkgroups[row].id;
    text(held ? "HOLD" : encrypted_now ? "ENC SKIP" : active ? "ACTIVE" : "SCAN",
         1050, y + 14, held ? kYellow : encrypted_now ? kRed : active ? kGreen : kMuted,
         2, middle_left);
  }
}

void draw_program_static() {
  card(24, 148, 600, 174);
  label("SYSTEM / SITE", 44, 164);
  card(638, 148, 618, 174);
  label("CONTROL CHANNELS", 658, 164);
  for (size_t i = 0; i < std::size(kControlChannels); ++i) {
    char value[48];
    snprintf(value, sizeof(value), "%c %.4f MHz   %.1f dBFS",
             i == g_snapshot.candidate_index ? '>' : ' ',
             kControlChannels[i] / 1000000.0,
             static_cast<double>(g_snapshot.candidate_levels[i]));
    text(value, 660, 204 + static_cast<int>(i) * 27,
         i == g_snapshot.candidate_index ? kGreen : TFT_WHITE, 2, middle_left);
  }
  card(24, 338, 1232, 198);
  label("TRUNKING OPTIONS", 44, 354);
  button(44, 395, 270, 58, "AUTO FOLLOW", kGreen, g_snapshot.auto_follow);
  button(330, 395, 270, 58, "SKIP ENCRYPTED", kGreen, g_snapshot.encryption_skip);
  button(616, 395, 270, 58, "SURVEY", kCyan, g_snapshot.survey_active);
  button(902, 395, 330, 58, "DEVICE SETTINGS");
  button(44, 468, 556, 50, "IMPORT / EDIT: DEFERRED", kMuted);
  button(616, 468, 270, 50, "HOME / NAV", kYellow);
  button(902, 468, 330, 50, "PHASE I  •  12.5 kHz", kGreen, true);
  text("Single tuner: follow voice traffic, then return to the control channel.",
       640, 574, kMuted, 1);
}

void draw_program_dynamic() {
  char value[96];
  M5.Display.fillRect(42, 198, 560, 108, kPanel);
  text("SW7 / LRIG", 44, 207, TFT_WHITE, 3, middle_left);
  if (g_snapshot.decoded.identity_valid) {
    snprintf(value, sizeof(value), "WACN %05lX   SYSID %03X   NAC %03X",
             static_cast<unsigned long>(g_snapshot.decoded.wacn),
             g_snapshot.decoded.system_id, g_snapshot.decoded.nac);
    text(value, 44, 250, kGreen, 2, middle_left);
    snprintf(value, sizeof(value), "RFSS %u   SITE %u   Lane County Simulcast",
             g_snapshot.decoded.rfss, g_snapshot.decoded.site);
    text(value, 44, 287, TFT_WHITE, 2, middle_left);
  } else {
    text("PROFILE  BEE00 / 1F3 / 1F0 — AWAITING DECODE", 44, 250, kMuted, 2, middle_left);
    text("Lane County Simulcast", 44, 287, TFT_WHITE, 2, middle_left);
  }
}

void health_card(int x, int y, int w, int h, const char* title, const char* value,
                 bool healthy, bool available = true) {
  card(x, y, w, h);
  label(title, x + 18, y + 14);
  text(value, x + w / 2, y + h / 2 + 18,
       !available ? kMuted : healthy ? kGreen : kYellow, 3);
  M5.Display.drawCircle(x + w - 28, y + h - 26, 14,
                        !available ? kMuted : healthy ? kGreen : kYellow);
}

void draw_health_static() {
  card(24, 148, 390, 110);
  label("FREQUENCY", 44, 164);
  card(428, 148, 390, 110);
  label("STATUS", 448, 164);
  card(832, 148, 424, 110);
  label("SYSTEM", 852, 164);
  for (int row = 0; row < 2; ++row)
    for (int col = 0; col < 4; ++col)
      card(24 + col * 308, 274 + row * 126, 294, 112);
  card(24, 536, 280, 74);
  label("AUDIO UNDERRUNS", 44, 550);
  card(318, 536, 280, 74);
  label("WI-FI", 338, 550);
  card(612, 536, 644, 74);
  label("LAST ERROR", 632, 550);
}

void draw_health_dynamic() {
  char value[64];
  M5.Display.fillRect(42, 194, 354, 50, kPanel);
  snprintf(value, sizeof(value), "%.4f MHz", g_snapshot.frequency_hz / 1000000.0);
  text(value, 219, 219, TFT_WHITE, 3);
  M5.Display.fillRect(446, 194, 354, 50, kPanel);
  text(g_snapshot.following_voice ? "VOICE" : g_snapshot.survey_active ? "SURVEYING" :
       g_snapshot.decoded.frame_sync ? "P25 LOCK" : "SEARCHING", 623, 219,
       g_snapshot.following_voice ? kGreen : g_snapshot.survey_active ? kYellow :
       g_snapshot.decoded.frame_sync ? kGreen : kYellow, 3);
  M5.Display.fillRect(850, 194, 388, 50, kPanel);
  text("SW7 / LRIG", 1044, 219, TFT_WHITE, 3);

  snprintf(value, sizeof(value), "%.1f%%",
           g_snapshot.target_sps ? 100.0 * g_snapshot.effective_sps / g_snapshot.target_sps : 0.0);
  health_card(24, 274, 294, 112, "EFFECTIVE RATE", value,
              g_snapshot.effective_sps * 100ull >= g_snapshot.target_sps * 95ull);
  snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(g_snapshot.usb_overruns));
  health_card(332, 274, 294, 112, "USB OVERRUNS", value, g_snapshot.usb_overruns == 0);
  snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(g_snapshot.consumer_drops));
  health_card(640, 274, 294, 112, "CONSUMER DROPS", value, g_snapshot.consumer_drops == 0);
  snprintf(value, sizeof(value), "%.1f dBFS", static_cast<double>(g_snapshot.relative_dbfs));
  health_card(948, 274, 294, 112, "RELATIVE LEVEL", value, g_snapshot.relative_dbfs > -75.0f);
  snprintf(value, sizeof(value), "%lu%%", static_cast<unsigned long>(g_snapshot.dsp_percent));
  health_card(24, 400, 294, 112, "DSP MAX TIME", value, g_snapshot.dsp_percent < 80);
  if (g_snapshot.following_voice) {
    snprintf(value, sizeof(value), "%lu / %lu",
             static_cast<unsigned long>(g_snapshot.imbe_frames),
             static_cast<unsigned long>(g_snapshot.imbe_errors));
    health_card(332, 400, 294, 112, "IMBE FRAMES / ERRORS", value,
                g_snapshot.imbe_frames > 0 && g_snapshot.imbe_errors == 0,
                g_snapshot.imbe_frames > 0);
  } else {
    snprintf(value, sizeof(value), "%lu / %lu",
             static_cast<unsigned long>(g_snapshot.decoded.tsbk_good),
             static_cast<unsigned long>(g_snapshot.decoded.tsbk_failed));
    health_card(332, 400, 294, 112, "TSBK GOOD / BAD", value,
                g_snapshot.decoded.tsbk_good > 0 && g_snapshot.decoded.tsbk_failed == 0,
                g_snapshot.decoded.nid_good > 0);
  }
  snprintf(value, sizeof(value), "%.2f%%",
           static_cast<double>(g_snapshot.decoded.estimated_ber_percent));
  health_card(640, 400, 294, 112, "ESTIMATED BER", value,
              g_snapshot.decoded.estimated_ber_percent < 2.0f,
              g_snapshot.decoded.nid_good > 0);
  health_card(948, 400, 294, 112, "DRIVER STATE",
              g_snapshot.running ? "RUNNING" : g_snapshot.driver_ready ? "READY" : "OFFLINE",
              g_snapshot.running || g_snapshot.driver_ready);
  M5.Display.fillRect(178, 566, 108, 30, kPanel);
  snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(g_snapshot.audio_underruns));
  text(value, 232, 580, g_snapshot.audio_underruns == 0 ? kGreen : kYellow, 2);
  M5.Display.fillRect(404, 566, 176, 30, kPanel);
  text(g_snapshot.wifi_connected ? "CONNECTED" : "OFFLINE", 492, 580,
       g_snapshot.wifi_connected ? kGreen : kMuted, 2);
  M5.Display.fillRect(744, 566, 486, 30, kPanel);
  text(g_snapshot.last_error[0] ? g_snapshot.last_error : "—", 754, 580,
       g_snapshot.last_error[0] ? kYellow : TFT_WHITE, 2, middle_left);
}

void draw_view_static() {
  M5.Display.clearScrollRect();
  M5.Display.fillRect(0, kHeaderH, 1280, 720 - kHeaderH, kBg);
  switch (g_view) {
    case View::monitor: draw_monitor_static(); break;
    case View::spectrum: draw_spectrum_static(); break;
    case View::talkgroups: draw_talkgroups_static(); break;
    case View::program: draw_program_static(); break;
    case View::rf_health: draw_health_static(); break;
    default: break;
  }
  draw_tabs();
}

void draw_dynamic() {
  switch (g_view) {
    case View::monitor: draw_monitor_dynamic(); break;
    case View::spectrum: draw_spectrum_dynamic(); break;
    case View::talkgroups: draw_talkgroups_dynamic(); break;
    case View::program: draw_program_dynamic(); break;
    case View::rf_health: draw_health_dynamic(); break;
    default: break;
  }
}

uint16_t waterfall_color(float level) {
  level = std::clamp(level, 0.0f, 1.0f);
  const uint8_t r = level < 0.5f ? 0 : static_cast<uint8_t>((level - 0.5f) * 510);
  const uint8_t g = level < 0.25f ? 0 : static_cast<uint8_t>(std::min(255.0f, (level - 0.25f) * 510));
  const uint8_t b = level < 0.65f ? static_cast<uint8_t>((0.65f - level) * 390) : 0;
  return M5.Display.color565(r, g, b);
}

}  // namespace

void enter(const Snapshot& snapshot) {
  g_snapshot = snapshot;
  g_view = View::monitor;
  g_active = true;
  audio_header::reset(g_audio_control);
  draw();
}

void leave() {
  M5.Display.clearScrollRect();
  g_active = false;
}

void draw() {
  if (!g_active) return;
  M5.Display.fillScreen(kBg);
  draw_header();
  draw_view_static();
  draw_dynamic();
}

void update(const Snapshot& snapshot) {
  if (!g_active) return;
  const bool header_changed = snapshot.battery_percent != g_snapshot.battery_percent ||
                              snapshot.volume != g_snapshot.volume ||
                              snapshot.sound_enabled != g_snapshot.sound_enabled;
  const bool controls_changed =
      snapshot.survey_active != g_snapshot.survey_active ||
      snapshot.hold != g_snapshot.hold ||
      snapshot.hold_talkgroup != g_snapshot.hold_talkgroup ||
      snapshot.auto_follow != g_snapshot.auto_follow ||
      snapshot.encryption_skip != g_snapshot.encryption_skip ||
      snapshot.following_voice != g_snapshot.following_voice ||
      snapshot.candidate_index != g_snapshot.candidate_index ||
      memcmp(snapshot.candidate_levels, g_snapshot.candidate_levels,
             sizeof(snapshot.candidate_levels)) != 0;
  const bool decoded_changed =
      snapshot.decoded.tsbk_good != g_snapshot.decoded.tsbk_good ||
      snapshot.decoded.tsbk_failed != g_snapshot.decoded.tsbk_failed ||
      snapshot.imbe_frames != g_snapshot.imbe_frames ||
      snapshot.imbe_errors != g_snapshot.imbe_errors ||
      snapshot.decoded.identity_valid != g_snapshot.decoded.identity_valid ||
      snapshot.decoded.current_grant.seen_ms != g_snapshot.decoded.current_grant.seen_ms;
  g_snapshot = snapshot;
  const uint32_t now = millis();
  if (header_changed || audio_header::service_timeout(g_audio_control, now))
    audio_header::draw(g_audio_control, g_snapshot.volume, g_snapshot.sound_enabled,
                       g_snapshot.battery_percent);
  if (now - g_last_dynamic_ms < 200) return;
  g_last_dynamic_ms = now;
  if (controls_changed && (g_view == View::monitor || g_view == View::program)) {
    draw_view_static();
  }
  if ((g_view != View::talkgroups && g_view != View::program) || decoded_changed ||
      controls_changed) draw_dynamic();
}

void draw_spectrum(const float* levels, size_t first_bin, size_t visible_bins, float floor) {
  if (!spectrum_active() || levels == nullptr || visible_bins < 2) return;
  M5.Display.startWrite();
  M5.Display.fillRect(kSpectrumX + 1, kSpectrumY + 1, kSpectrumW - 2, kSpectrumH - 2, kBg);
  for (int i = 1; i < 4; ++i) {
    M5.Display.drawFastVLine(kSpectrumX + i * kSpectrumW / 4, kSpectrumY, kSpectrumH, kGrid);
    M5.Display.drawFastHLine(kSpectrumX, kSpectrumY + i * kSpectrumH / 4, kSpectrumW, kGrid);
  }
  int px = kSpectrumX;
  int py = kSpectrumY + kSpectrumH - 2;
  for (size_t i = 0; i < visible_bins; ++i) {
    const float normalized = std::clamp((levels[first_bin + i] - floor) / 48.0f, 0.0f, 1.0f);
    const int x = kSpectrumX + static_cast<int>(i * (kSpectrumW - 1) / (visible_bins - 1));
    const int y = kSpectrumY + kSpectrumH - 2 - static_cast<int>(normalized * (kSpectrumH - 4));
    if (i) M5.Display.drawLine(px, py, x, y, kGreen);
    px = x;
    py = y;
    const int x0 = static_cast<int>(i * kSpectrumW / visible_bins);
    const int x1 = static_cast<int>((i + 1) * kSpectrumW / visible_bins);
    const uint16_t color = waterfall_color(normalized);
    for (int p = x0; p < x1; ++p) g_waterfall_row[p] = color;
  }
  const int center = kSpectrumX + kSpectrumW / 2;
  const int half_filter = std::max(3, static_cast<int>(12500ull * kSpectrumW /
                                                       (2ull * std::max<uint32_t>(1, g_snapshot.span_hz))));
  M5.Display.drawFastVLine(center, kSpectrumY, kSpectrumH, kCyan);
  M5.Display.drawFastVLine(center - half_filter, kSpectrumY, kSpectrumH, kCyan);
  M5.Display.drawFastVLine(center + half_filter, kSpectrumY, kSpectrumH, kCyan);
  M5.Display.scroll(0, -1);
  M5.Display.pushImage(kSpectrumX, kWaterfallY + kWaterfallH - 2, kSpectrumW, 1,
                       g_waterfall_row);
  M5.Display.drawFastVLine(center, kWaterfallY, kWaterfallH, kCyan);
  M5.Display.endWrite();
}

Action handle_touch(int32_t x, int32_t y) {
  if (!g_active) return {};
  const auto audio_action = audio_header::handle_touch(g_audio_control, x, y, millis());
  if (audio_action != audio_header::Action::none) {
    if (audio_action == audio_header::Action::opened ||
        audio_action == audio_header::Action::closed) {
      audio_header::draw(g_audio_control, g_snapshot.volume, g_snapshot.sound_enabled,
                         g_snapshot.battery_percent);
      return {};
    }
    if (audio_action == audio_header::Action::volume_down)
      return {ActionKind::volume_down};
    if (audio_action == audio_header::Action::sound_toggle)
      return {ActionKind::sound_toggle};
    return {ActionKind::volume_up};
  }
  if (hit(x, y, 1180, 25, 80, 82)) return {ActionKind::open_device_settings};
  if (y >= kTabsY) {
    const uint8_t next = std::min<uint8_t>(x / kTabW, static_cast<uint8_t>(View::count) - 1);
    if (next != static_cast<uint8_t>(g_view)) {
      g_view = static_cast<View>(next);
      draw();
    }
    return {};
  }
  if (g_view == View::monitor && y >= 505 && y < 597) {
    if (x < 256) return {ActionKind::previous_candidate};
    if (x < 502) return {ActionKind::next_candidate};
    if (x < 748) return {ActionKind::survey_toggle};
    if (x < 994) return {ActionKind::hold_toggle};
    return {ActionKind::skip_talkgroup};
  }
  if (g_view == View::spectrum) {
    if (hit(x, y, 390, 565, 80, 45)) return {ActionKind::span_down};
    if (hit(x, y, 810, 565, 80, 45)) return {ActionKind::span_up};
    if (hit(x, y, kSpectrumX, kSpectrumY, kSpectrumW, kSpectrumH)) {
      const int64_t offset = static_cast<int64_t>(x - (kSpectrumX + kSpectrumW / 2)) *
                             g_snapshot.span_hz / kSpectrumW;
      const int64_t selected = static_cast<int64_t>(g_snapshot.frequency_hz) + offset;
      return {ActionKind::tune_hz,
              static_cast<uint32_t>(std::clamp<int64_t>(selected, kP25MinHz, kP25MaxHz))};
    }
  }
  if (g_view == View::talkgroups && y >= 280 && y < 576) {
    const size_t row = static_cast<size_t>((y - 280) / 37);
    if (row < std::size(kTalkgroups))
      return {ActionKind::hold_talkgroup, kTalkgroups[row].id};
  }
  if (g_view == View::program) {
    if (hit(x, y, 44, 395, 270, 58)) return {ActionKind::auto_follow_toggle};
    if (hit(x, y, 330, 395, 270, 58)) return {ActionKind::encryption_skip_toggle};
    if (hit(x, y, 616, 395, 270, 58)) return {ActionKind::survey_toggle};
    if (hit(x, y, 902, 395, 330, 58)) return {ActionKind::open_device_settings};
    if (hit(x, y, 616, 468, 270, 50)) return {ActionKind::exit_to_home};
  }
  return {};
}

bool active() { return g_active; }
bool spectrum_active() { return g_active && g_view == View::spectrum; }
View view() { return g_view; }

void show_documentation_view(View requested, const Snapshot& snapshot,
                             bool show_volume_tray) {
  if (requested >= View::count) return;
  g_snapshot = snapshot;
  g_view = requested;
  g_active = true;
  audio_header::reset(g_audio_control);
  if (show_volume_tray) {
    g_audio_control.expanded = true;
    g_audio_control.hide_at_ms = UINT32_MAX;
  }
  draw();
}

bool self_check() {
  if (static_cast<uint8_t>(View::count) != 5 || kSpectrumX + kSpectrumW > 1280 ||
      kTabsY >= 720 || std::size(kTalkgroups) != 8) return false;
  for (size_t i = 0; i < std::size(kControlChannels); ++i) {
    if (kControlChannels[i] < kP25MinHz || kControlChannels[i] > kP25MaxHz) return false;
    for (size_t j = i + 1; j < std::size(kControlChannels); ++j)
      if (kControlChannels[i] == kControlChannels[j]) return false;
  }
  for (size_t i = 0; i < std::size(kTalkgroups); ++i)
    for (size_t j = i + 1; j < std::size(kTalkgroups); ++j)
      if (kTalkgroups[i].id == kTalkgroups[j].id) return false;
  return audio_header::self_check();
}

}  // namespace orcsdr::p25
