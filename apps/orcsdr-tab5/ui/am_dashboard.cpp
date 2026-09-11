#include "am_dashboard.hpp"

#include "dashboard_audio_control.hpp"
#include "orc_badge.hpp"
#include "receiver_band_plan.hpp"
#include "nvs_store.hpp"

#include <M5Unified.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <iterator>

namespace orcsdr::am {
namespace {

constexpr uint16_t kBg = TFT_BLACK;
constexpr uint16_t kPanel = 0x0841;
constexpr uint16_t kCyan = 0x2e7f;
constexpr uint16_t kGreen = 0x6fe8;
constexpr uint16_t kYellow = 0xff24;
constexpr uint16_t kMuted = 0x8c71;
constexpr uint16_t kGrid = 0x2945;
constexpr int kHeaderH = 132;
constexpr int kTabsY = 630;
constexpr int kTabW = 1280 / static_cast<int>(View::count);
constexpr int kSpectrumX = 46;
constexpr int kSpectrumY = 246;
constexpr int kSpectrumW = 1188;
constexpr int kSpectrumH = 145;
constexpr int kWaterfallY = 420;
constexpr int kWaterfallH = 130;
constexpr int kGainAutoX = 48;
constexpr int kGainAutoY = 486;
constexpr int kGainAutoW = 190;
constexpr int kGainSliderX = 280;
constexpr int kGainSliderY = 525;
constexpr int kGainSliderW = 900;
constexpr int kBandwidthSliderX = 720;
constexpr int kBandwidthSliderY = 438;
constexpr int kBandwidthSliderW = 500;
constexpr size_t kPresetsPerPage = 6;
constexpr size_t kMaxPresets = 160;
constexpr uint32_t kPresetHoldMs = 700;
constexpr size_t kScanResultCapacity = 6;

struct GainLayout {
  int auto_x;
  int auto_y;
  int auto_w;
  int auto_h;
  int slider_x;
  int slider_y;
  int slider_w;
};

Snapshot g_snapshot{};
View g_view = View::listen;
bool g_active = false;
uint32_t g_last_dynamic_ms = 0;
EXT_RAM_BSS_ATTR uint16_t g_waterfall_row[kSpectrumW]{};
audio_header::Control g_audio_control{};
NvsStore* g_store = nullptr;
uint32_t g_saved_frequency = receiver_bands::kAmBroadcast.default_hz;
uint32_t g_tune_step = 1000;
uint32_t g_scan_spacing = receiver_bands::kAmBroadcast.default_step_hz;
EXT_RAM_BSS_ATTR uint32_t g_presets[kMaxPresets]{};
uint16_t g_preset_count = 0;
uint8_t g_preset_page = 0;
bool g_preset_pressed = false;
bool g_preset_press_valid = false;
uint16_t g_pressed_preset = 0;
uint32_t g_preset_press_ms = 0;
bool g_preset_modal = false;
uint16_t g_edit_preset = 0;
EXT_RAM_BSS_ATTR uint32_t g_scan_results_hz[kScanResultCapacity]{};
EXT_RAM_BSS_ATTR float g_scan_results_dbfs[kScanResultCapacity]{};
EXT_RAM_BSS_ATTR bool g_scan_results_selected[kScanResultCapacity]{};
uint8_t g_scan_result_count = 0;
std::atomic<bool> g_scan_prompt{false};
std::atomic<bool> g_scan_prompt_draw_pending{false};
EXT_RAM_BSS_ATTR float g_scan_sorted[160]{};

struct ScanCandidate {
  size_t index;
  float level;
};

bool hit(int32_t x, int32_t y, int bx, int by, int bw, int bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

uint8_t preset_pages() {
  return static_cast<uint8_t>(std::max<size_t>(
      1, (g_preset_count + kPresetsPerPage - 1) / kPresetsPerPage));
}

void persist_presets() {
  if (!g_store) return;
  if (g_preset_count)
    (void)g_store->put_bytes("am_presets", g_presets,
                             static_cast<size_t>(g_preset_count) * sizeof(g_presets[0]));
  else
    (void)g_store->remove("am_presets");
  (void)g_store->put_u16("am_preset_n", g_preset_count);
}

bool erase_preset(uint32_t* presets, size_t& count, size_t index) {
  if (!presets || index >= count) return false;
  std::move(presets + index + 1, presets + count, presets + index);
  presets[--count] = 0;
  return true;
}

uint32_t bandwidth_for_x(int32_t x) {
  const int clamped = std::clamp<int32_t>(x, kBandwidthSliderX,
                                          kBandwidthSliderX + kBandwidthSliderW);
  const uint32_t hz = 3000u + static_cast<uint32_t>(clamped - kBandwidthSliderX) *
                                  27000u / kBandwidthSliderW;
  return ((hz + 500u) / 1000u) * 1000u;
}

void reset_scan_results() {
  g_scan_result_count = 0;
  g_scan_prompt = false;
  g_scan_prompt_draw_pending = false;
  std::fill(std::begin(g_scan_results_hz), std::end(g_scan_results_hz), 0u);
  std::fill(std::begin(g_scan_results_selected), std::end(g_scan_results_selected), false);
}

uint8_t save_scan_results() {
  uint8_t added = 0;
  for (size_t i = 0; i < g_scan_result_count && g_preset_count < std::size(g_presets); ++i) {
    if (!g_scan_results_selected[i]) continue;
    const uint32_t frequency_hz = g_scan_results_hz[i];
    if (std::find(g_presets, g_presets + g_preset_count, frequency_hz) !=
        g_presets + g_preset_count)
      continue;
    g_presets[g_preset_count++] = frequency_hz;
    ++added;
  }
  if (added) {
    g_preset_page = static_cast<uint8_t>((g_preset_count - 1) / kPresetsPerPage);
    persist_presets();
  }
  g_scan_prompt = false;
  g_scan_prompt_draw_pending = false;
  return added;
}

size_t select_scan_candidates(const float* levels, size_t count,
                              ScanCandidate* candidates, size_t capacity,
                              float* baseline_dbfs) {
  if (!levels || !candidates || !count || count > std::size(g_scan_sorted)) return 0;
  std::copy_n(levels, count, g_scan_sorted);
  std::nth_element(g_scan_sorted, g_scan_sorted + count / 2, g_scan_sorted + count);
  const float baseline = g_scan_sorted[count / 2];
  if (baseline_dbfs) *baseline_dbfs = baseline;
  size_t found = 0;
  for (size_t i = 0; i < count; ++i) {
    const float level = levels[i];
    const float left = i ? levels[i - 1] : baseline;
    const float right = i + 1 < count ? levels[i + 1] : baseline;
    // ponytail: local-prominence heuristic; add audio validation if RF-only scans prove noisy.
    if (level < baseline + 3.0f || level < left + 1.0f || level < right + 1.0f) continue;
    size_t slot = found;
    if (slot < capacity) ++found;
    else if (level <= candidates[slot = capacity - 1].level) continue;
    while (slot > 0 && level > candidates[slot - 1].level) {
      if (slot < capacity) candidates[slot] = candidates[slot - 1];
      --slot;
    }
    candidates[slot] = {i, level};
  }
  std::sort(candidates, candidates + found,
            [](const ScanCandidate& a, const ScanCandidate& b) { return a.index < b.index; });
  return found;
}

void text(const char* value, int x, int y, uint16_t color = TFT_WHITE,
          int size = 2, textdatum_t datum = middle_center) {
  M5.Display.setTextDatum(datum);
  M5.Display.setTextSize(size);
  M5.Display.setTextColor(color);
  M5.Display.drawString(value, x, y);
}

void format_khz(char* value, size_t size, uint32_t frequency_hz) {
  if (frequency_hz % 1000u == 0)
    snprintf(value, size, "%lu", static_cast<unsigned long>(frequency_hz / 1000u));
  else
    snprintf(value, size, "%.1f", static_cast<double>(frequency_hz) / 1000.0);
}

void card(int x, int y, int w, int h) {
  M5.Display.fillRoundRect(x, y, w, h, 12, kPanel);
  M5.Display.drawRoundRect(x, y, w, h, 12, kCyan);
}

void button(int x, int y, int w, int h, const char* title, uint16_t color = kCyan,
            bool selected = false) {
  M5.Display.fillRoundRect(x, y, w, h, 10, selected ? 0x1264 : kPanel);
  M5.Display.drawRoundRect(x, y, w, h, 10, color);
  text(title, x + w / 2, y + h / 2, selected ? color : TFT_WHITE, 2);
}

void draw_header() {
  M5.Display.fillRect(0, 0, 1280, kHeaderH, kBg);
  M5.Display.drawFastHLine(8, kHeaderH - 1, 1264, kCyan);
  if (!badge::draw(18, 13, 104)) M5.Display.drawRoundRect(18, 13, 104, 104, 18, kGreen);
  text("OrcSDR", 142, 38, TFT_WHITE, 4, middle_left);
  text("AM Broadcast", 142, 82, kCyan, 2, middle_left);
  M5.Display.drawFastVLine(365, 25, 82, kCyan);
  text("AM RADIO", 415, 66, TFT_WHITE, 4, middle_left);
  M5.Display.drawFastVLine(865, 25, 82, kCyan);
  audio_header::draw(g_audio_control, g_snapshot.volume, g_snapshot.sound_enabled,
                     g_snapshot.battery_percent);
  audio_header::draw_home_button();
  audio_header::draw_mute_button(g_snapshot.sound_enabled);
  audio_header::draw_visualizer_button(g_snapshot.running);
  audio_header::draw_settings_button();
}

void draw_tabs() {
  static constexpr const char* names[] = {"LISTEN", "FINDER", "SPECTRUM", "SETTINGS"};
  for (uint8_t i = 0; i < static_cast<uint8_t>(View::count); ++i) {
    const int x = i * kTabW;
    const bool selected = i == static_cast<uint8_t>(g_view);
    M5.Display.fillRect(x, kTabsY, kTabW, 90, selected ? 0x0a43 : kPanel);
    M5.Display.drawRect(x, kTabsY, kTabW, 90, selected ? kGreen : kGrid);
    text(names[i], x + kTabW / 2, kTabsY + 45, selected ? kGreen : TFT_WHITE, 3);
  }
}

void draw_meter(int x, int y, int w, float dbfs) {
  const int segments = 18;
  const int lit = static_cast<int>(std::clamp((dbfs + 90.0f) / 70.0f, 0.0f, 1.0f) * segments);
  const int gap = 3;
  const int sw = (w - (segments - 1) * gap) / segments;
  for (int i = 0; i < segments; ++i)
    M5.Display.fillRect(x + i * (sw + gap), y, sw, 30,
                        i < lit ? (i >= 14 ? kYellow : kGreen) : kGrid);
}

GainLayout gain_layout() {
  if (g_view == View::listen) return {1128, 304, 86, 42, 884, 338, 220};
  if (g_view == View::spectrum) return {928, 561, 82, 44, 1024, 584, 190};
  return {kGainAutoX, kGainAutoY, kGainAutoW, 70,
          kGainSliderX, kGainSliderY, kGainSliderW};
}

void draw_gain_control(bool compact) {
  const GainLayout layout = gain_layout();
  char value[24];
  if (g_snapshot.gain_auto)
    snprintf(value, sizeof(value), g_snapshot.gain_auto_selecting ? "AUTO..." : "AUTO %.1f",
             static_cast<double>(g_snapshot.gain_tenth_db) / 10.0);
  else
    snprintf(value, sizeof(value), "%.1f dB",
             static_cast<double>(g_snapshot.gain_tenth_db) / 10.0);
  button(layout.auto_x, layout.auto_y, layout.auto_w, layout.auto_h, "AUTO", kGreen,
         g_snapshot.gain_auto);
  text(compact ? "GAIN" : "RF GAIN", layout.slider_x,
       layout.slider_y - (compact ? 15 : 33), kCyan, 2, middle_left);
  text(value, layout.slider_x + layout.slider_w,
       layout.slider_y - (compact ? 15 : 33),
       g_snapshot.gain_auto ? kGreen : TFT_WHITE, 2, middle_right);
  M5.Display.fillRoundRect(layout.slider_x, layout.slider_y, layout.slider_w, 18, 9, kGrid);
  const int gain_x = layout.slider_x + std::clamp(g_snapshot.gain_tenth_db, 0, 496) *
                                         layout.slider_w / 496;
  if (!g_snapshot.gain_auto)
    M5.Display.fillRoundRect(layout.slider_x, layout.slider_y,
                             std::max(9, gain_x - layout.slider_x), 18, 9, kGreen);
  M5.Display.fillCircle(gain_x, layout.slider_y + 9, compact ? 11 : 14,
                        g_snapshot.gain_auto ? kMuted : kGreen);
}

void draw_listen_static() {
  card(24, 150, 820, 230);
  card(860, 150, 396, 230);
  text("RELATIVE SIGNAL", 884, 178, kCyan, 2, top_left);
  button(24, 398, 160, 76, "TUNE -");
  button(194, 398, 160, 76, "TUNE +");
  button(364, 398, 210, 76, "STEP");
  text("BANDWIDTH", 600, 420, kCyan, 2, middle_left);
  for (int i = 0; i < 6; ++i) button(24 + i * 166, 494, 154, 70, "EMPTY", kGrid);
  button(1020, 494, 236, 70, "SAVE CURRENT", kGreen);
  text("PRESETS", 24, 582, kCyan, 2, middle_left);
  button(710, 570, 70, 48, "<");
  button(930, 570, 70, 48, ">");
}

void draw_preset_modal() {
  if (!g_preset_modal || g_edit_preset >= g_preset_count) return;
  M5.Display.fillRoundRect(290, 205, 700, 300, 18, kPanel);
  M5.Display.drawRoundRect(290, 205, 700, 300, 18, TFT_RED);
  text("EDIT PRESET", 640, 250, TFT_WHITE, 4);
  char khz[16];
  char frequency[32];
  format_khz(khz, sizeof(khz), g_presets[g_edit_preset]);
  snprintf(frequency, sizeof(frequency), "%s kHz", khz);
  text(frequency, 640, 310, kYellow, 4);
  button(320, 400, 200, 70, "CANCEL", kCyan);
  button(540, 400, 200, 70, "REPLACE", kGreen);
  button(760, 400, 200, 70, "DELETE", TFT_RED);
}

void draw_bandwidth_slider() {
  char value[16];
  snprintf(value, sizeof(value), "%lu kHz",
           static_cast<unsigned long>(g_snapshot.filter_bandwidth_hz / 1000));
  text(value, 1220, 410, kGreen, 2, middle_right);
  M5.Display.fillRoundRect(kBandwidthSliderX, kBandwidthSliderY,
                           kBandwidthSliderW, 18, 9, kGrid);
  const int x = kBandwidthSliderX +
      (static_cast<int>(std::clamp<uint32_t>(g_snapshot.filter_bandwidth_hz, 3000, 30000)) - 3000) *
          kBandwidthSliderW / 27000;
  M5.Display.fillRoundRect(kBandwidthSliderX, kBandwidthSliderY,
                           std::max(9, x - kBandwidthSliderX), 18, 9, kGreen);
  M5.Display.fillCircle(x, kBandwidthSliderY + 9, 13, kGreen);
  text("3", kBandwidthSliderX, 468, kMuted, 1);
  text("30", kBandwidthSliderX + kBandwidthSliderW, 468, kMuted, 1);
}

void draw_listen_dynamic() {
  char value[48];
  M5.Display.fillRect(45, 175, 775, 180, kPanel);
  format_khz(value, sizeof(value), g_snapshot.frequency_hz);
  text(value, 390, 245, TFT_WHITE, 8);
  text("kHz", 690, 260, TFT_WHITE, 4);
  char step[16];
  format_khz(step, sizeof(step), g_snapshot.step_hz);
  snprintf(value, sizeof(value), "STEP %s kHz   BW %lu kHz", step,
           static_cast<unsigned long>(g_snapshot.filter_bandwidth_hz / 1000));
  text(value, 430, 330, kCyan, 3);
  M5.Display.fillRect(882, 215, 350, 145, kPanel);
  draw_meter(884, 220, 330, g_snapshot.relative_dbfs);
  snprintf(value, sizeof(value), "%.1f dBFS  %s", static_cast<double>(g_snapshot.relative_dbfs),
           g_snapshot.running ? "RECEIVING" : "STOPPED");
  text(value, 1058, 275, g_snapshot.running ? kGreen : TFT_RED, 2);
  draw_gain_control(true);
  snprintf(value, sizeof(value), "STEP %s kHz", step);
  button(364, 398, 210, 76, value, kCyan);
  draw_bandwidth_slider();
  for (int i = 0; i < 6; ++i) {
    char preset[20];
    if (i < g_snapshot.preset_count && g_snapshot.presets_hz[i]) {
      format_khz(preset, sizeof(preset), g_snapshot.presets_hz[i]);
    } else {
      snprintf(preset, sizeof(preset), "EMPTY");
    }
    button(24 + i * 166, 494, 154, 70, preset,
           i == g_snapshot.selected_preset ? kGreen : kGrid,
           i == g_snapshot.selected_preset);
  }
  snprintf(value, sizeof(value), "%u/%u  %u SAVED",
           static_cast<unsigned>(g_snapshot.preset_page + 1),
           static_cast<unsigned>(g_snapshot.preset_pages),
           static_cast<unsigned>(g_snapshot.preset_count));
  button(710, 570, 70, 48, "<", g_snapshot.preset_page ? kCyan : kMuted);
  M5.Display.fillRect(785, 570, 140, 48, kBg);
  text(value, 855, 594, kCyan, 2);
  button(930, 570, 70, 48, ">",
         g_snapshot.preset_page + 1 < g_snapshot.preset_pages ? kCyan : kMuted);
}

void draw_finder_static() {
  card(24, 150, 1232, 450);
  text("AM STATION FINDER", 48, 174, kCyan, 2, top_left);
  button(48, 210, 260, 70, "SCAN BAND", kGreen);
  button(328, 210, 260, 70, "PLAN 10 kHz");
  text("Scans the full band, ranks signals, then lets you review before saving.",
       620, 245, TFT_WHITE, 2, middle_left);
  button(920, 515, 290, 60, "SAVE SELECTED", kGreen);
  button(610, 515, 290, 60, "DISCARD", TFT_RED);
}

void draw_finder_dynamic() {
  char value[64];
  button(328, 210, 260, 70, g_scan_spacing == 9000 ? "PLAN 9 kHz" : "PLAN 10 kHz");
  if (g_snapshot.scan_active) {
    const unsigned percent = g_snapshot.scan_total
        ? static_cast<unsigned>(g_snapshot.scan_step) * 100u / g_snapshot.scan_total : 0;
    snprintf(value, sizeof(value), "STOP  %lu kHz  %u%%",
             static_cast<unsigned long>(g_snapshot.scan_frequency_hz / 1000), percent);
    button(48, 210, 260, 70, value, TFT_RED, true);
  } else {
    button(48, 210, 260, 70, "SCAN BAND", kGreen);
  }
  for (size_t i = 0; i < kScanResultCapacity; ++i) {
    const int x = 48 + static_cast<int>(i % 3) * 390;
    const int y = 310 + static_cast<int>(i / 3) * 90;
    if (i < g_scan_result_count) {
      snprintf(value, sizeof(value), "%s  %lu kHz  %.1f dBFS",
               g_scan_results_selected[i] ? "[X]" : "[ ]",
               static_cast<unsigned long>(g_scan_results_hz[i] / 1000),
               static_cast<double>(g_scan_results_dbfs[i]));
      button(x, y, 370, 70, value, g_scan_results_selected[i] ? kGreen : kGrid,
             g_scan_results_selected[i]);
    } else {
      button(x, y, 370, 70, "--", kGrid);
    }
  }
}

void draw_scan_prompt() {
  if (!g_scan_prompt) return;
  M5.Display.fillRoundRect(250, 190, 780, 350, 18, kPanel);
  M5.Display.drawRoundRect(250, 190, 780, 350, 18, kGreen);
  text("SCAN COMPLETE", 640, 245, TFT_WHITE, 4);
  char value[64];
  snprintf(value, sizeof(value), "%u strong station%s found",
           static_cast<unsigned>(g_scan_result_count), g_scan_result_count == 1 ? "" : "s");
  text(value, 640, 310, kCyan, 3);
  text("Choose what OrcSDR should do with the results.", 640, 355, TFT_WHITE, 2);
  button(280, 425, 220, 70, "DISCARD", TFT_RED);
  button(530, 425, 220, 70, "REVIEW", kCyan);
  button(780, 425, 220, 70, "SAVE TOP", kGreen);
}

void draw_spectrum_static() {
  card(24, 140, 1232, 470);
  text("AM CHANNEL SPECTRUM", 46, 158, kCyan, 2, top_left);
  for (int i = 0; i <= 4; ++i) {
    M5.Display.drawFastVLine(kSpectrumX + i * kSpectrumW / 4, kSpectrumY, kSpectrumH, kGrid);
    if (i) M5.Display.drawFastHLine(kSpectrumX, kSpectrumY + i * kSpectrumH / 4,
                                    kSpectrumW, kGrid);
  }
  M5.Display.drawRect(kSpectrumX, kWaterfallY, kSpectrumW, kWaterfallH, kCyan);
  M5.Display.setScrollRect(kSpectrumX + 1, kWaterfallY + 1, kSpectrumW - 2,
                           kWaterfallH - 2, kBg);
  button(390, 565, 70, 42, "-");
  button(820, 565, 70, 42, "+");
  text("SPAN", 55, 585, kCyan, 2, middle_left);
  text("TAP TO TUNE", 720, 585, TFT_WHITE, 2);
}

void draw_spectrum_dynamic() {
  char value[64];
  M5.Display.fillRect(45, 184, 1190, 58, kPanel);
  char frequency[20];
  format_khz(frequency, sizeof(frequency), g_snapshot.frequency_hz);
  snprintf(value, sizeof(value), "%s kHz", frequency);
  text(value, 55, 214, TFT_WHITE, 5, middle_left);
  char step[16];
  format_khz(step, sizeof(step), g_snapshot.step_hz);
  snprintf(value, sizeof(value), "STEP %s kHz   BW %lu kHz   SPAN %lu kHz", step,
           static_cast<unsigned long>(g_snapshot.filter_bandwidth_hz / 1000),
           static_cast<unsigned long>(g_snapshot.span_hz / 1000));
  text(value, 1220, 214, kGreen, 3, middle_right);
  draw_gain_control(true);
}

void draw_settings_static() {
  card(24, 150, 1232, 450);
  text("AM AUDIO & TUNING", 48, 174, kCyan, 2, top_left);
  button(48, 220, 250, 70, "SOUND", kGreen);
  button(320, 220, 180, 70, "VOL -");
  button(520, 220, 180, 70, "VOL +");
  button(48, 310, 310, 70, "TUNING STEP");
  button(48, 400, 310, 70, "SPECTRUM");
  button(380, 400, 310, 70, "RECORD AUDIO", TFT_RED);
  button(720, 220, 512, 70, "DEVICE SETTINGS");
  button(720, 310, 512, 70, "HOME", kGreen, true);
}

void draw_settings_dynamic() {
  char value[96];
  char step[16];
  format_khz(step, sizeof(step), g_snapshot.step_hz);
  M5.Display.fillRect(48, 476, 1184, 112, kPanel);
  draw_gain_control(false);
  snprintf(value, sizeof(value), "VOL %u   TUNE STEP %s kHz   BW %lu kHz   GRAPHICS %s   RECORD %s",
           g_snapshot.volume, step,
           static_cast<unsigned long>(g_snapshot.filter_bandwidth_hz / 1000),
           g_snapshot.graphics_enabled ? "ON" : "OFF", g_snapshot.recording ? "ON" : "OFF");
  text(value, 640, 572, TFT_WHITE, 2);
}

void draw_view() {
  M5.Display.clearScrollRect();
  M5.Display.fillRect(0, kHeaderH, 1280, 720 - kHeaderH, kBg);
  if (g_view == View::listen) draw_listen_static();
  else if (g_view == View::finder) draw_finder_static();
  else if (g_view == View::spectrum) draw_spectrum_static();
  else draw_settings_static();
  draw_tabs();
}

void draw_dynamic() {
  if (g_view == View::listen) draw_listen_dynamic();
  else if (g_view == View::finder) draw_finder_dynamic();
  else if (g_view == View::spectrum) draw_spectrum_dynamic();
  else draw_settings_dynamic();
  if (g_preset_modal) draw_preset_modal();
  if (g_scan_prompt) draw_scan_prompt();
}

uint16_t waterfall_color(float level) {
  level = std::clamp(level, 0.0f, 1.0f);
  const uint8_t r = level < 0.55f ? 0 : static_cast<uint8_t>((level - 0.55f) * 566);
  const uint8_t g = level < 0.2f ? 0 : static_cast<uint8_t>(std::min(255.0f, (level - 0.2f) * 510));
  const uint8_t b = level < 0.65f ? static_cast<uint8_t>((0.65f - level) * 390) : 0;
  return M5.Display.color565(r, g, b);
}

}  // namespace

void enter(const Snapshot& snapshot) {
  g_snapshot = snapshot;
  g_view = View::listen;
  g_active = true;
  g_preset_modal = false;
  g_preset_pressed = false;
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
  draw_view();
  draw_dynamic();
}

void update(const Snapshot& snapshot) {
  if (!g_active) return;
  const bool header_changed = snapshot.battery_percent != g_snapshot.battery_percent ||
                              snapshot.volume != g_snapshot.volume ||
                              snapshot.sound_enabled != g_snapshot.sound_enabled;
  g_snapshot = snapshot;
  const uint32_t now = millis();
  if (header_changed || audio_header::service_timeout(g_audio_control, now))
    audio_header::draw(g_audio_control, g_snapshot.volume, g_snapshot.sound_enabled,
                       g_snapshot.battery_percent);
  if (g_scan_prompt) {
    if (g_scan_prompt_draw_pending.exchange(false)) draw_dynamic();
    return;
  }
  if (g_preset_modal) return;
  if (now - g_last_dynamic_ms < 150) return;
  g_last_dynamic_ms = now;
  draw_dynamic();
}

void draw_spectrum(const float* levels, size_t first_bin, size_t visible_bins, float floor) {
  if (!spectrum_active() || levels == nullptr || visible_bins < 2) return;
  M5.Display.startWrite();
  M5.Display.fillRect(kSpectrumX + 1, kSpectrumY + 1, kSpectrumW - 2, kSpectrumH - 2, kBg);
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
    for (int p = x0; p < x1; ++p) g_waterfall_row[p] = waterfall_color(normalized);
  }
  const int center = kSpectrumX + kSpectrumW / 2;
  const int half_filter = std::clamp(static_cast<int>(
      static_cast<uint64_t>(g_snapshot.filter_bandwidth_hz) * kSpectrumW /
      (2u * (g_snapshot.span_hz ? g_snapshot.span_hz : 1u))), 3, kSpectrumW / 2 - 2);
  M5.Display.drawFastVLine(center, kSpectrumY, kSpectrumH, kCyan);
  M5.Display.drawFastVLine(center - half_filter, kSpectrumY, kSpectrumH, kYellow);
  M5.Display.drawFastVLine(center + half_filter, kSpectrumY, kSpectrumH, kYellow);
  M5.Display.scroll(0, -1);
  M5.Display.pushImage(kSpectrumX, kWaterfallY + kWaterfallH - 2, kSpectrumW, 1,
                       g_waterfall_row);
  M5.Display.endWrite();
}

Action handle_touch(int32_t x, int32_t y) {
  if (!g_active) return {};
  if (g_scan_prompt) {
    if (hit(x, y, 280, 425, 220, 70)) reset_scan_results();
    else if (hit(x, y, 530, 425, 220, 70)) {
      g_scan_prompt = false;
      g_view = View::finder;
    }
    else if (hit(x, y, 780, 425, 220, 70)) (void)save_scan_results();
    draw();
    return {};
  }
  const auto audio_action = audio_header::handle_touch(g_audio_control, x, y, millis());
  if (audio_action != audio_header::Action::none) {
    if (audio_action == audio_header::Action::opened || audio_action == audio_header::Action::closed) {
      audio_header::draw(g_audio_control, g_snapshot.volume, g_snapshot.sound_enabled,
                         g_snapshot.battery_percent);
      return {};
    }
    if (audio_action == audio_header::Action::volume_down) return {ActionKind::volume_down};
    if (audio_action == audio_header::Action::sound_toggle) return {ActionKind::sound_toggle};
    return {ActionKind::volume_up};
  }
  if (audio_header::settings_hit(x, y)) return {ActionKind::open_device_settings};
  if (y >= kTabsY) {
    const uint8_t next = std::min<uint8_t>(x / kTabW, static_cast<uint8_t>(View::count) - 1);
    if (next != static_cast<uint8_t>(g_view)) {
      g_view = static_cast<View>(next);
      draw();
    }
    return {};
  }
  if (g_view != View::finder) {
    const GainLayout layout = gain_layout();
    if (hit(x, y, layout.auto_x, layout.auto_y, layout.auto_w, layout.auto_h))
      return {ActionKind::gain_auto};
  }
  if (g_view == View::listen) {
    if (hit(x, y, 24, 398, 160, 76)) return {ActionKind::step_down};
    if (hit(x, y, 194, 398, 160, 76)) return {ActionKind::step_up};
    if (hit(x, y, 364, 398, 210, 76)) return {ActionKind::step_cycle};
    if (hit(x, y, 1020, 494, 236, 70)) return {ActionKind::preset_save};
    if (hit(x, y, 710, 570, 70, 48) && g_preset_page > 0) {
      --g_preset_page;
      populate_presets(g_snapshot);
      draw();
    } else if (hit(x, y, 930, 570, 70, 48) && g_preset_page + 1 < preset_pages()) {
      ++g_preset_page;
      populate_presets(g_snapshot);
      draw();
    }
  } else if (g_view == View::finder) {
    if (hit(x, y, 48, 210, 260, 70)) {
      reset_scan_results();
      return {ActionKind::scan_toggle};
    }
    if (hit(x, y, 328, 210, 260, 70)) return {ActionKind::scan_spacing_toggle};
    for (size_t i = 0; i < g_scan_result_count; ++i) {
      const int bx = 48 + static_cast<int>(i % 3) * 390;
      const int by = 310 + static_cast<int>(i / 3) * 90;
      if (!hit(x, y, bx, by, 370, 70)) continue;
      if (x < bx + 70) {
        g_scan_results_selected[i] = !g_scan_results_selected[i];
        draw_finder_dynamic();
        return {};
      }
      return {ActionKind::tune_hz, g_scan_results_hz[i]};
    }
    if (hit(x, y, 610, 515, 290, 60)) {
      reset_scan_results();
      draw();
    } else if (hit(x, y, 920, 515, 290, 60)) {
      (void)save_scan_results();
      draw();
    }
  } else if (g_view == View::spectrum) {
    if (hit(x, y, 390, 565, 70, 42)) return {ActionKind::span_down};
    if (hit(x, y, 820, 565, 70, 42)) return {ActionKind::span_up};
    if (hit(x, y, kSpectrumX, kSpectrumY, kSpectrumW, kSpectrumH + kWaterfallH + 30)) {
      const int64_t offset = static_cast<int64_t>(x - (kSpectrumX + kSpectrumW / 2)) *
                             g_snapshot.span_hz / kSpectrumW;
      const int64_t selected = static_cast<int64_t>(g_snapshot.frequency_hz) + offset;
      return {ActionKind::tune_hz, static_cast<uint32_t>(std::clamp<int64_t>(
          selected, receiver_bands::kAmBroadcast.min_hz, receiver_bands::kAmBroadcast.max_hz))};
    }
  } else {
    if (hit(x, y, 48, 220, 250, 70)) return {ActionKind::sound_toggle};
    if (hit(x, y, 320, 220, 180, 70)) return {ActionKind::volume_down};
    if (hit(x, y, 520, 220, 180, 70)) return {ActionKind::volume_up};
    if (hit(x, y, 48, 310, 310, 70)) return {ActionKind::step_cycle};
    if (hit(x, y, 48, 400, 310, 70)) return {ActionKind::graphics_toggle};
    if (hit(x, y, 380, 400, 310, 70)) return {ActionKind::recording_toggle};
    if (hit(x, y, 720, 220, 512, 70)) return {ActionKind::open_device_settings};
    if (hit(x, y, 720, 310, 512, 70)) return {ActionKind::exit_home};
  }
  return {};
}

TouchResult handle_preset_touch(int32_t x, int32_t y, bool pressed, uint32_t now_ms) {
  if (!g_active || g_view != View::listen) {
    g_preset_pressed = false;
    return {};
  }
  if (g_preset_modal) {
    if (pressed && !g_preset_pressed) {
      g_preset_pressed = true;
      if (hit(x, y, 320, 400, 200, 70)) {
        g_preset_modal = false;
        draw();
      } else if (hit(x, y, 540, 400, 200, 70)) {
        g_preset_modal = false;
        return {{ActionKind::preset_replace, g_edit_preset}, true};
      } else if (hit(x, y, 760, 400, 200, 70)) {
        g_preset_modal = false;
        return {{ActionKind::preset_delete, g_edit_preset}, true};
      }
    } else if (!pressed) {
      g_preset_pressed = false;
    }
    return {{}, true};
  }

  if (pressed && !g_preset_pressed) {
    for (size_t slot = 0; slot < kPresetsPerPage; ++slot) {
      if (!hit(x, y, 24 + static_cast<int>(slot) * 166, 494, 154, 70)) continue;
      g_preset_pressed = true;
      g_pressed_preset = static_cast<uint16_t>(g_preset_page * kPresetsPerPage + slot);
      g_preset_press_valid = g_pressed_preset < g_preset_count;
      g_preset_press_ms = now_ms;
      return {{}, true};
    }
    return {};
  }
  if (pressed && g_preset_pressed) {
    const size_t slot = g_pressed_preset % kPresetsPerPage;
    if (!hit(x, y, 24 + static_cast<int>(slot) * 166, 494, 154, 70))
      g_preset_press_valid = false;
    if (g_preset_press_valid && now_ms - g_preset_press_ms >= kPresetHoldMs) {
      g_preset_press_valid = false;
      g_edit_preset = g_pressed_preset;
      g_preset_modal = true;
      draw_preset_modal();
    }
    return {{}, true};
  }
  if (!pressed && g_preset_pressed) {
    g_preset_pressed = false;
    if (g_preset_press_valid) {
      g_preset_press_valid = false;
      return {{ActionKind::preset_recall, g_pressed_preset}, true};
    }
    return {{}, true};
  }
  return {};
}

Action handle_gain_drag(int32_t x, int32_t y) {
  const GainLayout layout = gain_layout();
  if (!g_active || g_view == View::finder ||
      !hit(x, y, layout.slider_x - 14, layout.slider_y - 24, layout.slider_w + 28, 66) ||
      g_snapshot.gain_step_count == 0) return {};
  const int raw_index = static_cast<int>(x - layout.slider_x) *
                        static_cast<int>(g_snapshot.gain_step_count) / layout.slider_w;
  const size_t index = static_cast<size_t>(std::clamp(
      raw_index, 0, static_cast<int>(g_snapshot.gain_step_count) - 1));
  const int gain = g_snapshot.gain_steps_tenth_db[index];
  if (!g_snapshot.gain_auto && gain == g_snapshot.gain_tenth_db) return {};
  return {ActionKind::gain_tenth_db, static_cast<uint32_t>(gain)};
}

Action handle_bandwidth_drag(int32_t x, int32_t y) {
  if (!g_active || g_view != View::listen ||
      !hit(x, y, kBandwidthSliderX - 18, kBandwidthSliderY - 24,
           kBandwidthSliderW + 36, 66))
    return {};
  const uint32_t bandwidth_hz = bandwidth_for_x(x);
  if (bandwidth_hz == g_snapshot.filter_bandwidth_hz) return {};
  return {ActionKind::filter_bandwidth_hz, bandwidth_hz};
}

bool active() { return g_active; }
bool spectrum_active() { return g_active && g_view == View::spectrum; }
View view() { return g_view; }

void load(NvsStore& store) {
  g_store = &store;
  g_saved_frequency = std::clamp(store.get_u32("sdr_am_hz", g_saved_frequency),
                                 receiver_bands::kAmBroadcast.min_hz,
                                 receiver_bands::kAmBroadcast.max_hz);
  const uint32_t stored_tune_step = store.get_u32("am_tune_step", g_tune_step);
  static constexpr uint32_t tune_steps[] = {100, 500, 1000, 5000, 9000, 10000};
  g_tune_step = std::find(std::begin(tune_steps), std::end(tune_steps), stored_tune_step) !=
                        std::end(tune_steps)
                    ? stored_tune_step
                    : 1000;
  const uint32_t spacing = store.get_u32("sdr_am_step", g_scan_spacing);
  g_scan_spacing = spacing == 9000 ? 9000 : 10000;
  g_preset_count = 0;
  g_preset_page = 0;
  std::fill(std::begin(g_presets), std::end(g_presets), 0u);
  const size_t stored_bytes = store.bytes_length("am_presets");
  if (stored_bytes > 0 && stored_bytes <= sizeof(g_presets) &&
      stored_bytes % sizeof(g_presets[0]) == 0 &&
      store.get_bytes("am_presets", g_presets, stored_bytes) == stored_bytes) {
    const uint16_t legacy_count = store.get_u8("am_preset_count", 0);
    const uint16_t count = std::min<uint16_t>(
        store.get_u16("am_preset_n", legacy_count),
        stored_bytes / sizeof(g_presets[0]));
    bool valid = true;
    for (uint16_t i = 0; i < count; ++i)
      valid &= g_presets[i] >= receiver_bands::kAmBroadcast.min_hz &&
               g_presets[i] <= receiver_bands::kAmBroadcast.max_hz;
    if (valid) g_preset_count = count;
    else std::fill(std::begin(g_presets), std::end(g_presets), 0u);
  }
}

uint32_t saved_frequency() { return g_saved_frequency; }
uint32_t tune_step() { return g_tune_step; }
uint32_t scan_spacing() { return g_scan_spacing; }

void note_tuned(uint32_t frequency_hz) {
  g_saved_frequency = std::clamp(frequency_hz, receiver_bands::kAmBroadcast.min_hz,
                                 receiver_bands::kAmBroadcast.max_hz);
  if (g_store) (void)g_store->put_u32("sdr_am_hz", g_saved_frequency);
}

uint32_t cycle_tune_step() {
  static constexpr uint32_t steps[] = {100, 500, 1000, 5000, 9000, 10000};
  const auto current = std::find(std::begin(steps), std::end(steps), g_tune_step);
  g_tune_step = current == std::end(steps) || current + 1 == std::end(steps)
                    ? steps[0]
                    : *(current + 1);
  if (g_store) (void)g_store->put_u32("am_tune_step", g_tune_step);
  return g_tune_step;
}

uint32_t toggle_scan_spacing() {
  g_scan_spacing = g_scan_spacing == 10000 ? 9000 : 10000;
  if (g_store) (void)g_store->put_u32("sdr_am_step", g_scan_spacing);
  return g_scan_spacing;
}

uint32_t preset(size_t index) { return index < g_preset_count ? g_presets[index] : 0; }

void save_current_preset() {
  for (size_t i = 0; i < g_preset_count; ++i)
    if (g_presets[i] == g_saved_frequency) return;
  if (g_preset_count == std::size(g_presets)) return;
  g_presets[g_preset_count++] = g_saved_frequency;
  g_preset_page = static_cast<uint8_t>((g_preset_count - 1) / kPresetsPerPage);
  persist_presets();
}

bool replace_preset(size_t index) {
  if (index >= g_preset_count) return false;
  const auto duplicate = std::find(g_presets, g_presets + g_preset_count, g_saved_frequency);
  if (duplicate != g_presets + g_preset_count &&
      static_cast<size_t>(duplicate - g_presets) != index)
    return false;
  g_presets[index] = g_saved_frequency;
  persist_presets();
  return true;
}

bool delete_preset(size_t index) {
  size_t count = g_preset_count;
  if (!erase_preset(g_presets, count, index)) return false;
  g_preset_count = static_cast<uint16_t>(count);
  g_preset_page = std::min<uint8_t>(g_preset_page, preset_pages() - 1);
  persist_presets();
  return true;
}

uint8_t prepare_scan_results(uint32_t start_hz, uint32_t step_hz,
                             const float* levels, size_t count, float* baseline_dbfs) {
  ScanCandidate candidates[kScanResultCapacity]{};
  const size_t found = select_scan_candidates(levels, count, candidates,
                                               std::size(candidates), baseline_dbfs);
  reset_scan_results();
  g_scan_result_count = static_cast<uint8_t>(found);
  for (size_t i = 0; i < found; ++i) {
    g_scan_results_hz[i] =
        start_hz + static_cast<uint32_t>(candidates[i].index) * step_hz;
    g_scan_results_dbfs[i] = candidates[i].level;
    g_scan_results_selected[i] = true;
  }
  g_scan_prompt = true;
  g_scan_prompt_draw_pending = true;
  return g_scan_result_count;
}

void clear_scan_results() { reset_scan_results(); }

bool scan_prompt_active() { return g_scan_prompt; }

bool auto_gain_should_advance(float level_dbfs, size_t step, size_t step_count) {
  return step + 1 < step_count && level_dbfs < kAutoGainTargetDbfs;
}

void populate_presets(Snapshot& snapshot) {
  snapshot.preset_count = g_preset_count;
  g_preset_page = std::min<uint8_t>(g_preset_page, preset_pages() - 1);
  snapshot.preset_page = g_preset_page;
  snapshot.preset_pages = preset_pages();
  snapshot.selected_preset = -1;
  const size_t first = static_cast<size_t>(g_preset_page) * kPresetsPerPage;
  for (size_t i = 0; i < kPresetsPerPage; ++i) {
    const size_t index = first + i;
    snapshot.presets_hz[i] = index < g_preset_count ? g_presets[index] : 0;
    if (snapshot.presets_hz[i] == snapshot.frequency_hz)
      snapshot.selected_preset = static_cast<int8_t>(i);
  }
}

bool self_check() {
  const float scan_levels[] = {
      -80.0f, -70.0f, -80.0f, -60.0f, -80.0f, -50.0f, -80.0f};
  const float quiet_levels[] = {-80.0f, -80.0f, -80.0f};
  ScanCandidate candidates[2]{};
  float baseline = 0.0f;
  uint32_t presets[] = {590000, 1050000, 1280000};
  size_t preset_count = std::size(presets);
  return static_cast<uint8_t>(View::count) == 4 &&
         receiver_bands::valid(receiver_bands::kAmBroadcast) &&
         kSpectrumX + kSpectrumW <= 1280 && kGainSliderX + kGainSliderW <= 1280 &&
         kTabsY < 720 && audio_header::self_check() &&
          select_scan_candidates(scan_levels, std::size(scan_levels), candidates,
                                 std::size(candidates), &baseline) == 2 &&
          baseline == -80.0f && candidates[0].index == 3 && candidates[1].index == 5 &&
          select_scan_candidates(quiet_levels, std::size(quiet_levels), candidates,
                                 std::size(candidates), nullptr) == 0 &&
          auto_gain_should_advance(-30.0f, 0, 3) &&
          !auto_gain_should_advance(kAutoGainTargetDbfs, 0, 3) &&
          !auto_gain_should_advance(-30.0f, 2, 3) &&
          bandwidth_for_x(kBandwidthSliderX) == 3000 &&
          bandwidth_for_x(kBandwidthSliderX + kBandwidthSliderW) == 30000 &&
          erase_preset(presets, preset_count, 1) && preset_count == 2 &&
          presets[0] == 590000 && presets[1] == 1280000 && presets[2] == 0;
}

}  // namespace orcsdr::am
