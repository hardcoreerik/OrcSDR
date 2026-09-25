#include "cb_dashboard.hpp"

#include "dashboard_audio_control.hpp"

#include <M5Unified.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace orcsdr::cb {
namespace {

constexpr uint16_t kPanel = 0x0841;
constexpr uint16_t kCyan = 0x2e7f;
constexpr uint16_t kGreen = 0x6fe8;
constexpr uint16_t kYellow = 0xff24;
constexpr uint16_t kMuted = 0x8c71;
constexpr uint16_t kGrid = 0x2945;
constexpr uint16_t kAmber = 0xfd20;
constexpr uint16_t kRedDim = 0x6000;
constexpr uint16_t kSelected = 0x1264;

struct Rect {
  int x;
  int y;
  int w;
  int h;
};

constexpr int kTabsY = 630;
constexpr int kTabW = 256;
constexpr int kTabCount = 5;

// LISTEN
constexpr Rect kNowCard{24, 104, 520, 256};
constexpr Rect kMeter{48, 300, 472, 22};
constexpr Rect kControlCard{556, 104, 700, 256};
constexpr int kControlX = 572;
constexpr int kControlY = 116;
constexpr int kControlW = 160;
constexpr int kControlH = 70;
constexpr int kControlGap = 12;
constexpr Rect kStrip{24, 372, 1232, 250};
constexpr int kBarTop = 408;
constexpr int kBarH = 170;
constexpr int kBarLabelY = 592;
constexpr float kBarMaxSnrDb = 40.0f;

// SPECTRUM
constexpr uint32_t kViewLowHz = 26955000;
constexpr uint32_t kViewHighHz = 27415000;
constexpr int kSpectrumX = 24;
constexpr int kSpectrumW = 1232;
constexpr int kSpectrumY = 146;
constexpr int kSpectrumH = 230;
constexpr int kAxisY = kSpectrumY + kSpectrumH;
constexpr int kAxisH = 30;
constexpr int kWaterfallY = kAxisY + kAxisH + 4;
constexpr int kWaterfallH = 184;

// ACTIVITY
constexpr int kLogRows = 7;
constexpr int kLogRowY = 196;
constexpr int kLogRowH = 56;

// CHANNELS
constexpr int kGridCols = 8;
constexpr int kGridX = 24;
constexpr int kGridY = 196;
constexpr int kCellW = 154;
constexpr int kCellH = 84;

// SETUP
constexpr int kSetupRows = 8;
constexpr int kSetupY = 104;
constexpr int kSetupRowH = 65;
constexpr Rect kSetupMinus{760, 3, 110, 52};
constexpr Rect kSetupPlus{1140, 3, 110, 52};
constexpr Rect kSetupValue{880, 3, 250, 52};

Snapshot g_snapshot{};
bool g_active = false;
Tab g_tab = Tab::listen;
size_t g_log_page = 0;
uint32_t g_activity_drawn_ms = 0;
uint16_t g_activity_signature = 0;
float g_bar_drawn[kChannelCount]{};
uint8_t g_bar_flags[kChannelCount]{};
EXT_RAM_BSS_ATTR uint16_t g_waterfall_row[kSpectrumW]{};

bool hit(int32_t x, int32_t y, const Rect& rect) {
  return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

bool overlaps(const Rect& a, const Rect& b) {
  return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

int center_x(const Rect& rect) { return rect.x + rect.w / 2; }
int center_y(const Rect& rect) { return rect.y + rect.h / 2; }

void text(const char* value, int x, int y, uint16_t color = TFT_WHITE, int size = 2,
          textdatum_t datum = middle_center) {
  M5.Display.setTextDatum(datum);
  M5.Display.setTextSize(size);
  M5.Display.setTextColor(color);
  M5.Display.drawString(value, x, y);
}

void card(const Rect& rect, uint16_t border = kCyan) {
  M5.Display.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 10, kPanel);
  M5.Display.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 10, border);
}

void button(const Rect& rect, const char* label, bool selected = false, bool enabled = true,
            int size = 2) {
  const uint16_t color = enabled ? (selected ? kGreen : kCyan) : TFT_DARKGREY;
  M5.Display.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 8, selected ? kSelected : kPanel);
  M5.Display.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 8, color);
  text(label, center_x(rect), center_y(rect), enabled ? TFT_WHITE : kMuted, size);
}

Rect control_rect(int column, int row) {
  return {kControlX + column * (kControlW + kControlGap),
          kControlY + row * (kControlH + 12), kControlW, kControlH};
}

// LISTEN control grid, row-major.
enum class Control : uint8_t {
  scan, hold, skip, lockout,
  channel_down, channel_up, ch9, ch19,
  mode, squelch_down, squelch_value, squelch_up,
};
constexpr int kControlCount = 12;

Rect control_rect(Control control) {
  const int index = static_cast<int>(control);
  return control_rect(index % 4, index / 4);
}

Rect bar_slot(size_t channel) {
  const int x0 = kStrip.x + 8 + static_cast<int>(channel * (kStrip.w - 16) / kChannelCount);
  const int x1 = kStrip.x + 8 +
                 static_cast<int>((channel + 1) * (kStrip.w - 16) / kChannelCount);
  return {x0, kBarTop, x1 - x0, kBarH};
}

Rect grid_cell(size_t channel) {
  const int column = static_cast<int>(channel % kGridCols);
  const int row = static_cast<int>(channel / kGridCols);
  return {kGridX + column * kCellW, kGridY + row * kCellH, kCellW - 8, kCellH - 8};
}

Rect setup_rect(const Rect& base, int row) {
  return {base.x, kSetupY + row * kSetupRowH + base.y, base.w, base.h};
}

Rect log_row(int row) { return {24, kLogRowY + row * kLogRowH, 1232, kLogRowH - 8}; }

constexpr Rect kLogPrev{24, 130, 80, 52};
constexpr Rect kLogNext{240, 130, 80, 52};
constexpr Rect kLogClear{1016, 130, 240, 52};
constexpr Rect kLockClear{760, 130, 240, 52};
constexpr Rect kLockSsb{1016, 130, 240, 52};

double mhz(uint32_t hz) { return static_cast<double>(hz) / 1000000.0; }

const char* state_label(char* buffer, size_t size) {
  const State state = g_snapshot.scan_state;
  if (state == State::off) return "MANUAL";
  if (state == State::hang) {
    snprintf(buffer, size, "HANG %.1fs",
             static_cast<double>(g_snapshot.hang_remaining_ms) / 1000.0);
    return buffer;
  }
  return state_name(state);
}

uint16_t state_color() {
  switch (g_snapshot.scan_state) {
    case State::receiving: return kGreen;
    case State::settling: return kGreen;
    case State::hang: return kYellow;
    case State::held: return kAmber;
    case State::watching: return kCyan;
    case State::off: return kMuted;
  }
  return kMuted;
}

void draw_header() {
  audio_header::draw_brand("CB SCANNER");
  M5.Display.drawFastVLine(350, 18, 58, kCyan);
  text("CB RADIO", 390, 42, TFT_WHITE, 4, middle_left);
  audio_header::draw_battery(g_snapshot.battery_percent);
  audio_header::draw_home_button();
  audio_header::draw_mute_button(g_snapshot.sound_enabled);
  audio_header::draw_visualizer_button(g_snapshot.running);
  audio_header::draw_settings_button();
  M5.Display.drawFastHLine(20, 92, 1240, kGreen);
}

void draw_tabs() {
  constexpr const char* tabs[kTabCount] = {"LISTEN", "SPECTRUM", "ACTIVITY", "CHANNELS",
                                           "SETUP"};
  for (int i = 0; i < kTabCount; ++i)
    button({i * kTabW + 4, kTabsY + 4, kTabW - 8, 82}, tabs[i],
           static_cast<int>(g_tab) == i);
}

void draw_page_title(const char* title, const char* subtitle) {
  M5.Display.fillRect(0, 93, 1280, kTabsY - 93, TFT_BLACK);
  text(title, 36, 124, TFT_WHITE, 3, middle_left);
  text(subtitle, 38, 160, kMuted, 2, middle_left);
}

// ---------------------------------------------------------------- LISTEN

void draw_meter() {
  M5.Display.fillRect(kMeter.x - 2, kMeter.y - 26, kMeter.w + 4, kMeter.h + 50, kPanel);
  M5.Display.drawRoundRect(kMeter.x, kMeter.y, kMeter.w, kMeter.h, 6, kCyan);
  const float level = std::clamp((g_snapshot.signal_dbfs + 90.0f) / 84.0f, 0.0f, 1.0f);
  const int filled = static_cast<int>(level * (kMeter.w - 4));
  const int s9_x = static_cast<int>((54.0f / 84.0f) * (kMeter.w - 4));
  if (filled > 0) {
    M5.Display.fillRect(kMeter.x + 2, kMeter.y + 3, std::min(filled, s9_x), kMeter.h - 6,
                        kGreen);
    if (filled > s9_x)
      M5.Display.fillRect(kMeter.x + 2 + s9_x, kMeter.y + 3, filled - s9_x, kMeter.h - 6,
                          TFT_RED);
  }
  static constexpr const char* marks[] = {"1", "3", "5", "7", "9", "+20", "+40"};
  static constexpr float marks_db[] = {-84.0f, -72.0f, -60.0f, -48.0f, -36.0f, -16.0f, 4.0f};
  for (size_t i = 0; i < std::size(marks); ++i) {
    const int x = kMeter.x + 2 +
                  static_cast<int>((marks_db[i] + 90.0f) / 84.0f * (kMeter.w - 4));
    text(marks[i], std::min(x, kMeter.x + kMeter.w - 12), kMeter.y - 12,
         i >= 5 ? TFT_RED : TFT_LIGHTGREY, 1);
  }
  int over = 0;
  const int s = s_units(g_snapshot.signal_dbfs, &over);
  char value[48];
  if (over > 0)
    snprintf(value, sizeof(value), "S9+%d  %.0f dBFS rel", over,
             static_cast<double>(g_snapshot.signal_dbfs));
  else
    snprintf(value, sizeof(value), "S%d  %.0f dBFS rel", s,
             static_cast<double>(g_snapshot.signal_dbfs));
  text(value, kMeter.x, kMeter.y + kMeter.h + 12, TFT_WHITE, 1, middle_left);
  text(g_snapshot.squelch_open ? "SQL OPEN" : "SQL CLOSED", kMeter.x + kMeter.w,
       kMeter.y + kMeter.h + 12, g_snapshot.squelch_open ? kGreen : kMuted, 1, middle_right);
}

void draw_now_playing() {
  card(kNowCard, state_color());
  char value[64];
  snprintf(value, sizeof(value), "CH %02u", static_cast<unsigned>(g_snapshot.channel + 1));
  text(value, 48, 162, TFT_WHITE, 7, middle_left);
  snprintf(value, sizeof(value), "%.3f MHz", mhz(g_snapshot.frequency_hz));
  text(value, 520, 140, kCyan, 3, middle_right);
  snprintf(value, sizeof(value), "%s  CLAR %+.1fk", mode_name(g_snapshot.mode),
           static_cast<double>(g_snapshot.clarifier_hz) / 1000.0);
  text(value, 520, 180, TFT_WHITE, 2, middle_right);
  text(channel_note(g_snapshot.channel), 48, 228, kMuted, 2, middle_left);
  char state_buffer[24];
  const char* state = state_label(state_buffer, sizeof(state_buffer));
  const uint16_t color = state_color();
  M5.Display.fillRoundRect(48, 250, 220, 34, 8, kSelected);
  M5.Display.drawRoundRect(48, 250, 220, 34, 8, color);
  text(state, 158, 267, color, 2);
  snprintf(value, sizeof(value), "%u ON AIR  |  %lu STOPS",
           static_cast<unsigned>(g_snapshot.active_count),
           static_cast<unsigned long>(g_snapshot.stops));
  text(value, 520, 267, g_snapshot.active_count ? kGreen : kMuted, 1, middle_right);
  draw_meter();
}

void draw_controls() {
  card(kControlCard);
  const bool scanning = g_snapshot.scan_state != State::off;
  const bool held = g_snapshot.scan_state == State::held;
  const bool locked = g_snapshot.channels[g_snapshot.channel].locked;
  button(control_rect(Control::scan), scanning ? "STOP SCAN" : "SCAN", scanning);
  button(control_rect(Control::hold), held ? "RESUME" : "HOLD", held, scanning);
  button(control_rect(Control::skip), "SKIP", false, scanning);
  button(control_rect(Control::lockout), locked ? "UNLOCK" : "LOCKOUT", locked);
  button(control_rect(Control::channel_down), "CH -");
  button(control_rect(Control::channel_up), "CH +");
  button(control_rect(Control::ch9), "CH 9", g_snapshot.channel == kEmergencyChannel);
  button(control_rect(Control::ch19), "CH 19", g_snapshot.channel == kHighwayChannel);
  char value[24];
  snprintf(value, sizeof(value), "MODE %s", mode_name(g_snapshot.mode));
  button(control_rect(Control::mode), value);
  button(control_rect(Control::squelch_down), "SQL -");
  if (g_snapshot.squelch_dbfs <= -90)
    snprintf(value, sizeof(value), "SQL OPEN");
  else
    snprintf(value, sizeof(value), "SQL %ld", static_cast<long>(g_snapshot.squelch_dbfs));
  button(control_rect(Control::squelch_value), value, g_snapshot.squelch_open);
  button(control_rect(Control::squelch_up), "SQL +");
}

uint8_t bar_flags(size_t channel) {
  const ChannelView& view = g_snapshot.channels[channel];
  return static_cast<uint8_t>((view.active ? 1u : 0u) | (view.locked ? 2u : 0u) |
                              (view.skipped ? 4u : 0u) |
                              (channel == g_snapshot.channel ? 8u : 0u) |
                              (g_snapshot.scan.priority_enabled &&
                                       channel == g_snapshot.scan.priority_channel
                                   ? 16u
                                   : 0u));
}

int bar_height(size_t channel) {
  const float snr = std::clamp(g_snapshot.channels[channel].snr_db, 0.0f, kBarMaxSnrDb);
  return static_cast<int>(snr / kBarMaxSnrDb * (kBarH - 4));
}

int threshold_y() {
  const float threshold = std::clamp(g_snapshot.scan.threshold_db, 0.0f, kBarMaxSnrDb);
  return kBarTop + kBarH - 2 - static_cast<int>(threshold / kBarMaxSnrDb * (kBarH - 4));
}

void draw_bar(size_t channel, bool force) {
  const int height = bar_height(channel);
  const uint8_t flags = bar_flags(channel);
  if (!force && flags == g_bar_flags[channel] &&
      std::fabs(static_cast<float>(height) - g_bar_drawn[channel]) < 2.0f)
    return;
  g_bar_drawn[channel] = static_cast<float>(height);
  g_bar_flags[channel] = flags;
  const Rect slot = bar_slot(channel);
  const ChannelView& view = g_snapshot.channels[channel];
  const bool tuned = (flags & 8u) != 0;
  M5.Display.fillRect(slot.x + 1, slot.y, slot.w - 2, slot.h, tuned ? kSelected : TFT_BLACK);
  const uint16_t color = view.locked ? kRedDim
                         : view.skipped ? kMuted
                         : view.active ? (tuned ? kGreen : kYellow)
                                       : 0x2a69;
  if (height > 0)
    M5.Display.fillRect(slot.x + 4, slot.y + slot.h - 2 - height, slot.w - 8, height, color);
  if (view.locked) {
    M5.Display.drawLine(slot.x + 4, slot.y + slot.h - 30, slot.x + slot.w - 5,
                        slot.y + slot.h - 6, TFT_RED);
    M5.Display.drawLine(slot.x + slot.w - 5, slot.y + slot.h - 30, slot.x + 4,
                        slot.y + slot.h - 6, TFT_RED);
  }
  M5.Display.drawFastHLine(slot.x + 1, threshold_y(), slot.w - 2, kYellow);
  if (tuned) M5.Display.drawRect(slot.x + 1, slot.y, slot.w - 2, slot.h, kCyan);
  // Label row: channel number plus P marker for priority.
  M5.Display.fillRect(slot.x, kBarLabelY - 12, slot.w, 24, kPanel);
  char label[4];
  snprintf(label, sizeof(label), "%u", static_cast<unsigned>(channel + 1));
  text(label, slot.x + slot.w / 2, kBarLabelY,
       view.locked ? TFT_RED : tuned ? kCyan : view.active ? kGreen : TFT_LIGHTGREY, 1);
  if (flags & 16u) text("P", slot.x + slot.w / 2, kBarLabelY + 12, kAmber, 1);
  else M5.Display.fillRect(slot.x, kBarLabelY + 6, slot.w, 12, kPanel);
}

void draw_strip_static() {
  card(kStrip);
  text("ALL 40 CHANNELS  -  LIVE BAND ACTIVITY", kStrip.x + 16, kStrip.y + 18, kCyan, 2,
       middle_left);
  char value[48];
  snprintf(value, sizeof(value), "SCAN THRESHOLD +%.0f dB", static_cast<double>(
                                                              g_snapshot.scan.threshold_db));
  text(value, kStrip.x + kStrip.w - 16, kStrip.y + 18, kYellow, 1, middle_right);
  for (size_t channel = 0; channel < kChannelCount; ++channel) draw_bar(channel, true);
}

void draw_strip() {
  for (size_t channel = 0; channel < kChannelCount; ++channel) draw_bar(channel, false);
}

void draw_listen() {
  M5.Display.fillRect(0, 93, 1280, kTabsY - 93, TFT_BLACK);
  draw_now_playing();
  draw_controls();
  draw_strip_static();
}

// -------------------------------------------------------------- SPECTRUM

int x_for_hz(uint32_t hz) {
  return kSpectrumX + static_cast<int>(static_cast<int64_t>(hz - kViewLowHz) * (kSpectrumW - 1) /
                                       static_cast<int64_t>(kViewHighHz - kViewLowHz));
}

void draw_spectrum_static() {
  M5.Display.clearScrollRect();
  draw_page_title("BAND SPECTRUM", "26.955 - 27.415 MHz. Tap a signal to tune its channel.");
  M5.Display.drawRect(kSpectrumX, kSpectrumY, kSpectrumW, kSpectrumH, kGrid);
  for (int i = 1; i < 4; ++i)
    M5.Display.drawFastHLine(kSpectrumX, kSpectrumY + i * kSpectrumH / 4, kSpectrumW, kGrid);
  M5.Display.fillRect(kSpectrumX, kAxisY, kSpectrumW, kAxisH, TFT_BLACK);
  for (size_t channel = 0; channel < kChannelCount; ++channel) {
    const int x = x_for_hz(kChannelsHz[channel]);
    M5.Display.drawFastVLine(x, kAxisY, 6, kGrid);
    if (channel == 0 || (channel + 1) % 5 == 0 || channel == kEmergencyChannel ||
        channel == kHighwayChannel) {
      char label[4];
      snprintf(label, sizeof(label), "%u", static_cast<unsigned>(channel + 1));
      text(label, x, kAxisY + 18,
           channel == kEmergencyChannel || channel == kHighwayChannel ? kAmber : TFT_LIGHTGREY,
           1);
    }
  }
  M5.Display.drawRect(kSpectrumX, kWaterfallY, kSpectrumW, kWaterfallH, kCyan);
  M5.Display.setScrollRect(kSpectrumX + 1, kWaterfallY + 1, kSpectrumW - 2, kWaterfallH - 2,
                           TFT_BLACK);
}

uint16_t waterfall_color(float level) {
  level = std::clamp(level, 0.0f, 1.0f);
  const uint8_t r = level < 0.55f ? 0 : static_cast<uint8_t>((level - 0.55f) * 566);
  const uint8_t g = level < 0.2f ? 0 : static_cast<uint8_t>(std::min(255.0f, (level - 0.2f) * 510));
  const uint8_t b = level < 0.65f ? static_cast<uint8_t>((0.65f - level) * 390) : 0;
  return M5.Display.color565(r, g, b);
}

// ------------------------------------------------------------- ACTIVITY

void format_ago(uint32_t elapsed_ms, char* buffer, size_t size) {
  const uint32_t seconds = elapsed_ms / 1000u;
  if (seconds < 60) snprintf(buffer, size, "%lus ago", static_cast<unsigned long>(seconds));
  else if (seconds < 3600)
    snprintf(buffer, size, "%lum ago", static_cast<unsigned long>(seconds / 60u));
  else snprintf(buffer, size, "%luh ago", static_cast<unsigned long>(seconds / 3600u));
}

size_t log_pages() {
  return std::max<size_t>(1, (g_snapshot.log_count + kLogRows - 1) / kLogRows);
}

uint16_t activity_signature() {
  uint16_t signature = g_snapshot.log_count;
  if (g_snapshot.log_count)
    signature = static_cast<uint16_t>(signature * 31u + g_snapshot.log[0].start_ms);
  for (size_t channel = 0; channel < kChannelCount; ++channel)
    if (g_snapshot.channels[channel].active)
      signature = static_cast<uint16_t>(signature * 7u + channel + 1u);
  return signature;
}

void draw_activity() {
  draw_page_title("ACTIVITY LOG", "");
  g_log_page = std::min(g_log_page, log_pages() - 1);
  button(kLogPrev, "<", false, g_log_page > 0);
  char value[96];
  snprintf(value, sizeof(value), "%u / %u", static_cast<unsigned>(g_log_page + 1),
           static_cast<unsigned>(log_pages()));
  text(value, 172, 156, kMuted, 2);
  button(kLogNext, ">", false, g_log_page + 1 < log_pages());
  size_t written = 0;
  written += snprintf(value, sizeof(value), "ON AIR: ");
  size_t listed = 0;
  for (size_t channel = 0; channel < kChannelCount && written + 8 < sizeof(value); ++channel) {
    if (!g_snapshot.channels[channel].active) continue;
    written += snprintf(value + written, sizeof(value) - written, "%s%u",
                        listed++ ? ", " : "", static_cast<unsigned>(channel + 1));
  }
  if (!listed) snprintf(value, sizeof(value), "ON AIR: none");
  text(value, 360, 156, listed ? kGreen : kMuted, 2, middle_left);
  button(kLogClear, "CLEAR LOG", false, g_snapshot.log_count > 0);
  if (!g_snapshot.log_count) {
    card({24, 220, 1232, 300});
    text("NO TRANSMISSIONS HEARD YET", 640, 330, TFT_WHITE, 3);
    text("Every channel is watched at once, even while listening to one.", 640, 385,
         kMuted, 2);
    text("Transmissions longer than 0.25 s appear here with length and strength.", 640,
         420, kMuted, 2);
    return;
  }
  const size_t first = g_log_page * kLogRows;
  for (int row = 0; row < kLogRows; ++row) {
    const size_t index = first + static_cast<size_t>(row);
    if (index >= g_snapshot.log_count) break;
    const Hit& hit_entry = g_snapshot.log[index];
    char ago[16];
    const uint32_t ended = hit_entry.start_ms + hit_entry.duration_ms;
    format_ago(g_snapshot.now_ms - ended, ago, sizeof(ago));
    snprintf(value, sizeof(value), "CH %02u   %.3f MHz   %5.1f s   +%.0f dB   %s",
             static_cast<unsigned>(hit_entry.channel + 1),
             mhz(kChannelsHz[hit_entry.channel % kChannelCount]),
             static_cast<double>(hit_entry.duration_ms) / 1000.0,
             static_cast<double>(hit_entry.peak_snr_db), ago);
    const Rect rect = log_row(row);
    button(rect, value, hit_entry.channel == g_snapshot.channel);
  }
}

// ------------------------------------------------------------- CHANNELS

void draw_cell(size_t channel) {
  const Rect cell = grid_cell(channel);
  const ChannelView& view = g_snapshot.channels[channel];
  const bool tuned = channel == g_snapshot.channel;
  const uint16_t border = view.locked ? TFT_RED : view.active ? kGreen : tuned ? kCyan : kGrid;
  M5.Display.fillRoundRect(cell.x, cell.y, cell.w, cell.h, 8, tuned ? kSelected : kPanel);
  M5.Display.drawRoundRect(cell.x, cell.y, cell.w, cell.h, 8, border);
  char value[24];
  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(channel + 1));
  text(value, cell.x + 12, cell.y + 24, view.locked ? TFT_RED : TFT_WHITE, 3, middle_left);
  snprintf(value, sizeof(value), "%.3f", mhz(kChannelsHz[channel]));
  text(value, cell.x + cell.w - 10, cell.y + 18, kMuted, 1, middle_right);
  if (g_snapshot.scan.priority_enabled && channel == g_snapshot.scan.priority_channel)
    text("PRI", cell.x + cell.w - 10, cell.y + 36, kAmber, 1, middle_right);
  if (view.locked) snprintf(value, sizeof(value), "LOCKED OUT");
  else snprintf(value, sizeof(value), "%u HITS", static_cast<unsigned>(view.hits));
  text(value, cell.x + 12, cell.y + cell.h - 16, view.locked ? TFT_RED : TFT_LIGHTGREY, 1,
       middle_left);
}

void draw_channels() {
  size_t locked = 0;
  for (const auto& view : g_snapshot.channels) locked += view.locked ? 1u : 0u;
  char subtitle[80];
  snprintf(subtitle, sizeof(subtitle), "Tap a channel to lock it out of the scan  |  %u of 40 scanned",
           static_cast<unsigned>(kChannelCount - locked));
  draw_page_title("SCAN LIST", subtitle);
  button(kLockClear, "SCAN ALL 40", locked == 0);
  button(kLockSsb, "SSB 36-40 ONLY");
  for (size_t channel = 0; channel < kChannelCount; ++channel) draw_cell(channel);
}

// ---------------------------------------------------------------- SETUP

void setup_value(int row, char* value, size_t size) {
  const Settings& scan = g_snapshot.scan;
  switch (row) {
    case 0: snprintf(value, size, "%s", mode_name(g_snapshot.mode)); break;
    case 1:
      snprintf(value, size, "%+.1f kHz", static_cast<double>(g_snapshot.clarifier_hz) / 1000.0);
      break;
    case 2:
      if (g_snapshot.squelch_dbfs <= -90) snprintf(value, size, "OPEN");
      else snprintf(value, size, "%ld dBFS", static_cast<long>(g_snapshot.squelch_dbfs));
      break;
    case 3:
      snprintf(value, size, "+%.0f dB", static_cast<double>(scan.threshold_db));
      break;
    case 4:
      snprintf(value, size, "%.1f s", static_cast<double>(scan.hang_ms) / 1000.0);
      break;
    case 5:
      if (!scan.max_hold_s) snprintf(value, size, "UNLIMITED");
      else snprintf(value, size, "%u s", static_cast<unsigned>(scan.max_hold_s));
      break;
    case 6:
      if (!scan.priority_enabled) snprintf(value, size, "OFF");
      else snprintf(value, size, "CH %u", static_cast<unsigned>(scan.priority_channel + 1));
      break;
    case 7: snprintf(value, size, "%s", scan.auto_sideband ? "ON" : "OFF"); break;
    default: value[0] = '\0'; break;
  }
}

constexpr const char* kSetupLabels[kSetupRows] = {
    "DEMOD MODE", "CLARIFIER (SSB)", "AUDIO SQUELCH", "SCAN THRESHOLD", "HANG TIME",
    "MAX HOLD", "PRIORITY CHANNEL", "AUTO SIDEBAND"};
constexpr const char* kSetupHelp[kSetupRows] = {
    "AM for channels 1-40, USB/LSB for sideband",
    "Fine-tune sideband voices by ear",
    "Mutes audio below this level",
    "Signal above band noise that stops the scan",
    "Wait for a reply before scanning again",
    "Skip a stuck carrier after this long",
    "Checked first, interrupts other channels",
    "LSB on channels 36-40, AM on 1-35 when a channel is tuned"};

void draw_setup() {
  M5.Display.fillRect(0, 93, 1280, kTabsY - 93, TFT_BLACK);
  for (int row = 0; row < kSetupRows; ++row) {
    const int y = kSetupY + row * kSetupRowH;
    M5.Display.fillRoundRect(24, y, 1232, kSetupRowH - 7, 8, kPanel);
    // Size 3 name over a size 2 description: size 1 was unreadable on the
    // 5-inch panel. The longest description (57 chars) ends at x=728, clear
    // of the -/+ buttons at x=760.
    text(kSetupLabels[row], 44, y + 17, TFT_WHITE, 3, middle_left);
    text(kSetupHelp[row], 44, y + 43, kMuted, 2, middle_left);
    char value[32];
    setup_value(row, value, sizeof(value));
    const bool cycle = row == 0 || row >= 5;
    if (!cycle) button(setup_rect(kSetupMinus, row), "-");
    button(setup_rect(kSetupValue, row), value, cycle);
    if (!cycle) button(setup_rect(kSetupPlus, row), "+");
  }
}

void draw_page() {
  if (g_tab != Tab::spectrum) M5.Display.clearScrollRect();
  switch (g_tab) {
    case Tab::listen: draw_listen(); break;
    case Tab::spectrum: draw_spectrum_static(); break;
    case Tab::activity:
      draw_activity();
      g_activity_signature = activity_signature();
      g_activity_drawn_ms = g_snapshot.now_ms;
      break;
    case Tab::channels: draw_channels(); break;
    case Tab::setup: draw_setup(); break;
  }
}

bool readout_changed(const Snapshot& before, const Snapshot& after) {
  return before.channel != after.channel || before.mode != after.mode ||
         before.clarifier_hz != after.clarifier_hz || before.scan_state != after.scan_state ||
         before.stops != after.stops || before.active_count != after.active_count ||
         before.hang_remaining_ms / 100u != after.hang_remaining_ms / 100u;
}

bool controls_changed(const Snapshot& before, const Snapshot& after) {
  return before.channel != after.channel || before.mode != after.mode ||
         before.squelch_dbfs != after.squelch_dbfs ||
         before.squelch_open != after.squelch_open ||
         (before.scan_state == State::off) != (after.scan_state == State::off) ||
         (before.scan_state == State::held) != (after.scan_state == State::held) ||
         before.channels[after.channel].locked != after.channels[after.channel].locked;
}

bool settings_changed(const Snapshot& before, const Snapshot& after) {
  return before.mode != after.mode || before.clarifier_hz != after.clarifier_hz ||
         before.squelch_dbfs != after.squelch_dbfs ||
         before.scan.threshold_db != after.scan.threshold_db ||
         before.scan.hang_ms != after.scan.hang_ms ||
         before.scan.max_hold_s != after.scan.max_hold_s ||
         before.scan.priority_enabled != after.scan.priority_enabled ||
         before.scan.priority_channel != after.scan.priority_channel ||
         before.scan.auto_sideband != after.scan.auto_sideband;
}

bool grid_changed(const Snapshot& before, const Snapshot& after, size_t channel) {
  const ChannelView& a = before.channels[channel];
  const ChannelView& b = after.channels[channel];
  const bool was_tuned = before.channel == channel;
  const bool is_tuned = after.channel == channel;
  const bool was_pri = before.scan.priority_enabled && before.scan.priority_channel == channel;
  const bool is_pri = after.scan.priority_enabled && after.scan.priority_channel == channel;
  return a.active != b.active || a.locked != b.locked || a.hits != b.hits ||
         was_tuned != is_tuned || was_pri != is_pri;
}

Action tab_touch(int32_t x) {
  const int index = std::clamp<int32_t>(x / kTabW, 0, kTabCount - 1);
  g_tab = static_cast<Tab>(index);
  g_log_page = 0;
  draw_tabs();
  draw_page();
  return {};
}

Action listen_touch(int32_t x, int32_t y) {
  for (int i = 0; i < kControlCount; ++i) {
    const auto control = static_cast<Control>(i);
    if (!hit(x, y, control_rect(control))) continue;
    const bool scanning = g_snapshot.scan_state != State::off;
    switch (control) {
      case Control::scan: return {ActionKind::scan_toggle};
      case Control::hold: return scanning ? Action{ActionKind::hold_toggle} : Action{};
      case Control::skip: return scanning ? Action{ActionKind::skip} : Action{};
      case Control::lockout: return {ActionKind::lockout_current};
      case Control::channel_down: return {ActionKind::channel_down};
      case Control::channel_up: return {ActionKind::channel_up};
      case Control::ch9:
        return {ActionKind::tune_channel, static_cast<int32_t>(kEmergencyChannel)};
      case Control::ch19:
        return {ActionKind::tune_channel, static_cast<int32_t>(kHighwayChannel)};
      case Control::mode: return {ActionKind::mode_cycle};
      case Control::squelch_down: return {ActionKind::squelch_down};
      case Control::squelch_value: return {};
      case Control::squelch_up: return {ActionKind::squelch_up};
    }
  }
  for (size_t channel = 0; channel < kChannelCount; ++channel) {
    const Rect slot = bar_slot(channel);
    if (hit(x, y, {slot.x, slot.y, slot.w, kBarLabelY + 20 - slot.y}))
      return {ActionKind::tune_channel, static_cast<int32_t>(channel)};
  }
  return {};
}

Action spectrum_touch(int32_t x, int32_t y) {
  if (!hit(x, y, {kSpectrumX, kSpectrumY, kSpectrumW, kWaterfallY + kWaterfallH - kSpectrumY}))
    return {};
  const uint32_t hz = kViewLowHz + static_cast<uint32_t>(
      static_cast<int64_t>(x - kSpectrumX) * (kViewHighHz - kViewLowHz) / (kSpectrumW - 1));
  return {ActionKind::tune_channel, static_cast<int32_t>(nearest_channel(hz))};
}

Action activity_touch(int32_t x, int32_t y) {
  if (hit(x, y, kLogPrev) && g_log_page > 0) {
    --g_log_page;
    draw_activity();
    return {};
  }
  if (hit(x, y, kLogNext) && g_log_page + 1 < log_pages()) {
    ++g_log_page;
    draw_activity();
    return {};
  }
  if (hit(x, y, kLogClear) && g_snapshot.log_count) return {ActionKind::log_clear};
  const size_t first = g_log_page * kLogRows;
  for (int row = 0; row < kLogRows; ++row) {
    const size_t index = first + static_cast<size_t>(row);
    if (index >= g_snapshot.log_count) break;
    if (hit(x, y, log_row(row)))
      return {ActionKind::tune_channel,
              static_cast<int32_t>(g_snapshot.log[index].channel % kChannelCount)};
  }
  return {};
}

Action channels_touch(int32_t x, int32_t y) {
  if (hit(x, y, kLockClear)) return {ActionKind::lockout_clear};
  if (hit(x, y, kLockSsb)) return {ActionKind::lockout_ssb_only};
  for (size_t channel = 0; channel < kChannelCount; ++channel)
    if (hit(x, y, grid_cell(channel)))
      return {ActionKind::lockout_toggle, static_cast<int32_t>(channel)};
  return {};
}

Action setup_touch(int32_t x, int32_t y) {
  static constexpr ActionKind minus[kSetupRows] = {
      ActionKind::mode_cycle, ActionKind::clarifier_down, ActionKind::squelch_down,
      ActionKind::threshold_down, ActionKind::hang_down, ActionKind::max_hold_cycle,
      ActionKind::priority_cycle, ActionKind::auto_sideband_toggle};
  static constexpr ActionKind plus[kSetupRows] = {
      ActionKind::mode_cycle, ActionKind::clarifier_up, ActionKind::squelch_up,
      ActionKind::threshold_up, ActionKind::hang_up, ActionKind::max_hold_cycle,
      ActionKind::priority_cycle, ActionKind::auto_sideband_toggle};
  for (int row = 0; row < kSetupRows; ++row) {
    const bool cycle = row == 0 || row >= 5;
    if (hit(x, y, setup_rect(kSetupValue, row)))
      return cycle ? Action{minus[row]} : Action{};
    if (!cycle && hit(x, y, setup_rect(kSetupMinus, row))) return {minus[row]};
    if (!cycle && hit(x, y, setup_rect(kSetupPlus, row))) return {plus[row]};
  }
  return {};
}

}  // namespace

const char* mode_name(Mode mode) {
  switch (mode) {
    case Mode::usb: return "USB";
    case Mode::lsb: return "LSB";
    case Mode::am: break;
  }
  return "AM";
}

int s_units(float signal_dbfs, int* over_s9_db) {
  constexpr float kS9 = -36.0f;
  if (over_s9_db) *over_s9_db = 0;
  if (signal_dbfs > kS9) {
    if (over_s9_db) *over_s9_db = static_cast<int>(std::lround(signal_dbfs - kS9));
    return 9;
  }
  const int units = 9 - static_cast<int>(std::ceil((kS9 - signal_dbfs) / 6.0f));
  return std::clamp(units, 0, 9);
}

void enter(const Snapshot& snapshot) {
  g_snapshot = snapshot;
  g_active = true;
  g_log_page = 0;
  draw();
}

void leave() {
  if (g_active) M5.Display.clearScrollRect();
  g_active = false;
}

void draw() {
  if (!g_active) return;
  M5.Display.clearScrollRect();
  M5.Display.fillScreen(TFT_BLACK);
  draw_header();
  draw_tabs();
  draw_page();
}

void update(const Snapshot& snapshot) {
  if (!g_active) return;
  // Static: two ~2 KB snapshots would otherwise sit on the UI task stack.
  // PSRAM: internal RAM must keep room for the boot-time DMA reserve, and
  // this copy is always overwritten before it is read.
  EXT_RAM_BSS_ATTR static Snapshot before;
  before = g_snapshot;
  g_snapshot = snapshot;
  switch (g_tab) {
    case Tab::listen:
      if (readout_changed(before, snapshot) ||
          before.squelch_open != snapshot.squelch_open)
        draw_now_playing();
      else
        draw_meter();
      if (controls_changed(before, snapshot)) draw_controls();
      if (before.scan.threshold_db != snapshot.scan.threshold_db ||
          before.scan.priority_enabled != snapshot.scan.priority_enabled ||
          before.scan.priority_channel != snapshot.scan.priority_channel)
        draw_strip_static();
      else
        draw_strip();
      break;
    case Tab::spectrum: break;
    case Tab::activity:
      if (activity_signature() != g_activity_signature ||
          snapshot.now_ms - g_activity_drawn_ms >= 5000u) {
        draw_activity();
        g_activity_signature = activity_signature();
        g_activity_drawn_ms = snapshot.now_ms;
      }
      break;
    case Tab::channels: {
      size_t locked_before = 0;
      size_t locked_after = 0;
      for (size_t channel = 0; channel < kChannelCount; ++channel) {
        locked_before += before.channels[channel].locked ? 1u : 0u;
        locked_after += snapshot.channels[channel].locked ? 1u : 0u;
      }
      if (locked_before != locked_after) {
        draw_channels();
        break;
      }
      for (size_t channel = 0; channel < kChannelCount; ++channel)
        if (grid_changed(before, snapshot, channel)) draw_cell(channel);
      break;
    }
    case Tab::setup:
      if (settings_changed(before, snapshot)) draw_setup();
      break;
  }
}

void draw_spectrum(const float* bins, size_t bin_count, uint32_t sample_rate_sps,
                   uint32_t center_hz) {
  if (!spectrum_active() || !bins || bin_count < 16 || sample_rate_sps == 0) return;
  const double bins_per_hz = static_cast<double>(bin_count) / sample_rate_sps;
  const double hz_per_pixel =
      static_cast<double>(kViewHighHz - kViewLowHz) / static_cast<double>(kSpectrumW - 1);
  // PSRAM scratch (fully rewritten each frame): keeps ~5 KB out of the
  // internal RAM the boot-time DMA reserve needs.
  EXT_RAM_BSS_ATTR static float column[kSpectrumW];
  float maximum = -200.0f;
  for (int x = 0; x < kSpectrumW; ++x) {
    const double hz = kViewLowHz + x * hz_per_pixel;
    const int64_t first = static_cast<int64_t>(bin_count / 2) +
                          std::llround((hz - center_hz) * bins_per_hz);
    const int64_t last = static_cast<int64_t>(bin_count / 2) +
                         std::llround((hz + hz_per_pixel - center_hz) * bins_per_hz);
    float peak = kNoLevel;
    for (int64_t bin = first; bin <= std::max(first, last); ++bin)
      if (bin >= 0 && bin < static_cast<int64_t>(bin_count)) peak = std::max(peak, bins[bin]);
    column[x] = peak;
    maximum = std::max(maximum, peak);
  }
  const float floor = maximum - 48.0f;
  constexpr int kPlotH = kSpectrumH - 2;
  M5.Display.startWrite();
  M5.Display.fillRect(kSpectrumX + 1, kSpectrumY + 1, kSpectrumW - 2, kPlotH, TFT_BLACK);
  for (int i = 1; i < 4; ++i)
    M5.Display.drawFastHLine(kSpectrumX + 1, kSpectrumY + i * kSpectrumH / 4, kSpectrumW - 2,
                             kGrid);
  for (size_t channel = 0; channel < kChannelCount; ++channel) {
    const ChannelView& view = g_snapshot.channels[channel];
    if (!view.active && channel != g_snapshot.channel) continue;
    const int left = x_for_hz(kChannelsHz[channel] - kChannelHalfWidthHz);
    const int right = x_for_hz(kChannelsHz[channel] + kChannelHalfWidthHz);
    M5.Display.fillRect(left, kSpectrumY + 1, std::max(1, right - left), kPlotH,
                        channel == g_snapshot.channel ? kSelected : 0x0220);
  }
  int last_x = kSpectrumX;
  int last_y = kSpectrumY + kPlotH;
  for (int x = 0; x < kSpectrumW; ++x) {
    const float normalized =
        column[x] <= kNoLevel ? 0.0f : std::clamp((column[x] - floor) / 48.0f, 0.0f, 1.0f);
    const int px = kSpectrumX + x;
    const int py = kSpectrumY + kPlotH - static_cast<int>(normalized * (kPlotH - 4));
    if (x) M5.Display.drawLine(last_x, last_y, px, py, kGreen);
    last_x = px;
    last_y = py;
    g_waterfall_row[x] = waterfall_color(normalized);
  }
  const int tuned_x = x_for_hz(kChannelsHz[g_snapshot.channel]);
  M5.Display.drawFastVLine(tuned_x, kSpectrumY + 1, kPlotH, kCyan);
  M5.Display.scroll(0, -1);
  M5.Display.pushImage(kSpectrumX + 1, kWaterfallY + kWaterfallH - 2, kSpectrumW - 2, 1,
                       g_waterfall_row + 1);
  M5.Display.endWrite();
}

Action handle_touch(int32_t x, int32_t y) {
  if (!g_active) return {};
  if (audio_header::home_hit(x, y)) return {ActionKind::exit_home};
  if (audio_header::settings_hit(x, y)) return {ActionKind::open_settings};
  if (y >= kTabsY) return tab_touch(x);
  switch (g_tab) {
    case Tab::listen: return listen_touch(x, y);
    case Tab::spectrum: return spectrum_touch(x, y);
    case Tab::activity: return activity_touch(x, y);
    case Tab::channels: return channels_touch(x, y);
    case Tab::setup: return setup_touch(x, y);
  }
  return {};
}

bool active() { return g_active; }
bool spectrum_active() { return g_active && g_tab == Tab::spectrum; }
Tab tab() { return g_tab; }

bool dashboard_self_check() {
  EXT_RAM_BSS_ATTR static Snapshot saved;  // PSRAM; overwritten before use.
  saved = g_snapshot;
  const bool was_active = g_active;
  const Tab saved_tab = g_tab;
  const size_t saved_page = g_log_page;
  // Hit tests only: the per-tab handlers are probed directly so no page draws.
  g_snapshot = Snapshot{};
  g_snapshot.scan_state = State::watching;
  g_snapshot.log_count = 1;
  g_snapshot.log[0].channel = 22;
  g_active = true;
  g_tab = Tab::listen;
  const auto touch = [](const Rect& rect) { return handle_touch(center_x(rect), center_y(rect)); };
  bool ok = touch(control_rect(Control::scan)).kind == ActionKind::scan_toggle &&
            touch(control_rect(Control::hold)).kind == ActionKind::hold_toggle &&
            touch(control_rect(Control::skip)).kind == ActionKind::skip &&
            touch(control_rect(Control::lockout)).kind == ActionKind::lockout_current &&
            touch(control_rect(Control::channel_down)).kind == ActionKind::channel_down &&
            touch(control_rect(Control::channel_up)).kind == ActionKind::channel_up &&
            touch(control_rect(Control::ch9)).value == static_cast<int32_t>(kEmergencyChannel) &&
            touch(control_rect(Control::ch19)).value == static_cast<int32_t>(kHighwayChannel) &&
            touch(control_rect(Control::mode)).kind == ActionKind::mode_cycle &&
            touch(control_rect(Control::squelch_down)).kind == ActionKind::squelch_down &&
            touch(control_rect(Control::squelch_up)).kind == ActionKind::squelch_up;
  const Rect first_bar = bar_slot(0);
  const Rect last_bar = bar_slot(kChannelCount - 1);
  ok = ok && touch(first_bar).kind == ActionKind::tune_channel && touch(first_bar).value == 0 &&
       touch(last_bar).value == static_cast<int32_t>(kChannelCount - 1) &&
       last_bar.x + last_bar.w <= kStrip.x + kStrip.w && first_bar.w >= 28;
  g_snapshot.scan_state = State::off;
  ok = ok && touch(control_rect(Control::hold)).kind == ActionKind::none &&
       touch(control_rect(Control::skip)).kind == ActionKind::none;

  // Geometry: every control inside its card, no overlaps, all on screen.
  bool geometry = kTabsY + 90 <= 720 && kWaterfallY + kWaterfallH <= kTabsY &&
                  kStrip.y + kStrip.h <= kTabsY && kNowCard.y + kNowCard.h < kStrip.y &&
                  kGridY + 5 * kCellH <= kTabsY + 8 &&
                  kGridX + kGridCols * kCellW <= 1280 &&
                  kSetupY + kSetupRows * kSetupRowH <= kTabsY + 7 &&
                  log_row(kLogRows - 1).y + log_row(kLogRows - 1).h <= kTabsY &&
                  !overlaps(kNowCard, kControlCard);
  for (int i = 0; i < kControlCount; ++i) {
    const Rect a = control_rect(static_cast<Control>(i));
    geometry = geometry && a.x >= kControlCard.x && a.y >= kControlCard.y &&
               a.x + a.w <= kControlCard.x + kControlCard.w &&
               a.y + a.h <= kControlCard.y + kControlCard.h;
    for (int j = i + 1; j < kControlCount; ++j)
      geometry = geometry && !overlaps(a, control_rect(static_cast<Control>(j)));
  }
  for (size_t channel = 0; channel + 1 < kChannelCount; ++channel)
    geometry = geometry && !overlaps(grid_cell(channel), grid_cell(channel + 1)) &&
               !overlaps(bar_slot(channel), bar_slot(channel + 1));
  ok = ok && geometry;

  g_tab = Tab::spectrum;
  const Action tuned = spectrum_touch(x_for_hz(kChannelsHz[kHighwayChannel]), kSpectrumY + 20);
  ok = ok && tuned.kind == ActionKind::tune_channel &&
       tuned.value == static_cast<int32_t>(kHighwayChannel) &&
       spectrum_touch(x_for_hz(kChannelsHz[0]), kWaterfallY + 10).value == 0 &&
       spectrum_touch(x_for_hz(kChannelsHz[39]), kSpectrumY + 5).value == 39 &&
       spectrum_touch(kSpectrumX, kSpectrumY - 1).kind == ActionKind::none;
  ok = ok && activity_touch(center_x(log_row(0)), center_y(log_row(0))).value == 22 &&
       activity_touch(center_x(log_row(1)), center_y(log_row(1))).kind == ActionKind::none &&
       activity_touch(center_x(kLogClear), center_y(kLogClear)).kind == ActionKind::log_clear;
  ok = ok && channels_touch(center_x(grid_cell(37)), center_y(grid_cell(37))).value == 37 &&
       channels_touch(center_x(grid_cell(37)), center_y(grid_cell(37))).kind ==
           ActionKind::lockout_toggle &&
       channels_touch(center_x(kLockSsb), center_y(kLockSsb)).kind ==
           ActionKind::lockout_ssb_only;
  ok = ok && setup_touch(center_x(setup_rect(kSetupMinus, 3)),
                         center_y(setup_rect(kSetupMinus, 3))).kind ==
                 ActionKind::threshold_down &&
       setup_touch(center_x(setup_rect(kSetupPlus, 4)), center_y(setup_rect(kSetupPlus, 4)))
               .kind == ActionKind::hang_up &&
       setup_touch(center_x(setup_rect(kSetupValue, 6)), center_y(setup_rect(kSetupValue, 6)))
               .kind == ActionKind::priority_cycle &&
       setup_touch(center_x(setup_rect(kSetupMinus, 6)), center_y(setup_rect(kSetupMinus, 6)))
               .kind == ActionKind::none;
  int over = 0;
  ok = ok && s_units(-36.0f, &over) == 9 && over == 0 && s_units(-26.0f, &over) == 9 &&
       over == 10 && s_units(-42.0f, nullptr) == 8 && s_units(-120.0f, nullptr) == 0;

  g_snapshot = saved;
  g_active = was_active;
  g_tab = saved_tab;
  g_log_page = saved_page;
  return ok && Scanner::self_check();
}

}  // namespace orcsdr::cb
