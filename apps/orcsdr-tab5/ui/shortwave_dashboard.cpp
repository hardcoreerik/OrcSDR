#include "shortwave_dashboard.hpp"
#include "shortwave_dashboard_state.hpp"

#include "dashboard_audio_control.hpp"
#include "shortwave_model.hpp"
#include "spectrum_resample.hpp"
#include "text_editor.hpp"

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
struct Rect {
  int x;
  int y;
  int w;
  int h;
};

constexpr int kSpectrumX = 24;
constexpr int kSpectrumY = 276;
constexpr int kSpectrumClosedW = 1232;
constexpr int kSpectrumOpenW = 804;
constexpr int kSpectrumH = 204;
constexpr int kSpectrumLabelH = 20;
constexpr int kSpectrumPlotH = kSpectrumH - kSpectrumLabelH;
constexpr int kWaterfallY = 486;
constexpr int kWaterfallH = 102;
constexpr int kZoomY = 592;
constexpr int kTabsY = 630;
constexpr int kTabW = 256;
constexpr Rect kFrequencyCard{24, 104, 1232, 80};
constexpr Rect kFrequencyMinus{42, 112, 64, 64};
constexpr Rect kFrequencyEntry{120, 108, 1040, 72};
constexpr Rect kFrequencyPlus{1174, 112, 64, 64};
constexpr Rect kStep{24, 190, 150, 52};
constexpr Rect kFilterDown{182, 190, 52, 52};
constexpr Rect kFilterValue{242, 190, 180, 52};
constexpr Rect kFilterUp{430, 190, 52, 52};
constexpr Rect kClean{490, 190, 100, 52};
constexpr Rect kBoost{598, 190, 100, 52};
constexpr Rect kNoiseReduction{706, 190, 110, 52};
constexpr Rect kNotch{824, 190, 110, 52};
constexpr Rect kSquelch{942, 190, 160, 52};
constexpr Rect kTuner{1110, 190, 146, 52};
constexpr Rect kDrawer{840, 104, 420, 522};
constexpr Rect kDrawerClose{858, 552, 378, 52};
constexpr Rect kTunerAgc{858, 176, 184, 68};
constexpr Rect kRtlAgc{1052, 176, 184, 68};
constexpr int kGainX = 872;
constexpr int kGainY = 442;
constexpr int kGainW = 350;

Snapshot g_snapshot{};
bool g_active = false;
DashboardState g_state{};
uint32_t g_saved_frequency = 7100000;
size_t g_hunt_band = 0;
char g_entry[12]{};
char g_pending_memory_label[64]{};
char g_pending_memory_notes[160]{};
char g_pending_log_antenna[64]{};
char g_pending_log_notes[240]{};
size_t g_edit_memory = SIZE_MAX;
size_t g_edit_log = SIZE_MAX;
size_t g_memory_page = 0;
size_t g_log_page = 0;
size_t g_delete_memory = SIZE_MAX;
size_t g_delete_log = SIZE_MAX;
bool g_filter_dragging = false;
bool g_tuner_drawer_open = false;
EXT_RAM_BSS_ATTR uint16_t g_waterfall_row[kSpectrumClosedW]{};

int spectrum_width() {
  return g_tuner_drawer_open ? kSpectrumOpenW : kSpectrumClosedW;
}

bool hit(int32_t x, int32_t y, const Rect& rect) {
  return x >= rect.x && x < rect.x + rect.w &&
         y >= rect.y && y < rect.y + rect.h;
}

bool overlaps(const Rect& a, const Rect& b) {
  return a.x < b.x + b.w && a.x + a.w > b.x &&
         a.y < b.y + b.h && a.y + a.h > b.y;
}

int center_x(const Rect& rect) { return rect.x + rect.w / 2; }
int center_y(const Rect& rect) { return rect.y + rect.h / 2; }

int spectrum_x_for_bin(size_t bin, size_t visible_bins) {
  return kSpectrumX + static_cast<int>(bin * (spectrum_width() - 1) /
                                       (visible_bins > 1 ? visible_bins - 1 : 1));
}

bool hit(int32_t x, int32_t y, int bx, int by, int bw, int bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

uint32_t spectrum_tick_frequency(uint32_t center_hz, uint32_t span_hz,
                                 size_t tick, size_t tick_count) {
  if (tick_count < 2) return center_hz;
  const uint32_t half_span = span_hz / 2u;
  const uint32_t first = center_hz > half_span ? center_hz - half_span : 0u;
  return first + static_cast<uint32_t>(
      static_cast<uint64_t>(span_hz) * tick / (tick_count - 1u));
}

int filter_half_width() {
  const int width = spectrum_width();
  return std::clamp(static_cast<int>(
      static_cast<uint64_t>(g_snapshot.filter_bandwidth_hz) * width /
      (2u * (g_snapshot.span_hz ? g_snapshot.span_hz : 1u))), 3,
      width / 2 - 2);
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

void button(const Rect& rect, const char* label, bool selected = false,
            bool enabled = true) {
  button(rect.x, rect.y, rect.w, rect.h, label, selected, enabled);
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
  const int width = spectrum_width();
  M5.Display.fillRect(kSpectrumX + 1, kFrequencyCard.y + 1,
                      width - 2, kFrequencyCard.h - 2, kPanel);
  button(kFrequencyMinus, "-");
  const Rect plus{kSpectrumX + width - 82, kFrequencyPlus.y,
                  kFrequencyPlus.w, kFrequencyPlus.h};
  button(plus, "+");
  char value[40];
  snprintf(value, sizeof(value), "%lu.%03lu.%03lu MHz",
           static_cast<unsigned long>(g_snapshot.frequency_hz / 1000000u),
           static_cast<unsigned long>((g_snapshot.frequency_hz / 1000u) % 1000u),
           static_cast<unsigned long>(g_snapshot.frequency_hz % 1000u));
  text(value, kSpectrumX + width / 2, 132, TFT_WHITE, 4);
  const BroadcastBand* sw_band = band_for(g_snapshot.frequency_hz);
  const ModeGuide guide = mode_guide_for(g_snapshot.frequency_hz);
  char context[64];
  snprintf(context, sizeof(context), "%s  |  %s %s",
           sw_band ? sw_band->label : region_label(region_for(g_snapshot.frequency_hz)),
           guide.likely_mode, guide.supported_now ? "READY" : "GUIDE");
  text(context, kSpectrumX + width / 2, 166,
       guide.supported_now ? kGreen : kYellow, 2);
}

void draw_status() {
  const int width = spectrum_width();
  M5.Display.fillRect(kSpectrumX, 246, width, 26, TFT_BLACK);
  char status[96];
  snprintf(status, sizeof(status), "%s  |  %+.1f dBFS  |  %lu kHz span%s",
           g_snapshot.running ? "LIVE IQ" : "WAITING",
           static_cast<double>(g_snapshot.relative_dbfs),
           static_cast<unsigned long>(g_snapshot.span_hz / 1000u),
           g_snapshot.clipping_percent > 0.1f ? "  |  CLIP" : "");
  text(status, kSpectrumX + width / 2, 259,
       g_snapshot.clipping_percent > 0.1f ? TFT_RED
                                         : g_snapshot.running ? kGreen : TFT_ORANGE,
       2);
}

void draw_quick_controls() {
  char value[40];
  snprintf(value, sizeof(value), "STEP %lu Hz",
           static_cast<unsigned long>(g_snapshot.step_hz));
  button(kStep, value);
  button(kFilterDown, "<");
  snprintf(value, sizeof(value), "FILTER %.1f kHz",
           static_cast<double>(g_snapshot.filter_bandwidth_hz) / 1000.0);
  button(kFilterValue, value);
  button(kFilterUp, ">");
  const auto boost = receiver_controls::item(receiver_controls::Control::audio_boost,
                                              g_snapshot.controls);
  const bool clean = g_snapshot.filter_bandwidth_hz == 6000 &&
      g_snapshot.dsp.noise_reduction == audio_dsp::NoiseReduction::low &&
      g_snapshot.dsp.auto_notch &&
      g_snapshot.dsp.squelch == audio_dsp::SquelchMode::off;
  const char* nr = g_snapshot.dsp.noise_reduction == audio_dsp::NoiseReduction::off
                       ? "NR OFF"
                       : g_snapshot.dsp.noise_reduction == audio_dsp::NoiseReduction::low
                             ? "NR LOW"
                             : "NR HIGH";
  char sql[32];
  if (g_snapshot.dsp.squelch == audio_dsp::SquelchMode::off)
    snprintf(sql, sizeof(sql), "SQL OFF");
  else if (g_snapshot.dsp.squelch == audio_dsp::SquelchMode::automatic)
    snprintf(sql, sizeof(sql), "SQL AUTO");
  else
    snprintf(sql, sizeof(sql), "SQL %d dB", g_snapshot.dsp.squelch_dbfs);
  button(kClean, "CLEAN", clean);
  button(kBoost, "BOOST", boost.active);
  button(kNoiseReduction, nr,
         g_snapshot.dsp.noise_reduction != audio_dsp::NoiseReduction::off);
  button(kNotch, "NOTCH", g_snapshot.dsp.auto_notch);
  button(kSquelch, sql,
         g_snapshot.dsp.squelch != audio_dsp::SquelchMode::off &&
             g_snapshot.dsp_metrics.squelch_open);
  button(kTuner, "TUNER", g_tuner_drawer_open);
}

void draw_zoom_controls() {
  char span[32];
  snprintf(span, sizeof(span), "SPAN %lu kHz",
           static_cast<unsigned long>(g_snapshot.span_hz / 1000u));
  const int width = spectrum_width();
  button(kSpectrumX, kZoomY, 160, 34, "ZOOM IN");
  text(span, kSpectrumX + width / 2, kZoomY + 17, kCyan, 2);
  button(kSpectrumX + width - 160, kZoomY, 160, 34, "ZOOM OUT");
}

void draw_spectrum_scale() {
  constexpr size_t kTicks = 5;
  const int width = spectrum_width();
  M5.Display.fillRect(kSpectrumX + 1, kSpectrumY + kSpectrumPlotH,
                      width - 2, kSpectrumLabelH - 1, TFT_BLACK);
  char value[20];
  for (size_t tick = 0; tick < kTicks; ++tick) {
    const int x = kSpectrumX + static_cast<int>(
        tick * (width - 1) / (kTicks - 1));
    const uint32_t frequency = spectrum_tick_frequency(
        g_snapshot.frequency_hz, g_snapshot.span_hz, tick, kTicks);
    snprintf(value, sizeof(value), "%.3f", static_cast<double>(frequency) / 1.0e6);
    const textdatum_t datum = tick == 0 ? top_left
                              : tick + 1 == kTicks ? top_right
                                                   : middle_center;
    text(value, x, kSpectrumY + kSpectrumPlotH + 10,
         tick == kTicks / 2 ? kCyan : TFT_LIGHTGREY, 1, datum);
  }
}

void draw_tuner_drawer() {
  if (!g_tuner_drawer_open) return;
  const auto tuner = receiver_controls::item(receiver_controls::Control::tuner_agc,
                                              g_snapshot.controls);
  const auto rtl = receiver_controls::item(receiver_controls::Control::rtl_agc,
                                            g_snapshot.controls);
  const auto gain = receiver_controls::item(receiver_controls::Control::rf_gain,
                                             g_snapshot.controls);
  card(kDrawer.x, kDrawer.y, kDrawer.w, kDrawer.h);
  text(route_name(g_snapshot.controls.route), 1050, 126, kGreen, 2);
  text(g_snapshot.device[0] ? g_snapshot.device : "NO RTL-SDR", 1050, 151,
       g_snapshot.driver_ready ? TFT_WHITE : TFT_ORANGE, 1);
  button(kTunerAgc, "TUNER AGC", tuner.active,
         tuner.availability == receiver_controls::Availability::enabled);
  button(kRtlAgc, "RTL AGC", rtl.active,
         rtl.availability == receiver_controls::Availability::enabled);
  M5.Display.fillRect(858, 258, 378, 282, kPanel);
  if (tuner.availability != receiver_controls::Availability::enabled)
    text(tuner.explanation, 1047, 274, kMuted, 1);
  if (rtl.availability != receiver_controls::Availability::enabled)
    text(rtl.explanation, 1047, 296, kMuted, 1);
  text("RF GAIN", 858, 318,
       gain.availability == receiver_controls::Availability::enabled ? kCyan : kMuted,
       2, top_left);
  if (gain.availability != receiver_controls::Availability::enabled) {
    text(gain.explanation, 1047, 374, kMuted, 2);
    text("This receiver route has no tuner gain stage", 1047, 410,
         TFT_LIGHTGREY, 1);
    text("RTL AGC remains available when supported", 1047, 432,
         TFT_LIGHTGREY, 1);
    button(kDrawerClose, "CLOSE");
    return;
  }
  char gain_value[32];
  snprintf(gain_value, sizeof(gain_value), g_snapshot.controls.tuner_agc
                                                   ? "AUTO %.1f dB"
                                                   : "MANUAL %.1f dB",
           static_cast<double>(g_snapshot.controls.gain_tenth_db) / 10.0);
  text(gain_value, 1047, 370, g_snapshot.controls.tuner_agc ? kGreen : TFT_WHITE, 2);
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
  text("Tap TUNER AGC for auto; drag for manual", 1047, 486, TFT_LIGHTGREY, 1);
  button(kDrawerClose, "CLOSE");
}

void draw_live_static() {
  const int width = spectrum_width();
  card(kFrequencyCard.x, kFrequencyCard.y, width, kFrequencyCard.h);
  M5.Display.drawRect(kSpectrumX, kSpectrumY, width, kSpectrumH, kGrid);
  for (int i = 1; i < 8; ++i)
    M5.Display.drawFastVLine(kSpectrumX + i * width / 8, kSpectrumY,
                            kSpectrumPlotH, kGrid);
  for (int i = 1; i < 4; ++i)
    M5.Display.drawFastHLine(kSpectrumX, kSpectrumY + i * kSpectrumPlotH / 4,
                            width, kGrid);
  draw_spectrum_scale();
  M5.Display.drawRect(kSpectrumX, kWaterfallY, width, kWaterfallH, kCyan);
  M5.Display.setScrollRect(kSpectrumX + 1, kWaterfallY + 1, width - 2,
                           kWaterfallH - 2, TFT_BLACK);
  draw_zoom_controls();
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

  if (g_state.tab() == Tab::live && g_state.modal() == Modal::none)
    draw_live_static();

  constexpr const char* tabs[] = {"LIVE", "ON AIR", "HUNT", "MEMORY", "LOGBOOK"};
  for (int i = 0; i < 5; ++i) {
    button(i * kTabW + 4, kTabsY + 4, kTabW - 8, 82, tabs[i],
           static_cast<int>(g_state.tab()) == i);
  }
}

void draw_page_title(const char* title, const char* subtitle) {
  M5.Display.clearScrollRect();
  M5.Display.fillRect(0, 93, 1280, kTabsY - 93, TFT_BLACK);
  text(title, 36, 126, TFT_WHITE, 4, middle_left);
  text(subtitle, 38, 166, kMuted, 2, middle_left);
}

void draw_storage_status() {
  const bool ready = g_snapshot.storage_status == StorageStatus::ready;
  text(ready ? "SD LIBRARY READY" : "SD LIBRARY UNAVAILABLE", 1220, 130,
       ready ? kGreen : TFT_ORANGE, 2, middle_right);
}

void draw_on_air() {
  draw_page_title("ON AIR", "Local schedules are suggestions, never decoded identity");
  draw_storage_status();
  if (!g_snapshot.utc_valid) {
    card(36, 230, 1208, 250);
    text("SET UTC TO USE ON AIR", 640, 315, TFT_ORANGE, 3);
    text("Schedules stay hidden until the device clock is valid.", 640, 370,
         TFT_WHITE, 2);
    return;
  }
  size_t row = 0;
  for (size_t i = 0; i < station_count() && row < 6; ++i) {
    const StationCard* station = station_at(i);
    if (!station || !schedule_matches(*station, g_snapshot.frequency_hz,
                                      g_snapshot.utc_minute,
                                      g_snapshot.utc_weekday))
      continue;
    char label[160];
    snprintf(label, sizeof(label), "%.3f MHz  %s  |  %s",
             static_cast<double>(station->frequency_hz) / 1000000.0,
             station->station, station->program);
    button(36, 220 + static_cast<int>(row) * 58, 1208, 50, label, row == 0);
    ++row;
  }
  if (!row) {
    card(36, 230, 1208, 250);
    text("NO MATCHING LOCAL SCHEDULE", 640, 315, TFT_ORANGE, 3);
    text("Tune near a listed station frequency or use HUNT to explore.", 640, 370,
         TFT_WHITE, 2);
  }
}

void draw_hunt() {
  draw_page_title("HUNT", "Scan one broadcast band and keep the strongest candidates");
  const BroadcastBand* selected = band(g_hunt_band);
  char selection[64];
  snprintf(selection, sizeof(selection), "BAND  %s  %.3f-%.3f MHz",
           selected ? selected->label : "--",
           selected ? static_cast<double>(selected->min_hz) / 1000000.0 : 0.0,
           selected ? static_cast<double>(selected->max_hz) / 1000000.0 : 0.0);
  button(36, 190, 80, 58, "<");
  button(126, 190, 540, 58, selection, true);
  button(676, 190, 80, 58, ">");
  button(790, 190, 220, 58, g_snapshot.hunt_active ? "SCANNING" : "START",
         g_snapshot.hunt_active);
  button(1020, 190, 224, 58, "CANCEL", false, g_snapshot.hunt_active);
  char progress[48];
  snprintf(progress, sizeof(progress), "%u / %u", g_snapshot.hunt_step,
           g_snapshot.hunt_total);
  text(progress, 1170, 165, g_snapshot.hunt_active ? kGreen : kMuted, 2);
  if (!g_snapshot.hunt_candidate_count) {
    card(36, 280, 1208, 250);
    text(g_snapshot.hunt_active ? "LISTENING FOR PEAKS..." : "NO HUNT RESULTS YET",
         640, 370, g_snapshot.hunt_active ? kGreen : TFT_WHITE, 3);
    text("Select a band, start the scan, then tap a result to listen.",
         640, 420, kMuted, 2);
    return;
  }
  for (size_t i = 0; i < std::min<size_t>(g_snapshot.hunt_candidate_count, 6); ++i) {
    const Candidate& candidate = g_snapshot.hunt_candidates[i];
    char row[96];
    snprintf(row, sizeof(row), "%u.  %.3f MHz     %+.1f dBFS",
             static_cast<unsigned>(i + 1),
             static_cast<double>(candidate.frequency_hz) / 1000000.0,
             static_cast<double>(candidate.level_dbfs));
    button(36, 270 + static_cast<int>(i) * 52, 1208, 44, row);
  }
}

void draw_memory() {
  draw_page_title("MEMORY", "Saved frequencies live on the SD card");
  draw_storage_status();
  button(980, 170, 264, 58, "SAVE CURRENT", true,
         g_snapshot.storage_status == StorageStatus::ready);
  const size_t count = g_snapshot.memories ? g_snapshot.memories->size() : 0;
  const size_t pages = std::max<size_t>(1, (count + 6) / 7);
  g_memory_page = std::min(g_memory_page, pages - 1);
  button(36, 170, 70, 58, "<", false, g_memory_page > 0);
  char page[48];
  snprintf(page, sizeof(page), "PAGE %u / %u", static_cast<unsigned>(g_memory_page + 1),
           static_cast<unsigned>(pages));
  text(page, 190, 199, kMuted, 2);
  button(300, 170, 70, 58, ">", false, g_memory_page + 1 < pages);
  if (!count) {
    card(36, 260, 1208, 270);
    text("NO SHORTWAVE MEMORIES", 640, 355, TFT_WHITE, 3);
    text("Tune a signal in LIVE, then save the current frequency here.",
         640, 410, kMuted, 2);
    return;
  }
  const size_t first = g_memory_page * 7;
  for (size_t row = 0; row < std::min<size_t>(count - first, 7); ++row) {
    const size_t i = first + row;
    const Memory* memory = g_snapshot.memories->at(i);
    if (!memory) continue;
    char label[128];
    snprintf(label, sizeof(label), "%u.  %.3f MHz  %-3s  %s",
             static_cast<unsigned>(i + 1),
             static_cast<double>(memory->frequency_hz) / 1000000.0,
             memory->mode, memory->station[0] ? memory->station : "Unlabeled signal");
    const int y = 250 + static_cast<int>(row) * 50;
    button(36, y, 780, 42, label);
    button(826, y, 100, 42, memory->favorite ? "STAR" : "FAV", memory->favorite);
    button(936, y, 130, 42, "EDIT");
    button(1076, y, 168, 42, g_delete_memory == i ? "CONFIRM" : "DELETE");
  }
}

void draw_logbook() {
  draw_page_title("LOGBOOK", "Reception history and community-friendly exports");
  draw_storage_status();
  button(720, 170, 250, 58, "LOG CURRENT", true,
         g_snapshot.storage_status == StorageStatus::ready);
  button(984, 170, 260, 58, "EXPORT CSV + ADIF", false,
         g_snapshot.storage_status == StorageStatus::ready);
  const size_t count = g_snapshot.logs ? g_snapshot.logs->size() : 0;
  const size_t pages = std::max<size_t>(1, (count + 6) / 7);
  g_log_page = std::min(g_log_page, pages - 1);
  button(36, 170, 70, 58, "<", false, g_log_page > 0);
  char page[48];
  snprintf(page, sizeof(page), "PAGE %u / %u", static_cast<unsigned>(g_log_page + 1),
           static_cast<unsigned>(pages));
  text(page, 190, 199, kMuted, 2);
  button(300, 170, 70, 58, ">", false, g_log_page + 1 < pages);
  if (!count) {
    card(36, 260, 1208, 270);
    text("NO RECEPTION LOGS", 640, 355, TFT_WHITE, 3);
    text("Log what you actually heard; station identity may remain unknown.",
         640, 410, kMuted, 2);
    return;
  }
  const size_t first = g_log_page * 7;
  for (size_t row_index = 0; row_index < std::min<size_t>(count - first, 7);
       ++row_index) {
    const LogEntry* entry = g_snapshot.logs->at(first + row_index);
    if (!entry) continue;
    char row[144];
    snprintf(row, sizeof(row), "%.3f MHz  %-3s  %+.1f dBFS  %s",
             static_cast<double>(entry->frequency_hz) / 1000000.0,
             entry->mode, static_cast<double>(entry->signal_dbfs),
             entry->station[0] ? entry->station : "Unidentified reception");
    const int y = 250 + static_cast<int>(row_index) * 50;
    button(36, y, 880, 42, row);
    button(926, y, 140, 42, "EDIT");
    const size_t index = first + row_index;
    button(1076, y, 168, 42, g_delete_log == index ? "CONFIRM" : "DELETE");
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
  g_filter_dragging = false;
  g_tuner_drawer_open = false;
  g_edit_memory = SIZE_MAX;
  g_edit_log = SIZE_MAX;
  g_memory_page = 0;
  g_log_page = 0;
  g_delete_memory = SIZE_MAX;
  g_delete_log = SIZE_MAX;
  g_active = true;
  g_state.close_modal();
  g_state.select_tab(Tab::live);
  if (const BroadcastBand* current = band_for(snapshot.frequency_hz)) {
    for (size_t i = 0; i < band_count(); ++i)
      if (band(i) == current) g_hunt_band = i;
  }
  g_entry[0] = '\0';
  draw();
}

void leave() {
  M5.Display.clearScrollRect();
  g_filter_dragging = false;
  g_tuner_drawer_open = false;
  g_delete_memory = SIZE_MAX;
  g_delete_log = SIZE_MAX;
  g_active = false;
}

void draw() {
  if (!g_active) return;
  draw_static();
  if (g_state.modal() == Modal::frequency) {
    draw_keypad();
    return;
  }
  if (g_state.modal() != Modal::none) {
    text_editor::draw();
    return;
  }
  switch (g_state.tab()) {
    case Tab::live:
      draw_frequency();
      draw_status();
      draw_quick_controls();
      draw_tuner_drawer();
      break;
    case Tab::on_air: draw_on_air(); break;
    case Tab::hunt: draw_hunt(); break;
    case Tab::memory: draw_memory(); break;
    case Tab::logbook: draw_logbook(); break;
  }
}

void update(const Snapshot& snapshot) {
  if (!g_active) return;
  const bool frequency_changed = snapshot.frequency_hz != g_snapshot.frequency_hz;
  const bool span_changed = snapshot.span_hz != g_snapshot.span_hz;
  const bool page_changed =
      frequency_changed || snapshot.utc_valid != g_snapshot.utc_valid ||
      snapshot.utc_minute != g_snapshot.utc_minute ||
      snapshot.utc_weekday != g_snapshot.utc_weekday ||
      snapshot.storage_status != g_snapshot.storage_status ||
      snapshot.memories != g_snapshot.memories || snapshot.logs != g_snapshot.logs ||
      snapshot.memory_count != g_snapshot.memory_count ||
      snapshot.log_count != g_snapshot.log_count ||
      snapshot.hunt_active != g_snapshot.hunt_active ||
      snapshot.hunt_step != g_snapshot.hunt_step ||
      snapshot.hunt_total != g_snapshot.hunt_total ||
      snapshot.hunt_candidate_count != g_snapshot.hunt_candidate_count;
  const bool controls_changed =
      snapshot.filter_bandwidth_hz != g_snapshot.filter_bandwidth_hz ||
      snapshot.controls.route != g_snapshot.controls.route ||
      snapshot.controls.capabilities.rf_gain != g_snapshot.controls.capabilities.rf_gain ||
      snapshot.controls.capabilities.tuner_agc != g_snapshot.controls.capabilities.tuner_agc ||
      snapshot.controls.capabilities.rtl_agc != g_snapshot.controls.capabilities.rtl_agc ||
      snapshot.controls.tuner_agc != g_snapshot.controls.tuner_agc ||
      snapshot.controls.rtl_agc != g_snapshot.controls.rtl_agc ||
      snapshot.controls.audio_boost != g_snapshot.controls.audio_boost ||
      snapshot.controls.volume != g_snapshot.controls.volume ||
      snapshot.controls.gain_tenth_db != g_snapshot.controls.gain_tenth_db ||
      snapshot.gain_step_count != g_snapshot.gain_step_count ||
      snapshot.dsp.noise_reduction != g_snapshot.dsp.noise_reduction ||
      snapshot.dsp.auto_notch != g_snapshot.dsp.auto_notch ||
      snapshot.dsp.squelch != g_snapshot.dsp.squelch ||
      snapshot.dsp.squelch_dbfs != g_snapshot.dsp.squelch_dbfs ||
      snapshot.dsp_metrics.squelch_open != g_snapshot.dsp_metrics.squelch_open ||
      snapshot.dsp_metrics.notch_active != g_snapshot.dsp_metrics.notch_active ||
      snapshot.dsp_metrics.notch_hz != g_snapshot.dsp_metrics.notch_hz;
  const bool quick_changed = controls_changed ||
      snapshot.step_hz != g_snapshot.step_hz;
  g_snapshot = snapshot;
  g_saved_frequency = snapshot.frequency_hz;
  if (!g_state.background_redraw_allowed()) return;
  if (g_state.tab() != Tab::live) {
    if (page_changed) {
      switch (g_state.tab()) {
        case Tab::on_air: draw_on_air(); break;
        case Tab::hunt: draw_hunt(); break;
        case Tab::memory: draw_memory(); break;
        case Tab::logbook: draw_logbook(); break;
        case Tab::live: break;
      }
    }
    return;
  }
  if (frequency_changed) draw_frequency();
  draw_status();
  if (quick_changed) draw_quick_controls();
  if (span_changed) draw_zoom_controls();
  if (frequency_changed || span_changed) draw_spectrum_scale();
  if (controls_changed || (quick_changed && g_tuner_drawer_open))
    draw_tuner_drawer();
}

void draw_spectrum(const float* levels, size_t first_bin, size_t visible_bins,
                   float floor) {
  if (!spectrum_active() || !levels || visible_bins < 2) return;
  const int width = spectrum_width();
  M5.Display.startWrite();
  M5.Display.fillRect(kSpectrumX + 1, kSpectrumY + 1, width - 2,
                      kSpectrumPlotH - 1, TFT_BLACK);
  int last_x = kSpectrumX;
  int last_y = kSpectrumY + kSpectrumPlotH - 2;
  for (size_t i = 0; i < static_cast<size_t>(width); ++i) {
    const float level = spectrum::peak_for_pixel(
        levels, first_bin, visible_bins, i, width);
    const float normalized = std::clamp((level - floor) / 48.0f,
                                        0.0f, 1.0f);
    const int x = kSpectrumX + static_cast<int>(i);
    const int y = kSpectrumY + kSpectrumPlotH - 2 -
                  static_cast<int>(normalized * (kSpectrumPlotH - 4));
    if (i) M5.Display.drawLine(last_x, last_y, x, y, kGreen);
    last_x = x;
    last_y = y;
    g_waterfall_row[i] = waterfall_color(normalized);
  }
  const int center = kSpectrumX + width / 2;
  const int half_filter = filter_half_width();
  M5.Display.drawFastVLine(center, kSpectrumY, kSpectrumPlotH, kCyan);
  M5.Display.drawFastVLine(center - half_filter, kSpectrumY, kSpectrumPlotH, kYellow);
  M5.Display.drawFastVLine(center + half_filter, kSpectrumY, kSpectrumPlotH, kYellow);
  M5.Display.scroll(0, -1);
  M5.Display.pushImage(kSpectrumX, kWaterfallY + kWaterfallH - 2, width, 1,
                       g_waterfall_row);
  M5.Display.endWrite();
}

Action handle_touch(int32_t x, int32_t y) {
  if (!g_active) return {};
  if (g_state.modal() != Modal::none && g_state.modal() != Modal::frequency) {
    const Modal modal = g_state.modal();
    const auto result = text_editor::handle_touch(x, y);
    if (result == text_editor::Result::cancelled) {
      g_state.close_modal();
      draw();
      return {};
    }
    if (result != text_editor::Result::accepted) return {};
    if (modal == Modal::memory_label) {
      snprintf(g_pending_memory_label, sizeof(g_pending_memory_label), "%s",
               text_editor::value());
      g_state.open(Modal::memory_notes);
      text_editor::begin("MEMORY NOTES", g_pending_memory_notes,
                         sizeof(g_pending_memory_notes) - 1, false,
                         g_edit_memory == SIZE_MAX ? "SAVE" : "UPDATE");
      text_editor::draw();
      return {};
    }
    if (modal == Modal::memory_notes) {
      snprintf(g_pending_memory_notes, sizeof(g_pending_memory_notes), "%s",
               text_editor::value());
      g_state.close_modal();
      draw();
      return {g_edit_memory == SIZE_MAX ? ActionKind::save_memory
                                        : ActionKind::update_memory,
              g_edit_memory == SIZE_MAX ? 0 : static_cast<int32_t>(g_edit_memory)};
    }
    if (modal == Modal::log_antenna) {
      snprintf(g_pending_log_antenna, sizeof(g_pending_log_antenna), "%s",
               text_editor::value());
      g_state.open(Modal::log_notes);
      text_editor::begin("RECEPTION NOTES", g_pending_log_notes,
                         sizeof(g_pending_log_notes) - 1, false,
                         g_edit_log == SIZE_MAX ? "LOG" : "UPDATE");
      text_editor::draw();
      return {};
    }
    if (modal == Modal::log_notes) {
      snprintf(g_pending_log_notes, sizeof(g_pending_log_notes), "%s",
               text_editor::value());
      g_state.close_modal();
      draw();
      return {g_edit_log == SIZE_MAX ? ActionKind::save_log
                                     : ActionKind::update_log,
              g_edit_log == SIZE_MAX ? 0 : static_cast<int32_t>(g_edit_log)};
    }
    return {};
  }
  if (g_state.modal() == Modal::frequency) {
    if (hit(x, y, 380, 525, 250, 55)) {
      g_state.close_modal();
      g_entry[0] = '\0';
      draw();
      return {};
    }
    if (hit(x, y, 650, 525, 250, 55)) {
      char* end = nullptr;
      const double mhz = strtod(g_entry, &end);
      if (end != g_entry && *end == '\0' && mhz >= 0.024 && mhz <= 30.0) {
        g_state.close_modal();
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
  if (y >= kTabsY) {
    const int index = std::clamp<int32_t>(x / kTabW, 0, 4);
    const bool cancel_hunt = g_state.select_tab(static_cast<Tab>(index));
    draw();
    return cancel_hunt ? Action{ActionKind::hunt_cancel} : Action{};
  }

  if (g_state.tab() == Tab::on_air) {
    if (!g_snapshot.utc_valid) return {};
    size_t row = 0;
    for (size_t i = 0; i < station_count() && row < 6; ++i) {
      const StationCard* station = station_at(i);
      if (!station || !schedule_matches(*station, g_snapshot.frequency_hz,
                                        g_snapshot.utc_minute,
                                        g_snapshot.utc_weekday))
        continue;
      if (hit(x, y, 36, 220 + static_cast<int>(row) * 58, 1208, 50))
        return {ActionKind::tune_hz, static_cast<int32_t>(station->frequency_hz)};
      ++row;
    }
    return {};
  }

  if (g_state.tab() == Tab::hunt) {
    if (hit(x, y, 36, 190, 80, 58)) {
      g_hunt_band = (g_hunt_band + band_count() - 1) % band_count();
      draw();
      return {};
    }
    if (hit(x, y, 676, 190, 80, 58)) {
      g_hunt_band = (g_hunt_band + 1) % band_count();
      draw();
      return {};
    }
    if (hit(x, y, 790, 190, 220, 58) && !g_snapshot.hunt_active)
      return {ActionKind::hunt_start, static_cast<int32_t>(g_hunt_band)};
    if (hit(x, y, 1020, 190, 224, 58) && g_snapshot.hunt_active)
      return {ActionKind::hunt_cancel};
    for (size_t i = 0; !g_snapshot.hunt_active &&
                       i < std::min<size_t>(g_snapshot.hunt_candidate_count, 6); ++i)
      if (hit(x, y, 36, 270 + static_cast<int>(i) * 52, 1208, 44))
        return {ActionKind::tune_hz,
                static_cast<int32_t>(g_snapshot.hunt_candidates[i].frequency_hz)};
    return {};
  }

  if (g_state.tab() == Tab::memory) {
    const size_t count = g_snapshot.memories ? g_snapshot.memories->size() : 0;
    const size_t pages = std::max<size_t>(1, (count + 6) / 7);
    g_memory_page = std::min(g_memory_page, pages - 1);
    if (hit(x, y, 36, 170, 70, 58) && g_memory_page > 0) {
      --g_memory_page; g_delete_memory = SIZE_MAX; draw(); return {};
    }
    if (hit(x, y, 300, 170, 70, 58) && g_memory_page + 1 < pages) {
      ++g_memory_page; g_delete_memory = SIZE_MAX; draw(); return {};
    }
    if (g_snapshot.storage_status == StorageStatus::ready &&
        hit(x, y, 980, 170, 264, 58)) {
      g_edit_memory = SIZE_MAX;
      g_pending_memory_label[0] = '\0';
      g_pending_memory_notes[0] = '\0';
      g_state.open(Modal::memory_label);
      text_editor::begin("MEMORY LABEL", "", sizeof(g_pending_memory_label) - 1,
                         false, "SAVE");
      text_editor::draw();
      return {};
    }
    const size_t first = g_memory_page * 7;
    for (size_t row = 0; row < std::min<size_t>(count - first, 7); ++row) {
      const size_t i = first + row;
      const Memory* memory = g_snapshot.memories->at(i);
      const int row_y = 250 + static_cast<int>(row) * 50;
      if (!memory) continue;
      if (hit(x, y, 36, row_y, 780, 42))
        return {ActionKind::tune_hz, static_cast<int32_t>(memory->frequency_hz)};
      if (hit(x, y, 826, row_y, 100, 42))
        return {ActionKind::favorite_memory, static_cast<int32_t>(i)};
      if (hit(x, y, 936, row_y, 130, 42)) {
        g_edit_memory = i;
        snprintf(g_pending_memory_label, sizeof(g_pending_memory_label), "%s",
                 memory->station);
        snprintf(g_pending_memory_notes, sizeof(g_pending_memory_notes), "%s",
                 memory->notes);
        g_state.open(Modal::memory_label);
        text_editor::begin("MEMORY LABEL", g_pending_memory_label,
                           sizeof(g_pending_memory_label) - 1, false, "NEXT");
        text_editor::draw();
        return {};
      }
      if (hit(x, y, 1076, row_y, 168, 42)) {
        if (g_delete_memory == i) {
          g_delete_memory = SIZE_MAX;
          return {ActionKind::delete_memory, static_cast<int32_t>(i)};
        }
        g_delete_memory = i;
        draw();
        return {};
      }
    }
    g_delete_memory = SIZE_MAX;
    return {};
  }

  if (g_state.tab() == Tab::logbook) {
    const size_t count = g_snapshot.logs ? g_snapshot.logs->size() : 0;
    const size_t pages = std::max<size_t>(1, (count + 6) / 7);
    g_log_page = std::min(g_log_page, pages - 1);
    if (hit(x, y, 36, 170, 70, 58) && g_log_page > 0) {
      --g_log_page; g_delete_log = SIZE_MAX; draw(); return {};
    }
    if (hit(x, y, 300, 170, 70, 58) && g_log_page + 1 < pages) {
      ++g_log_page; g_delete_log = SIZE_MAX; draw(); return {};
    }
    if (g_snapshot.storage_status == StorageStatus::ready &&
        hit(x, y, 720, 170, 250, 58)) {
      g_edit_log = SIZE_MAX;
      g_pending_log_antenna[0] = '\0';
      g_pending_log_notes[0] = '\0';
      g_state.open(Modal::log_antenna);
      text_editor::begin("ANTENNA USED", "", sizeof(g_pending_log_antenna) - 1,
                         false, "NEXT");
      text_editor::draw();
      return {};
    }
    if (g_snapshot.storage_status == StorageStatus::ready &&
        hit(x, y, 984, 170, 260, 58))
      return {ActionKind::export_log};
    const size_t first = g_log_page * 7;
    for (size_t row = 0; row < std::min<size_t>(count - first, 7); ++row) {
      const size_t i = first + row;
      const LogEntry* entry = g_snapshot.logs->at(i);
      const int row_y = 250 + static_cast<int>(row) * 50;
      if (!entry) continue;
      if (hit(x, y, 926, row_y, 140, 42)) {
        g_edit_log = i;
        snprintf(g_pending_log_antenna, sizeof(g_pending_log_antenna), "%s",
                 entry->antenna);
        snprintf(g_pending_log_notes, sizeof(g_pending_log_notes), "%s",
                 entry->notes);
        g_state.open(Modal::log_antenna);
        text_editor::begin("ANTENNA USED", g_pending_log_antenna,
                           sizeof(g_pending_log_antenna) - 1, false, "NEXT");
        text_editor::draw();
        return {};
      }
      if (hit(x, y, 1076, row_y, 168, 42)) {
        if (g_delete_log == i) {
          g_delete_log = SIZE_MAX;
          return {ActionKind::delete_log, static_cast<int32_t>(i)};
        }
        g_delete_log = i;
        draw();
        return {};
      }
    }
    g_delete_log = SIZE_MAX;
    return {};
  }

  if (g_tuner_drawer_open) {
    if (hit(x, y, kDrawerClose)) {
      g_tuner_drawer_open = false;
      draw();
      return {};
    }
    if (hit(x, y, kTunerAgc) &&
        receiver_controls::action(receiver_controls::Control::tuner_agc,
                                  g_snapshot.controls).kind !=
            receiver_controls::ActionKind::none)
      return {ActionKind::gain_auto};
    if (hit(x, y, kRtlAgc) &&
        receiver_controls::action(receiver_controls::Control::rtl_agc,
                                  g_snapshot.controls).kind !=
            receiver_controls::ActionKind::none)
      return {ActionKind::rtl_agc, !g_snapshot.controls.rtl_agc};
    if (hit(x, y, kDrawer)) return {};
  }

  const int width = spectrum_width();
  const Rect frequency_plus{kSpectrumX + width - 82, kFrequencyPlus.y,
                            kFrequencyPlus.w, kFrequencyPlus.h};
  const Rect frequency_entry{kFrequencyEntry.x, kFrequencyEntry.y,
                             width - 192, kFrequencyEntry.h};
  if (hit(x, y, kFrequencyMinus)) return {ActionKind::step_down};
  if (hit(x, y, frequency_plus)) return {ActionKind::step_up};
  if (hit(x, y, frequency_entry)) {
    g_state.open(Modal::frequency);
    g_entry[0] = '\0';
    draw();
    return {};
  }
  if (hit(x, y, kStep)) return {ActionKind::step_cycle};
  if (hit(x, y, kFilterDown)) return {ActionKind::filter_down};
  if (hit(x, y, kFilterValue)) return {ActionKind::filter_cycle};
  if (hit(x, y, kFilterUp)) return {ActionKind::filter_up};
  if (hit(x, y, kClean)) return {ActionKind::clean_audio};
  if (hit(x, y, kBoost))
    return {ActionKind::audio_boost, !g_snapshot.controls.audio_boost};
  if (hit(x, y, kNoiseReduction))
    return {ActionKind::noise_reduction_cycle};
  if (hit(x, y, kNotch))
    return {ActionKind::auto_notch_toggle};
  if (hit(x, y, kSquelch)) return {ActionKind::squelch_cycle};
  if (hit(x, y, kTuner)) {
    g_tuner_drawer_open = true;
    draw();
    return {};
  }
  if (hit(x, y, kSpectrumX, kZoomY, 160, 34))
    return {ActionKind::span_down};
  if (hit(x, y, kSpectrumX + width - 160, kZoomY, 160, 34))
    return {ActionKind::span_up};
  if (hit(x, y, kSpectrumX, kSpectrumY, width, kSpectrumPlotH) ||
      hit(x, y, kSpectrumX, kWaterfallY, width, kWaterfallH)) {
    const int64_t offset =
        static_cast<int64_t>(x - (kSpectrumX + width / 2)) *
        g_snapshot.span_hz / width;
    const int64_t selected = static_cast<int64_t>(g_snapshot.frequency_hz) + offset;
    return {ActionKind::tune_hz, static_cast<int32_t>(
        std::clamp<int64_t>(selected, 24000, 30000000))};
  }
  return {};
}

Action handle_gain_drag(int32_t x, int32_t y) {
  if (!g_active || !g_tuner_drawer_open || !g_state.spectrum_allowed() ||
      !hit(x, y, kGainX - 16, kGainY - 20, kGainW + 32, 62) ||
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

Action handle_filter_drag(int32_t x, int32_t y, bool pressed) {
  if (!pressed) {
    g_filter_dragging = false;
    return {};
  }
  if (!g_active || !g_state.spectrum_allowed()) return {};
  const int width = spectrum_width();
  const int center = kSpectrumX + width / 2;
  if (!g_filter_dragging) {
    if (!hit(x, y, kSpectrumX, kSpectrumY, width, kSpectrumPlotH)) return {};
    const int half_filter = filter_half_width();
    if (std::min(std::abs(x - (center - half_filter)),
                 std::abs(x - (center + half_filter))) > 18)
      return {};
    g_filter_dragging = true;
  }
  uint32_t bandwidth = static_cast<uint32_t>(
      2ull * static_cast<uint64_t>(std::abs(x - center)) *
      (g_snapshot.span_hz ? g_snapshot.span_hz : 1u) / width);
  bandwidth = std::clamp<uint32_t>((bandwidth / 1000u) * 1000u, 3000u, 30000u);
  return {ActionKind::filter_bandwidth_hz, static_cast<int32_t>(bandwidth)};
}

bool spectrum_contains(int32_t x, int32_t y) {
  const int width = spectrum_width();
  return g_active && g_state.spectrum_allowed() &&
         (hit(x, y, kSpectrumX, kSpectrumY, width, kSpectrumPlotH) ||
          hit(x, y, kSpectrumX, kWaterfallY, width, kWaterfallH));
}

bool active() { return g_active; }
bool spectrum_active() { return g_active && g_state.spectrum_allowed(); }
uint32_t saved_frequency() { return g_saved_frequency; }
void note_tuned(uint32_t frequency_hz) { g_saved_frequency = frequency_hz; }
const char* pending_memory_label() { return g_pending_memory_label; }
const char* pending_memory_notes() { return g_pending_memory_notes; }
const char* pending_log_antenna() { return g_pending_log_antenna; }
const char* pending_log_notes() { return g_pending_log_notes; }

bool dashboard_self_check() {
  const Snapshot saved = g_snapshot;
  const bool was_active = g_active;
  const bool was_filter_dragging = g_filter_dragging;
  const bool was_drawer_open = g_tuner_drawer_open;
  const Modal saved_modal = g_state.modal();
  const Tab saved_tab = g_state.tab();
  const size_t saved_memory_page = g_memory_page;
  char saved_entry[sizeof(g_entry)];
  memcpy(saved_entry, g_entry, sizeof(g_entry));
  Snapshot test{};
  test.controls.route = ReceiverRoute::hf_upconverter;
  test.controls.capabilities = {true, true, true, false};
  test.gain_steps_tenth_db[0] = 0;
  test.gain_steps_tenth_db[1] = 297;
  test.gain_steps_tenth_db[2] = 496;
  test.gain_step_count = 3;
  test.span_hz = 480000;
  test.filter_bandwidth_hz = 9000;
  test.utc_valid = true;
  test.utc_minute = 600;
  test.utc_weekday = 1;
  g_snapshot = test;
  g_active = true;
  g_tuner_drawer_open = false;
  g_state.close_modal();
  g_state.select_tab(Tab::live);
  const bool controls_ok =
      handle_touch(center_x(kFrequencyMinus), center_y(kFrequencyMinus)).kind ==
          ActionKind::step_down &&
      handle_touch(center_x(kFrequencyPlus), center_y(kFrequencyPlus)).kind ==
          ActionKind::step_up &&
      handle_touch(center_x(kStep), center_y(kStep)).kind == ActionKind::step_cycle &&
      handle_touch(center_x(kFilterDown), center_y(kFilterDown)).kind ==
          ActionKind::filter_down &&
      handle_touch(center_x(kFilterValue), center_y(kFilterValue)).kind ==
          ActionKind::filter_cycle &&
      handle_touch(center_x(kFilterUp), center_y(kFilterUp)).kind ==
          ActionKind::filter_up &&
      handle_touch(center_x(kClean), center_y(kClean)).kind ==
          ActionKind::clean_audio &&
      handle_touch(center_x(kBoost), center_y(kBoost)).kind ==
          ActionKind::audio_boost &&
      handle_touch(center_x(kNoiseReduction), center_y(kNoiseReduction)).kind ==
          ActionKind::noise_reduction_cycle &&
      handle_touch(center_x(kNotch), center_y(kNotch)).kind ==
          ActionKind::auto_notch_toggle &&
      handle_touch(center_x(kSquelch), center_y(kSquelch)).kind ==
          ActionKind::squelch_cycle &&
      handle_touch(kSpectrumX + 40, kZoomY + 10).kind ==
          ActionKind::span_down &&
      handle_touch(kSpectrumX + kSpectrumClosedW - 40, kZoomY + 10).kind ==
          ActionKind::span_up;
  const bool hidden_drawer_ok =
      handle_touch(center_x(kTunerAgc), center_y(kTunerAgc)).kind !=
          ActionKind::gain_auto &&
      handle_gain_drag(kGainX + kGainW, kGainY).kind == ActionKind::none;
  const int filter_edge = kSpectrumX + spectrum_width() / 2 + filter_half_width();
  const bool filter_drag_ok = spectrum_contains(kSpectrumX, kWaterfallY) &&
      handle_filter_drag(filter_edge, kSpectrumY + 20, true).kind ==
          ActionKind::filter_bandwidth_hz &&
      handle_filter_drag(filter_edge + 12, kSpectrumY + 20, true).value > 9000;
  (void)handle_filter_drag(0, 0, false);
  const bool tuner_opens =
      handle_touch(center_x(kTuner), center_y(kTuner)).kind == ActionKind::none &&
      g_tuner_drawer_open && spectrum_width() == kSpectrumOpenW;
  const bool drawer_ok =
      handle_touch(center_x(kTunerAgc), center_y(kTunerAgc)).kind ==
          ActionKind::gain_auto &&
      handle_gain_drag(kGainX + kGainW, kGainY).value == 496 &&
      handle_touch(kDrawer.x + 8, kSpectrumY + 20).kind == ActionKind::none &&
       handle_touch(kSpectrumX + kSpectrumOpenW - 8, kSpectrumY + 20).kind ==
           ActionKind::tune_hz;
  g_state.open(Modal::frequency);
  const bool modal_gestures_ok =
      !spectrum_contains(kSpectrumX + 20, kSpectrumY + 20) &&
      handle_filter_drag(filter_edge, kSpectrumY + 20, true).kind ==
          ActionKind::none &&
      handle_gain_drag(kGainX, kGainY).kind == ActionKind::none;
  g_state.close_modal();
  g_snapshot.controls.route = ReceiverRoute::direct_q;
  const bool direct_q_ok =
                           handle_touch(center_x(kTunerAgc), center_y(kTunerAgc)).kind ==
                               ActionKind::none &&
                           handle_gain_drag(kGainX, kGainY).kind == ActionKind::none &&
                           receiver_controls::item(
                               receiver_controls::Control::rf_gain,
                               g_snapshot.controls).availability !=
                               receiver_controls::Availability::enabled;
  const bool tuner_closes =
      handle_touch(center_x(kDrawerClose), center_y(kDrawerClose)).kind ==
          ActionKind::none &&
      !g_tuner_drawer_open && spectrum_width() == kSpectrumClosedW;
  constexpr Rect controls[] = {kStep, kFilterDown, kFilterValue, kFilterUp,
                               kClean, kBoost, kNoiseReduction, kNotch,
                               kSquelch, kTuner};
  bool geometry_ok = kFrequencyCard.x >= 0 && kFrequencyCard.y >= 0 &&
      kFrequencyCard.x + kFrequencyCard.w <= 1280 &&
      kFrequencyCard.y + kFrequencyCard.h <= 720 &&
      kSpectrumX + kSpectrumClosedW <= 1280 &&
      kSpectrumY + kSpectrumH <= 720 &&
      kWaterfallY + kWaterfallH <= 720 &&
      kDrawer.x + kDrawer.w <= 1280 && kDrawer.y + kDrawer.h <= 720;
  for (size_t i = 0; i < sizeof(controls) / sizeof(controls[0]); ++i) {
    geometry_ok = geometry_ok && controls[i].x >= 0 && controls[i].y >= 0 &&
        controls[i].x + controls[i].w <= 1280 &&
        controls[i].y + controls[i].h <= 720;
    for (size_t j = i + 1; j < sizeof(controls) / sizeof(controls[0]); ++j)
      geometry_ok = geometry_ok && !overlaps(controls[i], controls[j]);
  }
  const bool spectrum_layout_ok =
      spectrum_x_for_bin(0, 4) == kSpectrumX &&
      spectrum_x_for_bin(3, 4) == kSpectrumX + kSpectrumClosedW - 1 &&
      spectrum_tick_frequency(5850000, 480000, 0, 5) == 5610000 &&
      spectrum_tick_frequency(5850000, 480000, 2, 5) == 5850000 &&
      spectrum_tick_frequency(5850000, 480000, 4, 5) == 6090000 &&
      spectrum_tick_frequency(30000000, 2400000, 0, 5) == 28800000 &&
      spectrum_tick_frequency(30000000, 2400000, 1, 5) == 29400000 &&
      spectrum_tick_frequency(30000000, 2400000, 2, 5) == 30000000 &&
      spectrum_tick_frequency(30000000, 2400000, 3, 5) == 30600000 &&
      spectrum_tick_frequency(30000000, 2400000, 4, 5) == 31200000 &&
      handle_touch(kSpectrumX + 10, kSpectrumY + kSpectrumPlotH + 4).kind ==
          ActionKind::none &&
      kWaterfallY > kSpectrumY + kSpectrumH &&
      kWaterfallY + kWaterfallH <= kZoomY && kZoomY + 37 < kTabsY;
  const float resolution_test[] = {-80.0f, -20.0f, -75.0f, -40.0f};
  const bool peak_pool_ok =
      spectrum::peak_for_pixel(resolution_test, 0, 4, 0, 2) == -20.0f &&
      spectrum::peak_for_pixel(resolution_test, 0, 4, 1, 2) == -40.0f;
  g_snapshot.frequency_hz = 7100000;
  g_snapshot.span_hz = 480000;
  const bool touch_tune_bounds_ok =
      handle_touch(kSpectrumX + kSpectrumClosedW / 2, kSpectrumY).kind ==
          ActionKind::tune_hz &&
      handle_touch(kSpectrumX + kSpectrumClosedW / 2, kSpectrumY).value == 7100000 &&
      handle_touch(kSpectrumX - 1, kSpectrumY).kind == ActionKind::none &&
      handle_touch(kSpectrumX, kSpectrumY - 1).kind == ActionKind::none &&
      handle_touch(kSpectrumX + kSpectrumClosedW, kWaterfallY).kind == ActionKind::none &&
      handle_touch(kSpectrumX, kWaterfallY + kWaterfallH).kind == ActionKind::none &&
       handle_touch(kSpectrumX + 20, kSpectrumY + kSpectrumPlotH + 8).kind ==
           ActionKind::none;
  g_snapshot.frequency_hz = 5000000;
  const bool on_air_ok =
      handle_touch(kTabW + kTabW / 2, kTabsY + 20).kind == ActionKind::none &&
      handle_touch(640, 245).kind == ActionKind::tune_hz &&
      handle_touch(640, 245).value == 5000000;
  g_snapshot.frequency_hz = 5200000;
  const bool on_air_filter_ok = handle_touch(640, 245).kind == ActionKind::none;
  const bool hunt_cancel_ok =
      handle_touch(2 * kTabW + kTabW / 2, kTabsY + 20).kind == ActionKind::none &&
      handle_touch(3 * kTabW + kTabW / 2, kTabsY + 20).kind ==
          ActionKind::hunt_cancel;
  g_snapshot = saved;
  g_active = was_active;
  g_filter_dragging = was_filter_dragging;
  g_tuner_drawer_open = was_drawer_open;
  g_memory_page = saved_memory_page;
  g_state.open(saved_modal);
  g_state.select_tab(saved_tab);
  memcpy(g_entry, saved_entry, sizeof(g_entry));
  return controls_ok && hidden_drawer_ok && filter_drag_ok && tuner_opens &&
          drawer_ok && modal_gestures_ok && direct_q_ok && tuner_closes && geometry_ok &&
          spectrum_layout_ok && peak_pool_ok && touch_tune_bounds_ok &&
          on_air_ok && on_air_filter_ok && hunt_cancel_ok &&
          kTabsY + 90 <= 720 && model_self_check() && receiver_controls::self_check();
}

}  // namespace orcsdr::shortwave
