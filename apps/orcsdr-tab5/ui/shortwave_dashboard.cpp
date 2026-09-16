#include "shortwave_dashboard.hpp"

#include "dashboard_audio_control.hpp"
#include "shortwave_model.hpp"
#include "spectrum_resample.hpp"

#include <M5Unified.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace orcsdr::shortwave {
namespace {

constexpr uint16_t kPanel = 0x0841;
constexpr uint16_t kCyan = 0x2e7f;
constexpr uint16_t kGreen = 0x6fe8;
constexpr uint16_t kYellow = 0xff24;
constexpr uint16_t kMuted = 0x8c71;
constexpr uint16_t kGrid = 0x2945;
constexpr int kSpectrumX = 24;
constexpr int kSpectrumY = 340;
constexpr int kSpectrumW = 792;
constexpr int kSpectrumH = 145;
constexpr int kWaterfallY = 493;
constexpr int kWaterfallH = 91;
constexpr int kTabsY = 630;
constexpr int kTabW = 256;
constexpr int kGainX = 872;
constexpr int kGainY = 488;
constexpr int kGainW = 350;

Snapshot g_snapshot{};
bool g_active = false;
bool g_keypad = false;
uint32_t g_saved_frequency = 7100000;
char g_entry[12]{};
EXT_RAM_BSS_ATTR uint16_t g_waterfall_row[kSpectrumW]{};

int spectrum_x_for_bin(size_t bin, size_t visible_bins) {
  return kSpectrumX + static_cast<int>(bin * (kSpectrumW - 1) /
                                       (visible_bins > 1 ? visible_bins - 1 : 1));
}

bool hit(int32_t x, int32_t y, int bx, int by, int bw, int bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

void text(const char* value, int x, int y, uint16_t color = TFT_WHITE,
          int size = 2, textdatum_t datum = middle_center) {
  M5.Display.setTextDatum(datum);
  M5.Display.setTextSize(size);
  M5.Display.setTextColor(color);
  M5.Display.drawString(value, x, y);
}

void card(int x, int y, int w, int h) {
  M5.Display.fillRoundRect(x, y, w, h, 10, kPanel);
  M5.Display.drawRoundRect(x, y, w, h, 10, kCyan);
}

void button(int x, int y, int w, int h, const char* label, bool selected = false,
            bool enabled = true) {
  const uint16_t color = enabled ? (selected ? kGreen : kCyan) : TFT_DARKGREY;
  M5.Display.fillRoundRect(x, y, w, h, 8, selected ? 0x1264 : kPanel);
  M5.Display.drawRoundRect(x, y, w, h, 8, color);
  text(label, x + w / 2, y + h / 2, enabled ? TFT_WHITE : kMuted, 2);
}

const char* route_name(ReceiverRoute route) {
  switch (route) {
    case ReceiverRoute::direct_q: return "DIRECT Q SAMPLING";
    case ReceiverRoute::hf_upconverter: return "V4 HF UPCONVERTER";
    case ReceiverRoute::tuner: return "NORMAL TUNER";
    default: return "ROUTE UNKNOWN";
  }
}

void draw_frequency() {
  M5.Display.fillRect(120, 120, 565, 92, kPanel);
  char value[40];
  snprintf(value, sizeof(value), "%lu.%03lu.%03lu MHz",
           static_cast<unsigned long>(g_snapshot.frequency_hz / 1000000u),
           static_cast<unsigned long>((g_snapshot.frequency_hz / 1000u) % 1000u),
           static_cast<unsigned long>(g_snapshot.frequency_hz % 1000u));
  text(value, 402, 159, TFT_WHITE, 4);
  const BroadcastBand* sw_band = band_for(g_snapshot.frequency_hz);
  text(sw_band ? sw_band->label : "GENERAL HF", 402, 198, kGreen, 2);
}

void draw_status() {
  M5.Display.fillRect(862, 111, 374, 48, kPanel);
  text(route_name(g_snapshot.controls.route), 1049, 126, kGreen, 2);
  text(g_snapshot.device[0] ? g_snapshot.device : "NO RTL-SDR", 1049, 150,
       g_snapshot.driver_ready ? TFT_WHITE : TFT_ORANGE, 1);
  M5.Display.fillRect(35, 294, 760, 34, TFT_BLACK);
  char status[96];
  snprintf(status, sizeof(status), "%s  |  %+.1f dBFS  |  %lu kHz span%s",
           g_snapshot.running ? "LIVE IQ" : "WAITING",
           static_cast<double>(g_snapshot.relative_dbfs),
           static_cast<unsigned long>(g_snapshot.span_hz / 1000u),
           g_snapshot.clipping_percent > 0.1f ? "  |  CLIP" : "");
  text(status, 415, 311,
       g_snapshot.clipping_percent > 0.1f ? TFT_RED
                                         : g_snapshot.running ? kGreen : TFT_ORANGE,
       2);
}

void draw_quick_controls() {
  char value[40];
  snprintf(value, sizeof(value), "STEP %lu Hz",
           static_cast<unsigned long>(g_snapshot.step_hz));
  button(24, 246, 236, 56, value);
  snprintf(value, sizeof(value), "AM FILTER %.1f kHz",
           static_cast<double>(g_snapshot.filter_bandwidth_hz) / 1000.0);
  button(278, 246, 236, 56, value);
  button(532, 246, 284, 56,
         g_snapshot.sound_enabled ? "SOUND ON" : "SOUND OFF",
         g_snapshot.sound_enabled);
}

void draw_controls() {
  const auto tuner = receiver_controls::item(receiver_controls::Control::tuner_agc,
                                              g_snapshot.controls);
  const auto rtl = receiver_controls::item(receiver_controls::Control::rtl_agc,
                                            g_snapshot.controls);
  const auto boost = receiver_controls::item(receiver_controls::Control::audio_boost,
                                              g_snapshot.controls);
  const auto gain = receiver_controls::item(receiver_controls::Control::rf_gain,
                                             g_snapshot.controls);
  button(858, 176, 184, 68, "TUNER AGC", tuner.active,
         tuner.availability == receiver_controls::Availability::enabled);
  button(1052, 176, 184, 68, "RTL AGC", rtl.active,
         rtl.availability == receiver_controls::Availability::enabled);
  button(858, 258, 184, 68, "AUDIO BOOST", boost.active);
  button(1052, 258, 72, 68, "VOL -");
  button(1164, 258, 72, 68, "VOL +");
  char volume[24];
  snprintf(volume, sizeof(volume), "VOLUME %u", g_snapshot.controls.volume);
  text(volume, 1144, 345, TFT_WHITE, 2);

  M5.Display.fillRect(858, 378, 378, 184, kPanel);
  text("RF GAIN", 858, 382,
       gain.availability == receiver_controls::Availability::enabled ? kCyan : kMuted,
       2, top_left);
  if (gain.availability != receiver_controls::Availability::enabled) {
    text(gain.explanation, 1047, 440, kMuted, 2);
    text("Audio boost remains available", 1047, 474, TFT_LIGHTGREY, 1);
    return;
  }
  char gain_value[32];
  snprintf(gain_value, sizeof(gain_value), g_snapshot.controls.tuner_agc
                                                   ? "AUTO %.1f dB"
                                                   : "MANUAL %.1f dB",
           static_cast<double>(g_snapshot.controls.gain_tenth_db) / 10.0);
  text(gain_value, 1047, 426, g_snapshot.controls.tuner_agc ? kGreen : TFT_WHITE, 2);
  M5.Display.drawRoundRect(kGainX, kGainY, kGainW, 22, 10, kCyan);
  int position = 0;
  if (g_snapshot.gain_step_count > 1) {
    size_t nearest = 0;
    for (size_t i = 1; i < g_snapshot.gain_step_count; ++i)
      if (std::abs(g_snapshot.gain_steps_tenth_db[i] - g_snapshot.controls.gain_tenth_db) <
          std::abs(g_snapshot.gain_steps_tenth_db[nearest] - g_snapshot.controls.gain_tenth_db))
        nearest = i;
    position = static_cast<int>(nearest * kGainW / (g_snapshot.gain_step_count - 1));
  }
  M5.Display.fillCircle(kGainX + position, kGainY + 11, 12,
                        g_snapshot.controls.tuner_agc ? kMuted : kGreen);
  text("Tap TUNER AGC for auto; drag for manual", 1047, 535, TFT_LIGHTGREY, 1);
}

void draw_static() {
  M5.Display.clearScrollRect();
  M5.Display.fillScreen(TFT_BLACK);
  audio_header::draw_brand("SHORTWAVE EXPLORER");
  M5.Display.drawFastVLine(350, 18, 58, kCyan);
  text("SHORTWAVE", 390, 42, TFT_WHITE, 4, middle_left);
  audio_header::draw_battery(g_snapshot.battery_percent);
  audio_header::draw_home_button();
  audio_header::draw_mute_button(g_snapshot.sound_enabled);
  audio_header::draw_visualizer_button(g_snapshot.running);
  audio_header::draw_settings_button();
  M5.Display.drawFastHLine(20, 92, 1240, kGreen);

  card(24, 110, 792, 122);
  button(42, 128, 64, 72, "-");
  button(734, 128, 64, 72, "+");
  card(840, 110, 420, 474);
  M5.Display.drawRect(kSpectrumX, kSpectrumY, kSpectrumW, kSpectrumH, kGrid);
  for (int i = 1; i < 8; ++i)
    M5.Display.drawFastVLine(kSpectrumX + i * kSpectrumW / 8, kSpectrumY,
                            kSpectrumH, kGrid);
  for (int i = 1; i < 4; ++i)
    M5.Display.drawFastHLine(kSpectrumX, kSpectrumY + i * kSpectrumH / 4,
                            kSpectrumW, kGrid);
  M5.Display.drawRect(kSpectrumX, kWaterfallY, kSpectrumW, kWaterfallH, kCyan);
  M5.Display.setScrollRect(kSpectrumX + 1, kWaterfallY + 1, kSpectrumW - 2,
                           kWaterfallH - 2, TFT_BLACK);

  constexpr const char* tabs[] = {"LIVE", "ON AIR", "HUNT", "MEMORY", "LOGBOOK"};
  for (int i = 0; i < 5; ++i) {
    button(i * kTabW + 4, kTabsY + 4, kTabW - 8, 82, tabs[i], i == 0, i == 0);
    if (i) text("LATER", i * kTabW + kTabW / 2, kTabsY + 68, kMuted, 1);
  }
}

void draw_keypad() {
  M5.Display.clearScrollRect();
  M5.Display.fillRect(0, 93, 1280, 627, TFT_BLACK);
  card(340, 135, 600, 470);
  text("ENTER SHORTWAVE FREQUENCY (MHz)", 640, 168, kCyan, 2);
  char field[24];
  snprintf(field, sizeof(field), "%s%s", g_entry, g_entry[0] ? " MHz" : "");
  M5.Display.fillRoundRect(380, 200, 520, 58, 8, TFT_NAVY);
  text(field[0] ? field : "0.024 - 30.000", 640, 229, TFT_WHITE, 3);
  static constexpr char keys[] = {'1','2','3','4','5','6','7','8','9','.','0','<'};
  for (int i = 0; i < 12; ++i) {
    char key[2] = {keys[i], 0};
    button(380 + (i % 3) * 174, 275 + (i / 3) * 60, 160, 50, key);
  }
  button(380, 525, 250, 55, "CANCEL");
  button(650, 525, 250, 55, "TUNE", true);
}

uint16_t waterfall_color(float level) {
  level = std::clamp(level, 0.0f, 1.0f);
  const uint8_t r = level < 0.55f ? 0 : static_cast<uint8_t>((level - 0.55f) * 566);
  const uint8_t g = level < 0.2f ? 0 : static_cast<uint8_t>(
      std::min(255.0f, (level - 0.2f) * 510));
  const uint8_t b = level < 0.65f ? static_cast<uint8_t>((0.65f - level) * 390) : 0;
  return M5.Display.color565(r, g, b);
}

}  // namespace

void enter(const Snapshot& snapshot) {
  g_snapshot = snapshot;
  g_saved_frequency = snapshot.frequency_hz;
  g_active = true;
  g_keypad = false;
  g_entry[0] = '\0';
  draw();
}

void leave() {
  M5.Display.clearScrollRect();
  g_active = false;
}

void draw() {
  if (!g_active) return;
  draw_static();
  if (g_keypad) {
    draw_keypad();
    return;
  }
  draw_frequency();
  draw_status();
  draw_controls();
  draw_quick_controls();
}

void update(const Snapshot& snapshot) {
  if (!g_active) return;
  const bool controls_changed =
      snapshot.controls.route != g_snapshot.controls.route ||
      snapshot.controls.capabilities.rf_gain != g_snapshot.controls.capabilities.rf_gain ||
      snapshot.controls.capabilities.tuner_agc != g_snapshot.controls.capabilities.tuner_agc ||
      snapshot.controls.capabilities.rtl_agc != g_snapshot.controls.capabilities.rtl_agc ||
      snapshot.controls.tuner_agc != g_snapshot.controls.tuner_agc ||
      snapshot.controls.rtl_agc != g_snapshot.controls.rtl_agc ||
      snapshot.controls.audio_boost != g_snapshot.controls.audio_boost ||
      snapshot.controls.volume != g_snapshot.controls.volume ||
      snapshot.controls.gain_tenth_db != g_snapshot.controls.gain_tenth_db ||
      snapshot.gain_step_count != g_snapshot.gain_step_count;
  g_snapshot = snapshot;
  g_saved_frequency = snapshot.frequency_hz;
  draw_frequency();
  draw_status();
  draw_quick_controls();
  if (controls_changed) draw_controls();
}

void draw_spectrum(const float* levels, size_t first_bin, size_t visible_bins,
                   float floor) {
  if (!g_active || !levels || visible_bins < 2) return;
  M5.Display.startWrite();
  M5.Display.fillRect(kSpectrumX + 1, kSpectrumY + 1, kSpectrumW - 2,
                      kSpectrumH - 2, TFT_BLACK);
  int last_x = kSpectrumX;
  int last_y = kSpectrumY + kSpectrumH - 2;
  for (size_t i = 0; i < kSpectrumW; ++i) {
    const float level = spectrum::peak_for_pixel(
        levels, first_bin, visible_bins, i, kSpectrumW);
    const float normalized = std::clamp((level - floor) / 48.0f,
                                        0.0f, 1.0f);
    const int x = kSpectrumX + static_cast<int>(i);
    const int y = kSpectrumY + kSpectrumH - 2 -
                  static_cast<int>(normalized * (kSpectrumH - 4));
    if (i) M5.Display.drawLine(last_x, last_y, x, y, kGreen);
    last_x = x;
    last_y = y;
    g_waterfall_row[i] = waterfall_color(normalized);
  }
  const int center = kSpectrumX + kSpectrumW / 2;
  const int half_filter = std::clamp(static_cast<int>(
      static_cast<uint64_t>(g_snapshot.filter_bandwidth_hz) * kSpectrumW /
      (2u * (g_snapshot.span_hz ? g_snapshot.span_hz : 1u))), 3,
      kSpectrumW / 2 - 2);
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
  if (g_keypad) {
    if (hit(x, y, 380, 525, 250, 55)) {
      g_keypad = false;
      g_entry[0] = '\0';
      draw();
      return {};
    }
    if (hit(x, y, 650, 525, 250, 55)) {
      char* end = nullptr;
      const double mhz = strtod(g_entry, &end);
      if (end != g_entry && *end == '\0' && mhz >= 0.024 && mhz <= 30.0) {
        g_keypad = false;
        const uint32_t hz = static_cast<uint32_t>(llround(mhz * 1000000.0));
        g_entry[0] = '\0';
        draw();
        return {ActionKind::tune_hz, static_cast<int32_t>(hz)};
      }
      return {};
    }
    static constexpr char keys[] = {'1','2','3','4','5','6','7','8','9','.','0','\b'};
    for (int i = 0; i < 12; ++i) {
      if (!hit(x, y, 380 + (i % 3) * 174, 275 + (i / 3) * 60, 160, 50)) continue;
      const size_t n = strlen(g_entry);
      if (keys[i] == '\b') {
        if (n) g_entry[n - 1] = '\0';
      } else if (n + 1 < sizeof(g_entry) &&
                 (keys[i] != '.' || strchr(g_entry, '.') == nullptr)) {
        g_entry[n] = keys[i];
        g_entry[n + 1] = '\0';
      }
      draw_keypad();
      return {};
    }
    return {};
  }
  if (audio_header::home_hit(x, y)) return {ActionKind::exit_home};
  if (audio_header::settings_hit(x, y)) return {ActionKind::open_settings};
  if (hit(x, y, 42, 128, 64, 72)) return {ActionKind::step_down};
  if (hit(x, y, 734, 128, 64, 72)) return {ActionKind::step_up};
  if (hit(x, y, 120, 120, 565, 92)) {
    g_keypad = true;
    g_entry[0] = '\0';
    draw();
    return {};
  }
  if (hit(x, y, 24, 246, 236, 56)) return {ActionKind::step_cycle};
  if (hit(x, y, 278, 246, 236, 56)) return {ActionKind::filter_cycle};
  if (hit(x, y, 532, 246, 284, 56)) return {ActionKind::sound_toggle};
  if (hit(x, y, 858, 176, 184, 68) &&
      receiver_controls::action(receiver_controls::Control::tuner_agc,
                                g_snapshot.controls).kind !=
          receiver_controls::ActionKind::none)
    return {ActionKind::gain_auto};
  if (hit(x, y, 1052, 176, 184, 68) &&
      receiver_controls::action(receiver_controls::Control::rtl_agc,
                                g_snapshot.controls).kind !=
          receiver_controls::ActionKind::none)
    return {ActionKind::rtl_agc, !g_snapshot.controls.rtl_agc};
  if (hit(x, y, 858, 258, 184, 68))
    return {ActionKind::audio_boost, !g_snapshot.controls.audio_boost};
  if (hit(x, y, 1052, 258, 72, 68)) return {ActionKind::volume_down};
  if (hit(x, y, 1164, 258, 72, 68)) return {ActionKind::volume_up};
  if (hit(x, y, kSpectrumX, kSpectrumY, kSpectrumW, kSpectrumH) ||
      hit(x, y, kSpectrumX, kWaterfallY, kSpectrumW, kWaterfallH)) {
    const int64_t offset =
        static_cast<int64_t>(x - (kSpectrumX + kSpectrumW / 2)) *
        g_snapshot.span_hz / kSpectrumW;
    const int64_t selected = static_cast<int64_t>(g_snapshot.frequency_hz) + offset;
    return {ActionKind::tune_hz, static_cast<int32_t>(
        std::clamp<int64_t>(selected, 24000, 30000000))};
  }
  return {};
}

Action handle_gain_drag(int32_t x, int32_t y) {
  if (!g_active || !hit(x, y, kGainX - 16, kGainY - 20, kGainW + 32, 62) ||
      g_snapshot.gain_step_count == 0 ||
      receiver_controls::item(receiver_controls::Control::rf_gain,
                              g_snapshot.controls).availability !=
          receiver_controls::Availability::enabled)
    return {};
  const int clamped = std::clamp<int32_t>(x, kGainX, kGainX + kGainW);
  const size_t index = static_cast<size_t>(clamped - kGainX) *
                       (g_snapshot.gain_step_count - 1) / kGainW;
  return {ActionKind::gain_tenth_db, g_snapshot.gain_steps_tenth_db[index]};
}

bool active() { return g_active; }
bool spectrum_active() { return g_active && !g_keypad; }
uint32_t saved_frequency() { return g_saved_frequency; }
void note_tuned(uint32_t frequency_hz) { g_saved_frequency = frequency_hz; }

bool dashboard_self_check() {
  const Snapshot saved = g_snapshot;
  const bool was_active = g_active;
  const bool had_keypad = g_keypad;
  char saved_entry[sizeof(g_entry)];
  memcpy(saved_entry, g_entry, sizeof(g_entry));
  Snapshot test{};
  test.controls.route = ReceiverRoute::hf_upconverter;
  test.controls.capabilities = {true, true, true, false};
  test.gain_steps_tenth_db[0] = 0;
  test.gain_steps_tenth_db[1] = 297;
  test.gain_steps_tenth_db[2] = 496;
  test.gain_step_count = 3;
  g_snapshot = test;
  g_active = true;
  g_keypad = false;
  const bool ok = handle_touch(60, 150).kind == ActionKind::step_down &&
                   handle_touch(760, 150).kind == ActionKind::step_up &&
                   handle_touch(900, 200).kind == ActionKind::gain_auto &&
                   handle_gain_drag(kGainX + kGainW, kGainY).value == 496;
  g_snapshot.controls.route = ReceiverRoute::direct_q;
  const bool direct_q_ok = handle_touch(900, 200).kind == ActionKind::none &&
                           handle_gain_drag(kGainX, kGainY).kind == ActionKind::none &&
                           handle_touch(900, 280).kind == ActionKind::audio_boost;
  const bool spectrum_layout_ok =
      spectrum_x_for_bin(0, 4) == kSpectrumX &&
      spectrum_x_for_bin(3, 4) == kSpectrumX + kSpectrumW - 1 &&
      kWaterfallY > kSpectrumY + kSpectrumH &&
      kWaterfallY + kWaterfallH <= kTabsY;
  const float resolution_test[] = {-80.0f, -20.0f, -75.0f, -40.0f};
  const bool peak_pool_ok =
      spectrum::peak_for_pixel(resolution_test, 0, 4, 0, 2) == -20.0f &&
      spectrum::peak_for_pixel(resolution_test, 0, 4, 1, 2) == -40.0f;
  g_snapshot.frequency_hz = 7100000;
  g_snapshot.span_hz = 480000;
  const bool touch_tune_bounds_ok =
      handle_touch(kSpectrumX + kSpectrumW / 2, kSpectrumY).kind ==
          ActionKind::tune_hz &&
      handle_touch(kSpectrumX + kSpectrumW / 2, kSpectrumY).value == 7100000 &&
      handle_touch(kSpectrumX - 1, kSpectrumY).kind == ActionKind::none &&
      handle_touch(kSpectrumX, kSpectrumY - 1).kind == ActionKind::none &&
      handle_touch(kSpectrumX + kSpectrumW, kWaterfallY).kind == ActionKind::none &&
      handle_touch(kSpectrumX, kWaterfallY + kWaterfallH).kind == ActionKind::none &&
      handle_touch(830, 400).kind == ActionKind::none;
  g_snapshot = saved;
  g_active = was_active;
  g_keypad = had_keypad;
  memcpy(g_entry, saved_entry, sizeof(g_entry));
  return ok && direct_q_ok && spectrum_layout_ok && peak_pool_ok &&
         touch_tune_bounds_ok &&
         kTabsY + 90 <= 720 && model_self_check() && receiver_controls::self_check();
}

}  // namespace orcsdr::shortwave
