#include "focus_nav.hpp"
#include "p25_dashboard.hpp"
#include "p25_dashboard_layout.hpp"

#include "dashboard_audio_control.hpp"
#include "orc_badge.hpp"
#include "text_editor.hpp"

#include "scope_canvas.hpp"
#include "waterfall_style.hpp"
#include <M5Unified.h>
#include <esp_attr.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace orcsdr::p25 {
namespace {

constexpr uint16_t kBg = TFT_BLACK;
constexpr uint16_t kPanel = 0x0841;
constexpr uint16_t kCyan = 0x2e7f;
constexpr uint16_t kGreen = 0x6fe8;
constexpr uint16_t kYellow = 0xff24;
constexpr uint16_t kRed = 0xf9a7;
constexpr uint16_t kMuted = 0x8c71;
constexpr uint16_t kGrid = 0x2945;
constexpr int kHeaderH = 132;
constexpr int kSpectrumW = 1188;
constexpr uint32_t kP25MinHz = 24000000;
constexpr uint32_t kP25MaxHz = 1766000000;

static_assert(static_cast<uint8_t>(View::count) == 5);

Snapshot g_snapshot{};
View g_view = View::monitor;
bool g_active = false;
audio_header::Control g_audio_control{};
uint32_t g_last_dynamic_ms = 0;
EXT_RAM_BSS_ATTR uint16_t g_waterfall_row[kSpectrumW]{};
uint8_t g_profile_cursor = 0;
uint8_t g_talk_page = 0;
uint8_t g_program_candidate_page = 0;
bool g_program_details_page = false;
bool g_delete_armed = false;
char g_profile_name[48]{};
bool g_profile_name_ready = false;
bool g_update_pending = false;
layout::Layout g_layout = layout::make_layout({0, 0, 1280, 720});

void draw_tabs();

bool hit(int32_t x, int32_t y, int bx, int by, int bw, int bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

bool hit(int32_t x, int32_t y, layout::Rect r) {
  return hit(x, y, r.x, r.y, r.w, r.h);
}

void text(const char* value, int x, int y, uint16_t color = TFT_WHITE,
          int size = 2, textdatum_t datum = middle_center) {
  M5.Display.setTextDatum(datum);
  M5.Display.setTextSize(size);
  M5.Display.setTextColor(color);
  M5.Display.drawString(value, x, y);
}

void card(int x, int y, int w, int h, uint16_t border = kCyan) {
  M5.Display.fillRoundRect(x, y, w, h, 12, kPanel);
  M5.Display.drawRoundRect(x, y, w, h, 12, border);
}

void button(int x, int y, int w, int h, const char* title, uint16_t color = kCyan,
            bool selected = false) {
  orcsdr::focus_nav::note(x, y, w, h);
  M5.Display.fillRoundRect(x, y, w, h, 9, selected ? 0x1264 : kPanel);
  M5.Display.drawRoundRect(x, y, w, h, 9, color);
  text(title, x + w / 2, y + h / 2, selected ? color : TFT_WHITE, 2);
}

void draw_radio_icon(int cx, int cy, uint16_t color) {
  M5.Display.drawRoundRect(cx - 28, cy - 20, 56, 42, 8, color);
  M5.Display.drawCircle(cx + 10, cy + 2, 10, color);
  M5.Display.drawFastHLine(cx - 18, cy - 9, 31, color);
  M5.Display.drawFastHLine(cx - 18, cy, 15, color);
  M5.Display.drawLine(cx - 22, cy - 22, cx + 20, cy - 38, color);
}

void draw_header() {
  if (!g_layout.legacy) {
    audio_header::draw_brand("P25 TRUNKING");
    audio_header::draw(g_audio_control, g_snapshot.volume, g_snapshot.sound_enabled,
                       g_snapshot.battery_percent);
    audio_header::draw_home_button();
    audio_header::draw_mute_button(g_snapshot.sound_enabled);
    audio_header::draw_visualizer_button(g_snapshot.running);
    audio_header::draw_settings_button();
    return;
  }
  M5.Display.fillRect(0, 0, 1280, kHeaderH, kBg);
  M5.Display.drawFastHLine(8, kHeaderH - 1, 1264, kCyan);
  audio_header::draw_brand("P25 TRUNKING");
  M5.Display.drawFastVLine(365, 25, 82, kCyan);
  draw_radio_icon(456, 70, kCyan);
  text("P25 Trunking", 530, 66, TFT_WHITE, 4, middle_left);
  M5.Display.drawFastVLine(865, 25, 82, kCyan);
  audio_header::draw(g_audio_control, g_snapshot.volume, g_snapshot.sound_enabled,
                     g_snapshot.battery_percent);
  audio_header::draw_home_button();
  audio_header::draw_mute_button(g_snapshot.sound_enabled);
  audio_header::draw_visualizer_button(g_snapshot.running);
  audio_header::draw_settings_button();
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
  static constexpr const char* names[] = {"MONITOR", "SPECTRUM", "TALKGROUPS", "SYSTEMS", "RF HEALTH"};
  for (uint8_t i = 0; i < static_cast<uint8_t>(View::count); ++i) {
    const auto r = g_layout.tab[i];
    const bool selected = i == static_cast<uint8_t>(g_view);
    focus_nav::note(r.x, r.y, r.w, r.h);
    if (g_layout.legacy) {
      M5.Display.fillRect(r.x, r.y, r.w, r.h, selected ? 0x0a43 : kPanel);
      M5.Display.drawRect(r.x, r.y, r.w, r.h, selected ? kCyan : kGrid);
      draw_tab_icon(static_cast<View>(i), r.x + 52, r.y + r.h / 2 - 2,
                    selected ? (i == 0 ? kGreen : kCyan) : TFT_WHITE);
      text(names[i], r.x + 92, r.y + r.h / 2, selected ? kCyan : TFT_WHITE, 2, middle_left);
    } else {
      M5.Display.fillRoundRect(r.x, r.y, r.w, r.h, 7, selected ? 0x0a43 : kPanel);
      M5.Display.drawRoundRect(r.x, r.y, r.w, r.h, 7, selected ? kCyan : kGrid);
      text(names[i], r.x + r.w / 2, r.y + r.h / 2, selected ? kCyan : TFT_WHITE, 2);
    }
  }
}

const char* talkgroup_alias(uint16_t id) {
  for (size_t i = 0; i < g_snapshot.config.talkgroup_count; ++i)
    if (g_snapshot.config.talkgroups[i].id == id) return g_snapshot.config.talkgroups[i].alias;
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

void text_block(layout::Rect r, const char* value, uint16_t color = TFT_WHITE,
                int size = 2) {
  if (r.w <= 0 || r.h <= 0 || value == nullptr) return;
  M5.Display.setClipRect(r.x, r.y, r.w, r.h);
  M5.Display.setTextDatum(top_left);
  M5.Display.setTextSize(size);
  M5.Display.setTextColor(color);
  const int line_h = std::max(16, size * 8);
  const int max_chars = std::max(1, r.w / std::max(6, size * 7));
  int line = 0;
  const char* cursor = value;
  while (*cursor && (line + 1) * line_h <= r.h) {
    char out[128]{};
    size_t used = 0;
    const char* last_space = nullptr;
    while (cursor[used] && cursor[used] != '\n' && used < static_cast<size_t>(max_chars) && used + 1 < sizeof(out)) {
      out[used] = cursor[used];
      if (out[used] == ' ') last_space = cursor + used;
      ++used;
    }
    if (cursor[used] && cursor[used] != '\n' && last_space != nullptr && last_space > cursor) {
      used = static_cast<size_t>(last_space - cursor);
      while (used && cursor[used - 1] == ' ') --used;
    }
    out[used] = 0;
    M5.Display.drawString(out, r.x, r.y + line * line_h);
    cursor += used;
    while (*cursor == ' ') ++cursor;
    if (*cursor == '\n') ++cursor;
    ++line;
  }
  M5.Display.clearClipRect();
}

void draw_label_block(const char* value, layout::Rect r) {
  text_block(r, value, kCyan, 2);
}

void draw_value_block(layout::Rect r, const char* value, uint16_t color = TFT_WHITE,
                      int size = 2) {
  M5.Display.fillRect(r.x, r.y, r.w, r.h, kPanel);
  text_block({r.x + 3, r.y + 1, std::max(0, r.w - 6), std::max(0, r.h - 2)}, value, color, size);
}

void draw_card_block(layout::Rect r, const char* title) {
  card(r.x, r.y, r.w, r.h);
  if (title && *title) draw_label_block(title, {r.x + 10, r.y + 6, r.w - 20, 24});
}

void format_hold_label(char* value, size_t size) {
  if (g_snapshot.hold_talkgroup)
    snprintf(value, size, "HOLD %u", g_snapshot.hold_talkgroup);
  else
    strlcpy(value, g_snapshot.hold ? "RELEASE HOLD" : "HOLD NEXT", size);
}

void draw_monitor_static() {
  draw_card_block(g_layout.monitor_control, "CONTROL CHANNEL");
  draw_card_block(g_layout.monitor_profile, "SYSTEM PROFILE");
  draw_card_block(g_layout.monitor_grant, "CURRENT VOICE GRANT");
  draw_card_block(g_layout.monitor_signal, "CONTROL SIGNAL");
  char hold_label[24]{};
  format_hold_label(hold_label, sizeof(hold_label));
  const char* names[] = {"< CHANNEL", "CHANNEL >", g_snapshot.survey_active ? "STOP SURVEY" : "SURVEY",
                         hold_label, "SKIP / NEXT"};
  for (int i = 0; i < 5; ++i)
    button(g_layout.monitor_actions[i].x, g_layout.monitor_actions[i].y,
           g_layout.monitor_actions[i].w, g_layout.monitor_actions[i].h, names[i],
           i == 2 ? kGreen : i == 3 ? kYellow : kCyan,
           i == 2 ? g_snapshot.survey_active : i == 3 && g_snapshot.hold);
}

void draw_monitor_dynamic() {
  char value[160]{};
  auto r = g_layout.monitor_control;
  snprintf(value, sizeof(value), "%s\n%.4f MHz",
           g_snapshot.following_voice ? "VOICE CHANNEL" :
           g_snapshot.probing_phase2 ? "PHASE II PROBE" : "CONTROL CHANNEL",
           g_snapshot.frequency_hz / 1000000.0);
  draw_value_block({r.x + 8, r.y + 30, r.w - 16, r.h - 36}, value, TFT_WHITE);
  r = g_layout.monitor_profile;
  draw_value_block({r.x + 8, r.y + 30, r.w - 16, r.h - 36}, g_snapshot.config.system_name, TFT_WHITE, 2);
  r = g_layout.monitor_grant;
  M5.Display.fillRect(r.x + 6, r.y + 30, r.w - 12, r.h - 36, kPanel);
  const auto& grant = g_snapshot.decoded.current_grant;
  if (!grant.valid) {
    char identity[96]{};
    if (g_snapshot.decoded.identity_valid) {
      snprintf(identity, sizeof(identity), "SYSTEM WACN %05lX  SYSID %03X  NAC %03X",
               static_cast<unsigned long>(g_snapshot.decoded.wacn),
               g_snapshot.decoded.system_id, g_snapshot.decoded.nac);
    } else {
      strlcpy(identity, "AWAITING OVER-THE-AIR SYSTEM IDENTITY", sizeof(identity));
    }
    snprintf(value, sizeof(value), "%s\n%s",
             g_snapshot.config.control_channel_count == 0 ? "NO P25 SYSTEM CONFIGURED" :
             g_snapshot.survey_active ? "SURVEYING KNOWN CONTROL CHANNELS" :
             g_snapshot.decoded.frame_sync ? "P25 CONTROL CHANNEL LOCKED" : "ACQUIRING CONFIGURED CONTROL CHANNEL",
             identity);
    text_block({r.x + 12, r.y + 32, r.w - 24, r.h - 40},
               value,
               g_snapshot.decoded.frame_sync ? kGreen : kYellow, 2);
  } else {
    char voice_line[96]{};
    if (g_snapshot.voice_encrypted)
      snprintf(voice_line, sizeof(voice_line), "VOICE MUTED  ALGID %02X  KID %04X",
               g_snapshot.voice_algorithm_id, g_snapshot.voice_key_id);
    else if (grant.frequency_hz)
      snprintf(voice_line, sizeof(voice_line), "VOICE %.4f MHz  MODE %s%s",
               grant.frequency_hz / 1000000.0, grant.tdma ? "PHASE II" : "PHASE I",
               grant.encrypted ? "  ENCRYPTED" : "  CLEAR");
    else strlcpy(voice_line, "VOICE FREQUENCY AWAITING BAND PLAN", sizeof(voice_line));
    char identity[72]{};
    snprintf(identity, sizeof(identity), "WACN %05lX  SYSID %03X  NAC %03X",
             static_cast<unsigned long>(g_snapshot.decoded.wacn),
             g_snapshot.decoded.system_id, g_snapshot.decoded.nac);
    snprintf(value, sizeof(value), "%s\nTGID %u  %s\nSOURCE %lu\n%s\n%s",
             g_snapshot.probing_phase2 ? "PHASE II CALL DETECTED — CHECKING TRAFFIC" :
             g_snapshot.voice_encrypted ? "ENCRYPTED PHASE I VOICE MUTED" :
             g_snapshot.following_voice ? "FOLLOWING CLEAR PHASE I VOICE" :
             grant_live(grant) ? "LIVE CONTROL-CHANNEL GRANT" : "LAST GRANT — STALE",
             grant.talkgroup, talkgroup_alias(grant.talkgroup), static_cast<unsigned long>(grant.source_id),
             voice_line, identity);
    text_block({r.x + 12, r.y + 32, r.w - 24, r.h - 40}, value,
               g_snapshot.voice_encrypted ? kRed : kGreen, 2);
  }
  r = g_layout.monitor_signal;
  M5.Display.fillRect(r.x + 6, r.y + 30, r.w - 12, r.h - 36, kPanel);
  draw_meter(r.x + 12, r.y + 34, std::max(40, r.w - 24), g_snapshot.relative_dbfs, 12);
  snprintf(value, sizeof(value), "%.1f dBFS  %s",
           static_cast<double>(g_snapshot.relative_dbfs),
           p25core::modulation_name(g_snapshot.decoded.selected_modulation));
  text_block({r.x + 10, r.y + 62, r.w - 20, std::max(20, r.h - 66)}, value, TFT_WHITE, 2);
}

void draw_spectrum_static() {
  draw_card_block(g_layout.spectrum_frequency, nullptr);
  draw_card_block(g_layout.spectrum_filter, nullptr);
  draw_card_block(g_layout.spectrum_activity, nullptr);
  if (g_layout.legacy) {
    draw_label_block("CENTER FREQUENCY", g_layout.spectrum_frequency_label);
    draw_label_block("P25 FILTER BW", g_layout.spectrum_filter_label);
    draw_label_block("RF ACTIVITY", g_layout.spectrum_activity_label);
  }
  const auto p = g_layout.spectrum_plot, w = g_layout.waterfall;
  M5.Display.drawRect(p.x, p.y, p.w, p.h, kGrid);
  for (int i = 1; i < 4; ++i) {
    M5.Display.drawFastVLine(p.x + i * p.w / 4, p.y, p.h, kGrid);
    M5.Display.drawFastHLine(p.x, p.y + i * p.h / 4, p.w, kGrid);
  }
  M5.Display.drawRect(w.x, w.y, w.w, w.h, kCyan);
  M5.Display.setScrollRect(w.x + 1, w.y + 1, w.w - 2, w.h - 2);
  button(g_layout.span_down.x, g_layout.span_down.y, g_layout.span_down.w, g_layout.span_down.h, "−");
  button(g_layout.span_up.x, g_layout.span_up.y, g_layout.span_up.w, g_layout.span_up.h, "+");
  if (g_layout.legacy) text_block(g_layout.spectrum_hint, "TAP SPECTRUM TO TUNE", kCyan, 2);
  else text_block(g_layout.spectrum_hint, "TAP PLOT TO TUNE", kCyan, 2);
}

void draw_spectrum_dynamic() {
  char value[64]{};
  if (g_layout.legacy) {
    const auto frequency = g_layout.spectrum_frequency;
    M5.Display.fillRect(frequency.x, frequency.y, frequency.w, frequency.h, kBg);
    snprintf(value, sizeof(value), "%.4f", g_snapshot.frequency_hz / 1000000.0);
    text(value, frequency.x + 6, frequency.y + frequency.h / 2, TFT_WHITE, 4, middle_left);
    text("MHz", frequency.x + frequency.w - 95, frequency.y + 30, TFT_WHITE, 2, middle_left);
    text("12.5 kHz", g_layout.spectrum_filter.x + g_layout.spectrum_filter.w / 2,
         g_layout.spectrum_filter.y + g_layout.spectrum_filter.h * 2 / 3, kGreen, 3);
    const auto activity = g_layout.spectrum_activity;
    draw_meter(activity.x + 22, activity.y + 40, activity.w - 44, g_snapshot.relative_dbfs, 14);
    snprintf(value, sizeof(value), "SPAN  %.3f MHz", g_snapshot.span_hz / 1000000.0);
    draw_value_block(g_layout.spectrum_span, value, kCyan, 2);
  } else {
    snprintf(value, sizeof(value), "CENTER %.4f MHz", g_snapshot.frequency_hz / 1000000.0);
    draw_value_block(g_layout.spectrum_frequency, value, TFT_WHITE, 2);
    draw_value_block(g_layout.spectrum_filter, "FILTER 12.5 kHz", kGreen, 2);
    snprintf(value, sizeof(value), "RF ACTIVITY  %.1f dBFS", static_cast<double>(g_snapshot.relative_dbfs));
    draw_value_block(g_layout.spectrum_activity, value, TFT_WHITE, 2);
    snprintf(value, sizeof(value), "SPAN %.3f MHz  •  TAP PLOT TO TUNE", g_snapshot.span_hz / 1000000.0);
    draw_value_block(g_layout.spectrum_hint, value, kCyan, 2);
  }
}

void draw_talkgroups_static() {
  draw_card_block(g_layout.talk_summary, nullptr);
  draw_card_block(g_layout.talk_current, nullptr);
  if (g_layout.legacy) {
    draw_label_block("PROGRAMMED SYSTEM", g_layout.talk_summary_label);
    draw_label_block("CURRENT TG", g_layout.talk_current_label);
    static constexpr const char* columns[] = {"TGID", "ALIAS", "MODE", "PRIORITY", "STATUS"};
    for (int i = 0; i < 5; ++i) draw_label_block(columns[i], g_layout.talk_column_labels[i]);
    text_block(g_layout.talk_hint, "LIVE DECODED ACTIVITY  •  TAP A ROW TO HOLD / RELEASE", kMuted, 2);
  }
  for (int i = 0; i < g_layout.talk_row_count; ++i) {
    const auto r = g_layout.talk_rows[i];
    card(r.x, r.y, r.w, r.h, kGrid);
    focus_nav::note(r.x, r.y, r.w, r.h);
  }
  if (!g_layout.legacy) {
    button(g_layout.talk_previous.x, g_layout.talk_previous.y, g_layout.talk_previous.w, g_layout.talk_previous.h, "<");
    button(g_layout.talk_next.x, g_layout.talk_next.y, g_layout.talk_next.w, g_layout.talk_next.h, ">");
  }
}

void draw_talkgroups_dynamic() {
  char value[160]{};
  const int talk_pages = std::max(1, (static_cast<int>(g_snapshot.config.talkgroup_count) + g_layout.talk_row_count - 1) / g_layout.talk_row_count);
  if (g_talk_page >= talk_pages) g_talk_page = static_cast<uint8_t>(talk_pages - 1);
  snprintf(value, sizeof(value), g_layout.legacy ? "%s" : "SYSTEM: %s", g_snapshot.config.system_name);
  draw_value_block(g_layout.legacy ? g_layout.talk_summary_value : g_layout.talk_summary, value, TFT_WHITE, 2);
  const auto& grant = g_snapshot.decoded.current_grant;
  if (grant.valid) snprintf(value, sizeof(value), "CURRENT TG %u\n%s", grant.talkgroup, talkgroup_alias(grant.talkgroup));
  else strlcpy(value, "No active talkgroup", sizeof(value));
  if (g_layout.legacy) {
    const auto r = g_layout.talk_current_value;
    M5.Display.fillRect(r.x, r.y, r.w, r.h, kPanel);
    text_block(r, value,
               grant.valid ? (grant.encrypted ? kYellow : kGreen) : kMuted, 2);
  } else {
    draw_value_block(g_layout.talk_current, value,
                     grant.valid ? (grant.encrypted ? kYellow : kGreen) : kMuted, 2);
  }
  const int first = g_talk_page * g_layout.talk_row_count;
  for (int row = 0; row < g_layout.talk_row_count; ++row) {
    const int index = first + row;
    const auto r = g_layout.talk_rows[row];
    M5.Display.fillRect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, kBg);
    if (index >= static_cast<int>(g_snapshot.config.talkgroup_count)) continue;
    const auto& tg = g_snapshot.config.talkgroups[index];
    bool active = false, encrypted = false;
    for (const auto& recent : g_snapshot.decoded.recent_grants) {
      if (grant_live(recent) && recent.talkgroup == tg.id) { active = true; encrypted |= recent.encrypted; }
    }
    const bool held = g_snapshot.hold && g_snapshot.hold_talkgroup == tg.id;
    snprintf(value, sizeof(value), g_layout.legacy ? "TG %u  %s  USER  —  %s" : "TG %u  %s  USER  %s", tg.id, tg.alias,
             held ? "HOLD" : encrypted ? "ENCRYPTED" : active ? "ACTIVE" : "SCAN");
    text_block({r.x + 8, r.y + 4, r.w - 16, r.h - 8}, value,
               held ? kYellow : encrypted ? kRed : active ? kGreen : TFT_WHITE, 2);
  }
  if (!g_layout.legacy) {
    snprintf(value, sizeof(value), "PAGE %u  •  %u TALKGROUPS", unsigned(g_talk_page + 1),
             unsigned(g_snapshot.config.talkgroup_count));
    draw_value_block(g_layout.talk_page, value, kMuted, 2);
  }
}

void draw_program_profiles_static() {
  for (int i = 0; i < g_layout.profile_row_count; ++i) {
    const auto r = g_layout.profile_rows[i];
    card(r.x, r.y, r.w, r.h, kGrid);
    focus_nav::note(r.x, r.y, r.w, r.h);
  }
  if (g_layout.legacy) {
    text("PREV", g_layout.profile_previous.x + g_layout.profile_previous.w / 2,
         g_layout.profile_previous.y + g_layout.profile_previous.h / 2, kCyan, 1);
    text("NEXT", g_layout.profile_next.x + g_layout.profile_next.w / 2,
         g_layout.profile_next.y + g_layout.profile_next.h / 2, kCyan, 1);
  } else {
    button(g_layout.profile_previous.x, g_layout.profile_previous.y,
           g_layout.profile_previous.w, g_layout.profile_previous.h, "<");
    button(g_layout.profile_next.x, g_layout.profile_next.y,
           g_layout.profile_next.w, g_layout.profile_next.h, ">");
  }
  if (g_layout.legacy) {
    const char* names[] = {"SELECT", "RENAME", "IMPORT", "EXPORT", "DELETE", "RELOAD"};
    for (int i = 0; i < 6; ++i) {
      const auto r = g_layout.program_controls[i];
      button(r.x, r.y, r.w, r.h, i == 4 && g_delete_armed ? "CONFIRM" : names[i], i == 4 ? kRed : kCyan);
    }
  }
}

void draw_program_details_static() {
  for (int i = 0; i < g_layout.candidate_row_count; ++i) {
    const auto r = g_layout.program_candidates[i];
    card(r.x, r.y, r.w, r.h, kGrid);
  }
  if (!g_layout.legacy) {
    button(g_layout.program_candidate_previous.x, g_layout.program_candidate_previous.y,
           g_layout.program_candidate_previous.w, g_layout.program_candidate_previous.h, "<");
    button(g_layout.program_candidate_next.x, g_layout.program_candidate_next.y,
           g_layout.program_candidate_next.w, g_layout.program_candidate_next.h, ">");
  }
  button(g_layout.policy_controls[0].x, g_layout.policy_controls[0].y,
         g_layout.policy_controls[0].w, g_layout.policy_controls[0].h,
         "AUTO FOLLOW", kGreen, g_snapshot.auto_follow);
  button(g_layout.policy_controls[1].x, g_layout.policy_controls[1].y,
         g_layout.policy_controls[1].w, g_layout.policy_controls[1].h,
         "SKIP ENCRYPTED", kGreen, g_snapshot.encryption_skip);
}

void draw_program_static() {
  if (g_layout.legacy) {
    draw_card_block(g_layout.program_profiles, nullptr);
    draw_card_block(g_layout.program_details, nullptr);
    draw_label_block("SAVED SYSTEMS", g_layout.program_profiles_label);
    draw_label_block("ACTIVE SYSTEM", g_layout.program_details_label);
    draw_program_profiles_static();
    draw_program_details_static();
    return;
  }
  draw_card_block(g_layout.program_details, nullptr);
  button(g_layout.program_profiles_tab.x, g_layout.program_profiles_tab.y,
         g_layout.program_profiles_tab.w, g_layout.program_profiles_tab.h,
         "SYSTEMS", kCyan, !g_program_details_page);
  button(g_layout.program_details_tab.x, g_layout.program_details_tab.y,
         g_layout.program_details_tab.w, g_layout.program_details_tab.h,
         "DETAILS", kCyan, g_program_details_page);
  if (!g_program_details_page) draw_program_profiles_static();
  else draw_program_details_static();
}

void draw_program_profiles_dynamic() {
  const size_t page_size = static_cast<size_t>(std::max(1, g_layout.profile_row_count));
  if (g_snapshot.profiles.count == 0) g_profile_cursor = 0;
  else if (g_profile_cursor >= g_snapshot.profiles.count) g_profile_cursor = g_snapshot.profiles.count - 1;
  const size_t first = (g_profile_cursor / page_size) * page_size;
  char value[128]{};
  for (int row = 0; row < g_layout.profile_row_count; ++row) {
    const size_t index = first + static_cast<size_t>(row);
    const auto r = g_layout.profile_rows[row];
    M5.Display.fillRect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, kBg);
    if (index >= g_snapshot.profiles.count) continue;
    const auto& profile = g_snapshot.profiles.profiles[index];
    snprintf(value, sizeof(value), "%c %s%s", index == g_profile_cursor ? '>' : ' ', profile.name,
             static_cast<int>(index) == g_snapshot.profiles.active_index ? "  [ACTIVE]" : "");
    text_block({r.x + 4, r.y + 2, r.w - 8, r.h - 4}, value,
               index == g_profile_cursor ? kGreen : TFT_WHITE, 2);
  }
  const size_t last = std::min(first + page_size, static_cast<size_t>(g_snapshot.profiles.count));
  snprintf(value, sizeof(value), "%u-%u OF %u", unsigned(g_snapshot.profiles.count ? first + 1 : 0),
           unsigned(last), unsigned(g_snapshot.profiles.count));
  if (g_layout.legacy) {
    text_block(g_layout.profile_page, g_snapshot.profiles.count ? value : "NO SYSTEMS - USE IMPORT", kMuted, 2);
  }
  else draw_value_block(g_layout.profile_page, g_snapshot.profiles.count ? value : "NO SYSTEMS", kMuted, 2);
}

void draw_program_details_dynamic() {
  char value[160]{};
  char identity[112]{};
  snprintf(identity, sizeof(identity), "NAC %03X  WACN %05lX\nSYSID %03X  RFSS %u  SITE %u",
           g_snapshot.config.nac, static_cast<unsigned long>(g_snapshot.config.wacn),
           g_snapshot.config.system_id, g_snapshot.config.rfss, g_snapshot.config.site);
  if (g_layout.legacy) {
    draw_value_block(g_layout.program_detail_name, g_snapshot.config.system_name,
                     g_snapshot.profiles.active_index >= 0 ? TFT_WHITE : kYellow, 2);
    draw_value_block(g_layout.program_detail_identity, identity, kCyan, 2);
    draw_value_block(g_layout.program_detail_status, g_snapshot.config_status, kMuted, 2);
  } else {
    draw_value_block(g_layout.program_detail_name, g_snapshot.config.system_name,
                     g_snapshot.profiles.active_index >= 0 ? TFT_WHITE : kYellow, 2);
    draw_value_block(g_layout.program_detail_identity, identity, kCyan, 2);
    draw_value_block(g_layout.program_detail_status, g_snapshot.config_status, kMuted, 2);
  }
  const size_t page_size = static_cast<size_t>(std::max(1, g_layout.candidate_row_count));
  const size_t pages = std::max<size_t>(1, (g_snapshot.config.control_channel_count + page_size - 1) / page_size);
  if (g_program_candidate_page >= pages) g_program_candidate_page = static_cast<uint8_t>(pages - 1);
  const size_t first = static_cast<size_t>(g_program_candidate_page) * page_size;
  for (int row = 0; row < g_layout.candidate_row_count; ++row) {
    const size_t i = first + static_cast<size_t>(row);
    const auto r = g_layout.program_candidates[row];
    M5.Display.fillRect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, kBg);
    if (i >= g_snapshot.config.control_channel_count) continue;
    snprintf(value, sizeof(value), "CH %u  %.4f MHz%s", unsigned(i + 1),
             g_snapshot.config.control_channels_hz[i] / 1000000.0,
             i == g_snapshot.candidate_index ? "  <" : "");
    text_block({r.x + 4, r.y + 2, r.w - 8, r.h - 4}, value,
               i == g_snapshot.candidate_index ? kGreen : TFT_WHITE, 2);
  }
  snprintf(value, sizeof(value), "CHANNELS %u  PAGE %u", unsigned(g_snapshot.config.control_channel_count),
           unsigned(first / page_size + 1));
  if (!g_layout.legacy) draw_value_block(g_layout.program_candidate_page, value, kMuted, 2);
}

void draw_program_dynamic() {
  if (g_layout.legacy || !g_program_details_page) draw_program_profiles_dynamic();
  if (g_layout.legacy || g_program_details_page) draw_program_details_dynamic();
}

void draw_health_static() {
  for (const auto r : g_layout.health_summary) card(r.x, r.y, r.w, r.h);
  for (int i = 0; i < g_layout.health_row_count; ++i) card(g_layout.health_rows[i].x, g_layout.health_rows[i].y,
                                                          g_layout.health_rows[i].w, g_layout.health_rows[i].h);
  for (const auto r : g_layout.health_footer) card(r.x, r.y, r.w, r.h, kGrid);
}

void draw_health_dynamic() {
  char value[128]{};
  const auto put = [](layout::Rect r, const char* title, const char* value, uint16_t color = TFT_WHITE) {
    M5.Display.fillRect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, kBg);
    text_block({r.x + 6, r.y + 4, r.w - 12, 20}, title, kCyan, 2);
    text_block({r.x + 6, r.y + 24, r.w - 12, r.h - 28}, value, color, 2);
  };
  snprintf(value, sizeof(value), "%.4f MHz", g_snapshot.frequency_hz / 1000000.0);
  put(g_layout.health_summary[0], "FREQUENCY", value);
  put(g_layout.health_summary[1], "STATUS", g_snapshot.following_voice ? "VOICE" : g_snapshot.survey_active ? "SURVEYING" :
      g_snapshot.decoded.frame_sync ? "P25 LOCK" : g_snapshot.config.control_channel_count ? "ACQUIRING" : "NO SYSTEM",
      g_snapshot.following_voice || g_snapshot.decoded.frame_sync ? kGreen : kYellow);
  put(g_layout.health_summary[2], "SYSTEM", g_snapshot.config.system_name);
  const char* title[8] = {"EFFECTIVE RATE", "USB OVERRUNS", "CONSUMER DROPS", "RELATIVE LEVEL",
                          "DSP MAX TIME", g_snapshot.following_voice ? "VOICE FRAMES / ERRORS" : "TSBK GOOD / BAD",
                          "ESTIMATED BER / LOCK", "DRIVER / MODULATION"};
  char values[8][64]{};
  snprintf(values[0], sizeof(values[0]), "%.1f%%", g_snapshot.target_sps ? 100.0 * g_snapshot.effective_sps / g_snapshot.target_sps : 0.0);
  snprintf(values[1], sizeof(values[1]), "%lu", static_cast<unsigned long>(g_snapshot.usb_overruns));
  snprintf(values[2], sizeof(values[2]), "%lu", static_cast<unsigned long>(g_snapshot.consumer_drops));
  snprintf(values[3], sizeof(values[3]), "%.1f dBFS", static_cast<double>(g_snapshot.relative_dbfs));
  snprintf(values[4], sizeof(values[4]), "%lu%%", static_cast<unsigned long>(g_snapshot.dsp_percent));
  snprintf(values[5], sizeof(values[5]), "%lu / %lu",
           static_cast<unsigned long>(g_snapshot.following_voice ? g_snapshot.imbe_frames : g_snapshot.decoded.tsbk_good),
           static_cast<unsigned long>(g_snapshot.following_voice ? g_snapshot.imbe_errors : g_snapshot.decoded.tsbk_failed));
  snprintf(values[6], sizeof(values[6]), "%.2f%% / %.0f%%",
           static_cast<double>(g_snapshot.decoded.estimated_ber_percent),
           static_cast<double>(g_snapshot.decoded.lock_quality_percent));
  snprintf(values[7], sizeof(values[7]), "%s / %s",
           g_snapshot.running ? "RUNNING" : g_snapshot.driver_ready ? "READY" : "OFFLINE",
           p25core::modulation_name(g_snapshot.decoded.selected_modulation));
  for (int i = 0; i < g_layout.health_row_count; ++i) put(g_layout.health_rows[i], title[i], values[i]);
  snprintf(value, sizeof(value), "AUDIO UNDERRUNS  %lu", static_cast<unsigned long>(g_snapshot.audio_underruns));
  put(g_layout.health_footer[0], "AUDIO", value, g_snapshot.audio_underruns ? kYellow : kGreen);
  put(g_layout.health_footer[1], "WI-FI", g_snapshot.wifi_connected ? "CONNECTED" : "OFFLINE");
  snprintf(value, sizeof(value), "T%+.2f  C%+.0fHz  D%.1f/s  F%.1f%%  %s",
           static_cast<double>(g_snapshot.decoded.timing_error),
           static_cast<double>(g_snapshot.decoded.carrier_error_hz),
           static_cast<double>(g_snapshot.decoded.decode_rate_hz),
           static_cast<double>(g_snapshot.decoded.frame_error_percent), g_snapshot.last_error);
  put(g_layout.health_footer[2], "DEMODULATOR / LAST ERROR", value, g_snapshot.last_error[0] ? kYellow : TFT_WHITE);
}

void draw_view_static() {
  focus_nav::clear();
  (void)focus_nav::set_viewport(g_layout.viewport.x + g_layout.viewport.w,
                                g_layout.viewport.y + g_layout.viewport.h);
  M5.Display.clearScrollRect();
  M5.Display.fillRect(g_layout.content.x, g_layout.content.y, g_layout.content.w, g_layout.content.h, kBg);
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

bool same_grant(const p25core::Grant& a, const p25core::Grant& b) {
  return a.valid == b.valid && a.encrypted == b.encrypted && a.emergency == b.emergency &&
         a.tdma == b.tdma && a.talkgroup == b.talkgroup && a.source_id == b.source_id &&
         a.frequency_hz == b.frequency_hz && a.seen_ms == b.seen_ms;
}

bool same_visible_snapshot(const Snapshot& a, const Snapshot& b) {
  if (a.frequency_hz != b.frequency_hz || a.span_hz != b.span_hz ||
      a.effective_sps != b.effective_sps || a.target_sps != b.target_sps ||
      a.usb_overruns != b.usb_overruns || a.consumer_drops != b.consumer_drops ||
      a.audio_underruns != b.audio_underruns || a.dsp_percent != b.dsp_percent ||
      a.audio_ring_pressure_percent != b.audio_ring_pressure_percent ||
      a.battery_percent != b.battery_percent || a.relative_dbfs != b.relative_dbfs ||
      a.running != b.running || a.driver_ready != b.driver_ready ||
      a.wifi_connected != b.wifi_connected || a.sound_enabled != b.sound_enabled ||
      a.survey_active != b.survey_active || a.hold != b.hold ||
      a.hold_talkgroup != b.hold_talkgroup || a.auto_follow != b.auto_follow ||
      a.encryption_skip != b.encryption_skip || a.following_voice != b.following_voice ||
      a.probing_phase2 != b.probing_phase2 || a.voice_encrypted != b.voice_encrypted ||
      a.voice_algorithm_id != b.voice_algorithm_id || a.voice_key_id != b.voice_key_id ||
      a.imbe_frames != b.imbe_frames || a.imbe_errors != b.imbe_errors ||
      a.candidate_index != b.candidate_index || a.candidate_count != b.candidate_count ||
      a.volume != b.volume || a.config_revision != b.config_revision ||
      strcmp(a.config.system_name, b.config.system_name) != 0 ||
      strcmp(a.config_status, b.config_status) != 0 || strcmp(a.last_error, b.last_error) != 0 ||
      a.config.control_channel_count != b.config.control_channel_count ||
      a.config.talkgroup_count != b.config.talkgroup_count ||
      a.config.nac != b.config.nac || a.config.wacn != b.config.wacn ||
      a.config.system_id != b.config.system_id || a.config.rfss != b.config.rfss ||
      a.config.site != b.config.site ||
      a.profiles.count != b.profiles.count || a.profiles.active_index != b.profiles.active_index ||
      strcmp(a.profiles.active_id, b.profiles.active_id) != 0 ||
      a.decoded.frame_sync != b.decoded.frame_sync || a.decoded.identity_valid != b.decoded.identity_valid ||
      a.decoded.selected_modulation != b.decoded.selected_modulation ||
      a.decoded.tsbk_good != b.decoded.tsbk_good || a.decoded.tsbk_failed != b.decoded.tsbk_failed ||
      a.decoded.nid_good != b.decoded.nid_good || a.decoded.estimated_ber_percent != b.decoded.estimated_ber_percent ||
      a.decoded.frame_error_percent != b.decoded.frame_error_percent ||
      a.decoded.lock_quality_percent != b.decoded.lock_quality_percent ||
      a.decoded.timing_error != b.decoded.timing_error || a.decoded.carrier_error_hz != b.decoded.carrier_error_hz ||
      a.decoded.decode_rate_hz != b.decoded.decode_rate_hz ||
      !same_grant(a.decoded.current_grant, b.decoded.current_grant)) return false;
  if (memcmp(a.candidate_levels, b.candidate_levels, sizeof(a.candidate_levels)) != 0) return false;
  for (size_t i = 0; i < a.config.control_channel_count; ++i)
    if (a.config.control_channels_hz[i] != b.config.control_channels_hz[i]) return false;
  for (size_t i = 0; i < a.config.talkgroup_count; ++i)
    if (a.config.talkgroups[i].id != b.config.talkgroups[i].id ||
        strcmp(a.config.talkgroups[i].alias, b.config.talkgroups[i].alias) != 0) return false;
  for (size_t i = 0; i < a.profiles.count; ++i)
    if (strcmp(a.profiles.profiles[i].id, b.profiles.profiles[i].id) != 0 ||
        strcmp(a.profiles.profiles[i].name, b.profiles.profiles[i].name) != 0) return false;
  for (size_t i = 0; i < p25decoder::kRecentGrantCount; ++i)
    if (!same_grant(a.decoded.recent_grants[i], b.decoded.recent_grants[i])) return false;
  return true;
}

void draw_action_controls() {
  if (g_view == View::monitor) {
    char hold_label[24]{};
    format_hold_label(hold_label, sizeof(hold_label));
    const char* names[] = {"<  CHANNEL", "CHANNEL  >", g_snapshot.survey_active ? "STOP SURVEY" : "SURVEY",
                           hold_label, "SKIP / NEXT"};
    for (int i = 0; i < 5; ++i) {
      const auto r = g_layout.monitor_actions[i];
      button(r.x, r.y, r.w, r.h, names[i], i == 2 ? kGreen : i == 3 ? kYellow : kCyan,
             i == 2 ? g_snapshot.survey_active : i == 3 && g_snapshot.hold);
    }
  } else if (g_view == View::program) {
    if (g_layout.legacy || !g_program_details_page) {
      const char* names[] = {"SELECT", "RENAME", "IMPORT", "EXPORT", g_delete_armed ? "CONFIRM" : "DELETE", "RELOAD"};
      for (int i = 0; i < 6; ++i) {
        const auto r = g_layout.program_controls[i];
        button(r.x, r.y, r.w, r.h, names[i], i == 4 ? kRed : kCyan);
      }
    }
    if (g_layout.legacy || g_program_details_page) {
      button(g_layout.policy_controls[0].x, g_layout.policy_controls[0].y,
             g_layout.policy_controls[0].w, g_layout.policy_controls[0].h,
             "AUTO FOLLOW", kGreen, g_snapshot.auto_follow);
      button(g_layout.policy_controls[1].x, g_layout.policy_controls[1].y,
             g_layout.policy_controls[1].w, g_layout.policy_controls[1].h,
             "SKIP ENCRYPTED", kGreen, g_snapshot.encryption_skip);
    }
  }
}

uint16_t waterfall_color(float level) {
  return waterfall_style::color565(waterfall_style::Screen::p25, level);
}

}  // namespace

void enter(const Snapshot& snapshot) {
  g_snapshot = snapshot;
  g_view = View::monitor;
  g_active = true;
  g_profile_cursor = 0;
  g_talk_page = 0;
  g_program_candidate_page = 0;
  g_program_details_page = false;
  g_delete_armed = false;
  g_profile_name_ready = false;
  audio_header::reset(g_audio_control);
  draw();
}

bool set_viewport(int x, int y, int width, int height) {
  const layout::Rect viewport{x, y, width, height};
  const auto next = layout::make_layout(viewport);
  if (!next.valid) return false;
  const bool changed = g_layout.viewport.x != x || g_layout.viewport.y != y ||
                       g_layout.viewport.w != width || g_layout.viewport.h != height;
  const auto old_editor_viewport = g_layout.content;
  if (changed && text_editor::active() && !text_editor::set_viewport(
          {next.content.x + 8, next.content.y + 8, next.content.w - 16, next.content.h - 16})) return false;
  if (!audio_header::set_viewport(viewport)) {
    if (changed && text_editor::active()) (void)text_editor::set_viewport(
        {old_editor_viewport.x + 8, old_editor_viewport.y + 8,
         old_editor_viewport.w - 16, old_editor_viewport.h - 16});
    return false;
  }
  if (!focus_nav::set_viewport(x + width, y + height)) {
    (void)audio_header::set_viewport(g_layout.viewport);
    if (changed && text_editor::active()) (void)text_editor::set_viewport(
        {old_editor_viewport.x + 8, old_editor_viewport.y + 8,
         old_editor_viewport.w - 16, old_editor_viewport.h - 16});
    return false;
  }
  g_layout = next;
  if (!changed) return true;
  g_update_pending = true;
  if (g_active) draw();
  return true;
}

void leave() {
  M5.Display.clearScrollRect();
  g_active = false;
}

static void draw_view() {
  draw_view_static();
  draw_dynamic();
  if (text_editor::active()) text_editor::draw();
  g_update_pending = false;
}

void draw() {
  if (!g_active) return;
  M5.Display.fillScreen(kBg);
  draw_header();
  draw_view();
}

void update(const Snapshot& snapshot) {
  if (!g_active) return;
  const bool header_changed = snapshot.battery_percent != g_snapshot.battery_percent ||
                              snapshot.volume != g_snapshot.volume ||
                              snapshot.sound_enabled != g_snapshot.sound_enabled ||
                              snapshot.running != g_snapshot.running;
  const bool changed = !same_visible_snapshot(snapshot, g_snapshot);
  g_snapshot = snapshot;
  const uint32_t now = millis();
  if (header_changed || audio_header::service_timeout(g_audio_control, now))
    audio_header::draw(g_audio_control, g_snapshot.volume, g_snapshot.sound_enabled,
                       g_snapshot.battery_percent);
  if (changed) g_update_pending = true;
  if (!g_update_pending || now - g_last_dynamic_ms < 200) return;
  g_last_dynamic_ms = now;
  g_update_pending = false;
  draw_dynamic();
  draw_action_controls();
}

scope::FrameStats g_scope_stats;

void draw_spectrum(const float* levels, size_t first_bin, size_t visible_bins, float floor) {
  if (!spectrum_active() || levels == nullptr || visible_bins < 2) return;
  const uint32_t frame_started_ms = millis();
  const auto plot = g_layout.spectrum_plot;
  const auto water = g_layout.waterfall;
  // The trace sprite is the only size-dependent plot allocation and stays in PSRAM.
  const int plot_w = plot.w - 2;
  const int plot_h = plot.h - 2;
  const int w = plot_w;
  const int h = plot_h;
  if (w < 2 || h < 2 || water.w > kSpectrumW || water.h < 4) return;
  static scope::Trace trace;
  M5Canvas* canvas = trace.begin(w, h, kBg);
  // No memory for the trace sprite: leave the frame alone rather than scroll the waterfall under a stale spectrum.
  if (canvas == nullptr) return;
  if (canvas != nullptr) {
    for (int i = 1; i < 4; ++i) {
      canvas->drawFastVLine(i * plot.w / 4 - 1, 0, h, kGrid);
      canvas->drawFastHLine(0, i * plot.h / 4 - 1, w, kGrid);
    }
  }
  int px = 0;
  int py = h - 2;
  for (size_t i = 0; i < visible_bins; ++i) {
    const float normalized = std::clamp((levels[first_bin + i] - floor) / 48.0f, 0.0f, 1.0f);
    const int x = static_cast<int>(i * (plot.w - 1) / (visible_bins - 1)) - 1;
    const int y = h - 2 - static_cast<int>(normalized * (plot.h - 4));
    if (canvas != nullptr && i && x >= 0 && x < w) canvas->drawLine(px, py, x, y, kGreen);
    px = x;
    py = y;
    const int x0 = static_cast<int>(i * water.w / visible_bins);
    const int x1 = static_cast<int>((i + 1) * water.w / visible_bins);
    const uint16_t color = waterfall_color(normalized);
    for (int p = x0; p < x1; ++p) g_waterfall_row[p] = color;
  }
  const int center = plot.x + plot.w / 2;
  if (canvas != nullptr) {
    const int half_filter = std::max(3, static_cast<int>(12500ull * plot.w /
                                                         (2ull * std::max<uint32_t>(1, g_snapshot.span_hz))));
    canvas->drawFastVLine(w / 2, 0, h, kCyan);
    canvas->drawFastVLine(w / 2 - half_filter, 0, h, kCyan);
    canvas->drawFastVLine(w / 2 + half_filter, 0, h, kCyan);
    scope::draw_style_chips(canvas, w, waterfall_style::Screen::p25);
    canvas->pushSprite(plot.x + 1, plot.y + 1);
  }
  scope::scroll_waterfall(water.x, water.y + water.h - 2, water.w, g_waterfall_row,
                          waterfall_style::rows_due(waterfall_style::Screen::p25, millis()));
  M5.Display.drawFastVLine(center, water.y, water.h, kCyan);
  g_scope_stats.frame_done(frame_started_ms);
}

uint32_t spectrum_fps() { return g_scope_stats.fps(); }
uint32_t spectrum_draw_ms() { return g_scope_stats.draw_ms(); }

Action handle_touch(int32_t x, int32_t y) {
  if (text_editor::active()) {
    const auto result = text_editor::handle_touch(x, y);
    if (result == text_editor::Result::accepted) {
      strlcpy(g_profile_name, text_editor::value(), sizeof(g_profile_name));
      g_profile_name_ready = true;
      draw();
      return {ActionKind::rename_profile, g_profile_cursor};
    }
    if (result == text_editor::Result::cancelled) draw();
    return {};
  }
  if (!g_active) return {};
  if (spectrum_active() && scope::style_chip_tap(
          x, y, g_layout.spectrum_plot.x + 1, g_layout.spectrum_plot.y + 1,
          g_layout.spectrum_plot.w - 2, waterfall_style::Screen::p25))
    return {};
  if (audio_header::settings_hit(x, y)) return {ActionKind::open_device_settings};
  for (uint8_t i = 0; i < static_cast<uint8_t>(View::count); ++i) {
    if (!hit(x, y, g_layout.tab[i])) continue;
    if (i != static_cast<uint8_t>(g_view)) { g_view = static_cast<View>(i); draw_view(); }
    return {};
  }
  if (g_view == View::monitor) {
    for (int i = 0; i < 5; ++i) if (hit(x, y, g_layout.monitor_actions[i])) {
      switch (i) {
        case 0: return {ActionKind::previous_candidate};
        case 1: return {ActionKind::next_candidate};
        case 2: return {ActionKind::survey_toggle};
        case 3: return {ActionKind::hold_toggle};
        default: return {ActionKind::skip_talkgroup};
      }
    }
  } else if (g_view == View::spectrum) {
    if (hit(x, y, g_layout.span_down)) return {ActionKind::span_down};
    if (hit(x, y, g_layout.span_up)) return {ActionKind::span_up};
    if (hit(x, y, g_layout.spectrum_plot)) {
      const auto plot = g_layout.spectrum_plot;
      const int64_t offset = static_cast<int64_t>(x - (plot.x + plot.w / 2)) * g_snapshot.span_hz / plot.w;
      const int64_t selected = static_cast<int64_t>(g_snapshot.frequency_hz) + offset;
      return {ActionKind::tune_hz, static_cast<uint32_t>(std::clamp<int64_t>(selected, kP25MinHz, kP25MaxHz))};
    }
  } else if (g_view == View::talkgroups) {
    if (!g_layout.legacy && hit(x, y, g_layout.talk_previous)) {
      if (g_talk_page > 0) { --g_talk_page; draw_talkgroups_dynamic(); }
      return {};
    }
    if (!g_layout.legacy && hit(x, y, g_layout.talk_next)) {
      const int pages = std::max(1, (static_cast<int>(g_snapshot.config.talkgroup_count) + g_layout.talk_row_count - 1) / g_layout.talk_row_count);
      if (g_talk_page + 1 < pages) { ++g_talk_page; draw_talkgroups_dynamic(); }
      return {};
    }
    const int first = g_talk_page * g_layout.talk_row_count;
    for (int row = 0; row < g_layout.talk_row_count; ++row) if (hit(x, y, g_layout.talk_rows[row])) {
      const int index = first + row;
      if (index < g_snapshot.config.talkgroup_count)
        return {ActionKind::hold_talkgroup, g_snapshot.config.talkgroups[index].id};
      return {};
    }
  } else if (g_view == View::program) {
    if (!g_layout.legacy && !g_program_details_page && hit(x, y, g_layout.program_details_tab)) {
      g_program_details_page = true; g_program_candidate_page = 0; draw(); return {};
    }
    if (!g_layout.legacy && g_program_details_page && hit(x, y, g_layout.program_profiles_tab)) {
      g_program_details_page = false; draw(); return {};
    }
    if ((!g_program_details_page || g_layout.legacy) && hit(x, y, g_layout.profile_previous)) {
      if (g_profile_cursor >= g_layout.profile_row_count)
        g_profile_cursor = static_cast<uint8_t>(g_profile_cursor - g_layout.profile_row_count);
      else return {};
      g_delete_armed = false; draw_program_dynamic(); return {};
    }
    if ((!g_program_details_page || g_layout.legacy) && hit(x, y, g_layout.profile_next)) {
      if (g_profile_cursor + g_layout.profile_row_count < g_snapshot.profiles.count)
        g_profile_cursor = static_cast<uint8_t>(g_profile_cursor + g_layout.profile_row_count);
      else return {};
      g_delete_armed = false; draw_program_dynamic(); return {};
    }
    if (!g_layout.legacy && g_program_details_page && hit(x, y, g_layout.program_candidate_previous)) {
      if (g_program_candidate_page) --g_program_candidate_page;
      draw_program_details_dynamic(); return {};
    }
    if (!g_layout.legacy && g_program_details_page && hit(x, y, g_layout.program_candidate_next)) {
      if ((g_program_candidate_page + 1) * g_layout.candidate_row_count < g_snapshot.config.control_channel_count)
        ++g_program_candidate_page;
      draw_program_details_dynamic(); return {};
    }
    if (!g_program_details_page || g_layout.legacy) {
      const size_t page_size = static_cast<size_t>(std::max(1, g_layout.profile_row_count));
      const size_t first = (g_profile_cursor / page_size) * page_size;
      for (int row = 0; row < g_layout.profile_row_count; ++row) if (hit(x, y, g_layout.profile_rows[row])) {
        const size_t index = first + static_cast<size_t>(row);
        if (index < g_snapshot.profiles.count) g_profile_cursor = static_cast<uint8_t>(index);
        g_delete_armed = false; draw_program_profiles_dynamic(); return {};
      }
    }
    if (g_layout.legacy || !g_program_details_page) {
      for (int i = 0; i < 6; ++i) if (hit(x, y, g_layout.program_controls[i])) {
        switch (i) {
          case 0: return g_snapshot.profiles.count ? Action{ActionKind::select_profile, g_profile_cursor} : Action{};
          case 1:
            if (!g_snapshot.profiles.count) return {};
            text_editor::begin("RENAME P25 SYSTEM", g_snapshot.profiles.profiles[g_profile_cursor].name, 47, false, "SAVE");
            (void)text_editor::set_viewport({g_layout.content.x + 8, g_layout.content.y + 8,
                                             g_layout.content.w - 16, g_layout.content.h - 16});
            text_editor::draw(); return {};
          case 2: return {ActionKind::import_profile};
          case 3: return g_snapshot.profiles.count ? Action{ActionKind::export_profile, g_profile_cursor} : Action{};
          case 4:
            if (!g_snapshot.profiles.count) return {};
            if (g_delete_armed) { g_delete_armed = false; return {ActionKind::delete_profile, g_profile_cursor}; }
            g_delete_armed = true; draw_action_controls(); return {};
          default: return {ActionKind::reload_config};
        }
      }
    }
    if (g_layout.legacy || g_program_details_page) {
      for (int i = 0; i < 2; ++i) if (hit(x, y, g_layout.policy_controls[i]))
        return {i == 0 ? ActionKind::auto_follow_toggle : ActionKind::encryption_skip_toggle};
    }
  }
  return {};
}

Action handle_control(Control control, uint32_t value) {
  if (!g_active) return {};
  switch (control) {
    case Control::tune_hz:
      return {ActionKind::tune_hz, std::clamp(value, kP25MinHz, kP25MaxHz)};
    case Control::previous_candidate: return {ActionKind::previous_candidate};
    case Control::next_candidate: return {ActionKind::next_candidate};
    case Control::survey_toggle: return {ActionKind::survey_toggle};
    case Control::hold_toggle: return {ActionKind::hold_toggle};
    case Control::hold_talkgroup:
      for (size_t i = 0; i < g_snapshot.config.talkgroup_count; ++i)
        if (g_snapshot.config.talkgroups[i].id == value) return {ActionKind::hold_talkgroup, value};
      return {};
    case Control::skip_talkgroup: return {ActionKind::skip_talkgroup};
    case Control::span_down: return {ActionKind::span_down};
    case Control::span_up: return {ActionKind::span_up};
    case Control::auto_follow_toggle: return {ActionKind::auto_follow_toggle};
    case Control::encryption_skip_toggle: return {ActionKind::encryption_skip_toggle};
    case Control::select_profile:
      return g_snapshot.profiles.count ? Action{ActionKind::select_profile, g_profile_cursor} : Action{};
    case Control::rename_profile:
      if (!g_snapshot.profiles.count) return {};
      text_editor::begin("RENAME P25 SYSTEM", g_snapshot.profiles.profiles[g_profile_cursor].name, 47, false, "SAVE");
      (void)text_editor::set_viewport({g_layout.content.x + 8, g_layout.content.y + 8,
                                       g_layout.content.w - 16, g_layout.content.h - 16});
      text_editor::draw();
      return {};
    case Control::import_profile: return {ActionKind::import_profile};
    case Control::export_profile:
      return g_snapshot.profiles.count ? Action{ActionKind::export_profile, g_profile_cursor} : Action{};
    case Control::delete_profile:
      if (!g_snapshot.profiles.count) return {};
      if (g_delete_armed) { g_delete_armed = false; return {ActionKind::delete_profile, g_profile_cursor}; }
      g_delete_armed = true; draw_action_controls(); return {};
    case Control::reload_config: return {ActionKind::reload_config};
    case Control::sound_toggle: return {ActionKind::sound_toggle};
    case Control::volume_down: return {ActionKind::volume_down};
    case Control::volume_up: return {ActionKind::volume_up};
    case Control::open_device_settings: return {ActionKind::open_device_settings};
    case Control::exit_to_home: return {ActionKind::exit_to_home};
    case Control::previous_view:
      g_view = static_cast<View>((static_cast<uint8_t>(g_view) + static_cast<uint8_t>(View::count) - 1) % static_cast<uint8_t>(View::count));
      draw_view(); return {};
    case Control::next_view:
      g_view = static_cast<View>((static_cast<uint8_t>(g_view) + 1) % static_cast<uint8_t>(View::count));
      draw_view(); return {};
  }
  return {};
}

bool active() { return g_active; }
bool spectrum_active() { return g_active && g_view == View::spectrum; }
View view() { return g_view; }
bool take_profile_name(char* value, size_t size) {
  if (!g_profile_name_ready || value == nullptr || size == 0) return false;
  strlcpy(value, g_profile_name, size);
  g_profile_name_ready = false;
  return true;
}

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
  const auto legacy = layout::make_layout({0, 0, 1280, 720});
  if (static_cast<uint8_t>(View::count) != 5 || !legacy.valid ||
      legacy.spectrum_plot.x + legacy.spectrum_plot.w > 1280 ||
      legacy.tabs.y >= 720 || !p25config::self_check() || !audio_header::self_check()) return false;
  const layout::Rect viewports[] = {{0, 0, 1280, 720}, {0, 0, 800, 480},
                                    {72, 72, 424, 1088}, {72, 72, 1088, 424}};
  for (const auto viewport : viewports) {
    const auto l = layout::make_layout(viewport);
    if (!l.valid || !layout::inside(viewport, l.content) || !layout::inside(viewport, l.tabs)) return false;
    for (const auto tab : l.tab)
      if (!layout::inside(l.tabs, tab) || tab.w < 44 || tab.h < 44) return false;
  }
  return true;
}

}  // namespace orcsdr::p25
