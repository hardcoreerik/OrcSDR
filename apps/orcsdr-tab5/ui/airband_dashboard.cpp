#include "focus_nav.hpp"
#include "airband_dashboard.hpp"

#include "dashboard_audio_control.hpp"
#include "spectrum_resample.hpp"
#include "waterfall_style.hpp"

#include <M5Unified.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace orcsdr::airband {
namespace {

constexpr uint16_t kPanel = 0x0841;
constexpr uint16_t kGrid = 0x2945;
constexpr uint16_t kCyan = 0x2e7f;
constexpr uint16_t kGreen = 0x6fe8;
constexpr uint16_t kYellow = 0xff24;
constexpr uint16_t kAmber = 0xfd20;
constexpr uint16_t kMuted = 0x8c71;
constexpr uint16_t kSelected = 0x1264;

struct Rect { int x; int y; int w; int h; };

constexpr int kTabsY = 630;
constexpr int kTabCount = 6;
constexpr int kTabW = 1280 / kTabCount;

// SCOPE tab: spectrum on top, frequency axis, waterfall below, status line at the bottom.
constexpr int kScopeX = 24, kScopeW = 1232;
constexpr int kScopeSpecY = 168, kScopeSpecH = 190;
constexpr int kScopeAxisY = 358, kScopeAxisH = 20;
constexpr int kScopeWfY = 380, kScopeWfH = 170;
// Control row under the waterfall: span, gain and tuner AGC, like the FM spectrum view's controls.
constexpr Rect kScopeSpanDown{24, 560, 70, 56};
constexpr Rect kScopeSpanUp{300, 560, 70, 56};
constexpr Rect kScopeGainDown{404, 560, 70, 56};
constexpr Rect kScopeGainUp{690, 560, 70, 56};
constexpr Rect kScopeAgc{790, 560, 230, 56};
constexpr Rect kScopePaletteChip{900, 100, 170, 52};
constexpr Rect kScopeSpeedChip{1086, 100, 170, 52};
constexpr float kScopeMinRangeDb = 24.0f;
constexpr float kScopeHeadroomDb = 5.0f;
constexpr float kScopeWaterfallRangeDb = 18.0f;

constexpr Rect kNowCard{24, 108, 520, 286};
constexpr Rect kControlsCard{556, 108, 700, 286};
constexpr Rect kStatusCard{24, 404, 1232, 112};
constexpr Rect kGainCard{24, 524, 1232, 96};
constexpr Rect kGainAuto{44, 540, 270, 64};
constexpr Rect kGainDown{330, 540, 120, 64};
constexpr Rect kGainValue{460, 540, 230, 64};
constexpr Rect kGainUp{700, 540, 120, 64};
constexpr Rect kRtlAgc{850, 540, 250, 64};
constexpr int kControlX = 574;
constexpr int kControlY = 126;
constexpr int kControlW = 208;
constexpr int kControlH = 70;
constexpr int kControlGapX = 12;
constexpr int kControlGapY = 12;

constexpr int kListX = 24;
constexpr int kListY = 194;
constexpr int kListW = 1232;
constexpr int kListRowH = 56;
constexpr int kListRows = 7;

constexpr int kSetupY = 112;
constexpr int kSetupRowH = 70;
constexpr int kSetupRows = 7;
constexpr Rect kMinusBase{780, 8, 104, 50};
constexpr Rect kValueBase{896, 8, 232, 50};
constexpr Rect kPlusBase{1140, 8, 104, 50};

EXT_RAM_BSS_ATTR static Snapshot g_snapshot{};
bool g_active = false;
Tab g_tab = Tab::listen;
EXT_RAM_BSS_ATTR uint16_t g_scope_row[kScopeW]{};
EXT_RAM_BSS_ATTR float g_scope_levels[kScopeW]{};
uint32_t g_scope_span_hz = 0;
void (*g_scope_span_hook)(uint32_t hz) = nullptr;
constexpr uint32_t kScopeDefaultSpanHz = 480000;
bool g_scope_levels_valid = false;
constexpr float kScopeSmoothing = 0.45f;   // weight of the new frame (the FM trace uses 0.22)

// Entering the scope narrows the shared span; leaving it restores the previous one.
void set_tab(Tab next) {
  if (g_tab == Tab::scope && next != Tab::scope) {
    M5.Display.clearScrollRect();
    if (g_scope_span_hook) g_scope_span_hook(0);
  } else if (g_tab != Tab::scope && next == Tab::scope) {
    g_scope_span_hz = 0;
    g_scope_levels_valid = false;
    if (g_scope_span_hook) g_scope_span_hook(kScopeDefaultSpanHz);
  }
  g_tab = next;
}
uint32_t g_scope_fps = 0, g_scope_frames = 0, g_scope_window_ms = 0, g_scope_draw_ms = 0;
float g_scope_ceiling = 0.0f;
bool g_scope_ceiling_valid = false;
uint32_t g_last_activity_redraw_ms = 0;
// What the level meter currently shows, so a slow drift still triggers a repaint.
float g_meter_snr_db = 0.0f;
bool g_meter_open = false;

bool hit(int32_t x, int32_t y, const Rect& r) {
  return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

int cx(const Rect& r) { return r.x + r.w / 2; }
int cy(const Rect& r) { return r.y + r.h / 2; }

void text(const char* value, int x, int y, uint16_t color = TFT_WHITE, int size = 2,
          textdatum_t datum = middle_center) {
  M5.Display.setTextDatum(datum);
  M5.Display.setTextSize(size);
  M5.Display.setTextColor(color);
  M5.Display.drawString(value, x, y);
}

void card(const Rect& r, uint16_t border = kCyan) {
  M5.Display.fillRoundRect(r.x, r.y, r.w, r.h, 10, kPanel);
  M5.Display.drawRoundRect(r.x, r.y, r.w, r.h, 10, border);
}

void button(const Rect& r, const char* label, bool selected = false,
            bool enabled = true, int size = 2) {
  if (enabled) focus_nav::note(r.x, r.y, r.w, r.h);   // keyboard focus stop
  const uint16_t border = enabled ? (selected ? kGreen : kCyan) : TFT_DARKGREY;
  M5.Display.fillRoundRect(r.x, r.y, r.w, r.h, 8, selected ? kSelected : kPanel);
  M5.Display.drawRoundRect(r.x, r.y, r.w, r.h, 8, border);
  text(label, cx(r), cy(r), enabled ? TFT_WHITE : kMuted, size);
}

Rect control(int col, int row) {
  return {kControlX + col * (kControlW + kControlGapX),
          kControlY + row * (kControlH + kControlGapY),
          kControlW, kControlH};
}

Rect list_row(int row) {
  return {kListX, kListY + row * kListRowH, kListW, kListRowH - 7};
}

Rect setup_rect(const Rect& base, int row) {
  return {base.x, kSetupY + row * kSetupRowH + base.y, base.w, base.h};
}

double mhz(uint32_t hz) { return static_cast<double>(hz) / 1000000.0; }

uint16_t state_color() {
  switch (g_snapshot.scan_state) {
    case ScanState::receiving: return kGreen;
    case ScanState::settling: return kYellow;
    case ScanState::hang: return kAmber;
    case ScanState::held: return kAmber;
    case ScanState::scanning: return kCyan;
    case ScanState::off: return kMuted;
  }
  return kMuted;
}

void draw_header() {
  audio_header::draw_brand("AIRBAND");
  M5.Display.drawFastVLine(350, 18, 58, kCyan);
  text("AIRBAND RX", 390, 42, TFT_WHITE, 4, middle_left);
  text("118.000 - 136.975 MHz  AM", 390, 70, kMuted, 1, middle_left);
  audio_header::draw_battery(g_snapshot.battery_percent);
  audio_header::draw_home_button();
  audio_header::draw_mute_button(g_snapshot.sound_enabled);
  audio_header::draw_visualizer_button(g_snapshot.running);
  audio_header::draw_settings_button();
  M5.Display.drawFastHLine(20, 92, 1240, kGreen);
}

void draw_tabs() {
  static constexpr const char* labels[kTabCount] = {
      "LISTEN", "SCAN", "AIRPORTS", "ACTIVITY", "SETUP", "SCOPE"};
  for (int i = 0; i < kTabCount; ++i)
    button({i * kTabW + 4, kTabsY + 4, kTabW - 8, 82}, labels[i],
           static_cast<int>(g_tab) == i);
}

void draw_scope_chips() {
  using waterfall_style::Screen;
  const auto chip = [](const Rect& r, const char* title, const char* value) {
    button(r, "", false, true, 1);
    text(title, cx(r), r.y + 15, kCyan, 1);
    text(value, cx(r), r.y + 36, kGreen, 2);
  };
  chip(kScopePaletteChip, "PALETTE",
       waterfall_style::palette_name(waterfall_style::palette(Screen::airband)));
  chip(kScopeSpeedChip, "SPEED", waterfall_style::speed_name(waterfall_style::speed(Screen::airband)));
}

void format_mhz(char* out, size_t size, uint32_t hz) {
  std::snprintf(out, size, "%.3f", static_cast<double>(hz) / 1e6);
}

void draw_scope_axis() {
  M5.Display.fillRect(kScopeX, kScopeAxisY, kScopeW, kScopeAxisH, TFT_BLACK);
  if (g_scope_span_hz == 0) return;
  const uint32_t half = g_scope_span_hz / 2u;
  const uint32_t centre = g_snapshot.frequency_hz;
  char low[16], mid[16], high[16];
  format_mhz(low, sizeof(low), centre > half ? centre - half : 0u);
  format_mhz(mid, sizeof(mid), centre);
  format_mhz(high, sizeof(high), centre + half);
  const int y = kScopeAxisY + kScopeAxisH / 2;
  text(low, kScopeX + 4, y, kMuted, 1, middle_left);
  text(mid, kScopeX + kScopeW / 2, y, kCyan, 1, middle_center);
  text(high, kScopeX + kScopeW - 4, y, kMuted, 1, middle_right);
}

void draw_scope_status() {
  M5.Display.fillRect(kScopeX, 140, 860, 22, TFT_BLACK);
  char value[96];
  std::snprintf(value, sizeof(value), "SNR %.0f dB   SQL +%d dB   %s   tap the scope to tune",
                static_cast<double>(g_snapshot.snr_db), static_cast<int>(g_snapshot.scan.squelch_db),
                g_snapshot.squelch_open ? "OPEN" : "CLOSED");
  text(value, 34, 152, g_snapshot.squelch_open ? kGreen : kMuted, 1, middle_left);
}

void draw_scope_controls() {
  using receiver_controls::Availability;
  using receiver_controls::Control;
  M5.Display.fillRect(kScopeX, 556, kScopeW, 64, TFT_BLACK);
  char value[40];
  button(kScopeSpanDown, "-", false, true, 3);
  button(kScopeSpanUp, "+", false, true, 3);
  text("SPAN", 112, 575, kCyan, 1, middle_left);
  if (g_scope_span_hz)
    std::snprintf(value, sizeof(value), "%lu kHz", static_cast<unsigned long>(g_scope_span_hz / 1000u));
  else
    std::snprintf(value, sizeof(value), "--");
  text(value, 197, 598, kGreen, 2);

  const auto agc = receiver_controls::item(Control::tuner_agc, g_snapshot.controls);
  const auto gain = receiver_controls::item(Control::rf_gain, g_snapshot.controls);
  const bool gain_ok = gain.availability == Availability::enabled;
  const bool agc_ok = agc.availability == Availability::enabled;
  button(kScopeGainDown, "-", false, gain_ok, 3);
  button(kScopeGainUp, "+", false, gain_ok, 3);
  text("GAIN", 482, 575, kCyan, 1, middle_left);
  if (!gain_ok)
    std::snprintf(value, sizeof(value), "N/A");
  else if (agc_ok && agc.active)
    std::snprintf(value, sizeof(value), "AUTO");
  else
    std::snprintf(value, sizeof(value), "%.1f dB", static_cast<double>(gain.value) / 10.0);
  text(value, 580, 598, gain_ok ? kGreen : kMuted, 2);
  button(kScopeAgc, !agc_ok ? "TUNER AGC N/A" : agc.active ? "TUNER AGC ON" : "TUNER AGC OFF",
         agc_ok && agc.active, agc_ok, 2);
}

// The tuned frequency is the scope's heading; it must follow every retune (tap, scanner, keys).
void draw_scope_title() {
  M5.Display.fillRect(kScopeX, 100, 860, 36, TFT_BLACK);
  char value[48];
  std::snprintf(value, sizeof(value), "%.3f MHz  AM", static_cast<double>(g_snapshot.frequency_hz) / 1e6);
  text(value, 32, 118, TFT_WHITE, 3, middle_left);
}

void draw_scope() {
  M5.Display.fillRect(0, 93, 1280, kTabsY - 93, TFT_BLACK);
  draw_scope_title();
  draw_scope_chips();
  M5.Display.drawRect(kScopeX, kScopeSpecY, kScopeW, kScopeSpecH, kCyan);
  M5.Display.drawRect(kScopeX, kScopeWfY, kScopeW, kScopeWfH, kCyan);
  g_scope_ceiling_valid = false;
  draw_scope_axis();
  draw_scope_status();
  draw_scope_controls();
  M5.Display.setScrollRect(kScopeX + 1, kScopeWfY + 1, kScopeW - 2, kScopeWfH - 2, TFT_BLACK);
}

void page_title(const char* title, const char* subtitle) {
  M5.Display.fillRect(0, 93, 1280, kTabsY - 93, TFT_BLACK);
  text(title, 32, 126, TFT_WHITE, 3, middle_left);
  text(subtitle, 34, 160, kMuted, 2, middle_left);
}

constexpr Rect kMeterArea{36, 296, 496, 62};

void draw_signal_meter() {
  g_meter_snr_db = g_snapshot.snr_db;
  g_meter_open = g_snapshot.squelch_open;
  M5.Display.fillRect(kMeterArea.x, kMeterArea.y, kMeterArea.w, kMeterArea.h, kPanel);
  const Rect meter{48, 304, 468, 24};
  M5.Display.drawRoundRect(meter.x, meter.y, meter.w, meter.h, 6, kCyan);
  const float level = std::clamp(g_snapshot.snr_db / 30.0f, 0.0f, 1.0f);
  const int filled = static_cast<int>((meter.w - 4) * level);
  if (filled > 0)
    M5.Display.fillRect(meter.x + 2, meter.y + 3, filled, meter.h - 6,
                        g_snapshot.squelch_open ? kGreen : kYellow);
  char value[64];
  const int threshold_x = meter.x + 2 +
      static_cast<int>((meter.w - 4) * std::clamp(
          static_cast<float>(g_snapshot.scan.squelch_db) / 30.0f, 0.0f, 1.0f));
  M5.Display.drawFastVLine(threshold_x, meter.y - 4, meter.h + 8, kAmber);
  std::snprintf(value, sizeof(value), "SNR %.0f dB   SQL +%d dB   %s",
                static_cast<double>(g_snapshot.snr_db),
                static_cast<int>(g_snapshot.scan.squelch_db),
                g_snapshot.squelch_open ? "OPEN" : "CLOSED");
  text(value, meter.x, meter.y + 42,
       g_snapshot.squelch_open ? kGreen : kMuted, 2, middle_left);
}

void draw_gain() {
  using receiver_controls::Availability;
  using receiver_controls::Control;
  card(kGainCard);
  const auto agc = receiver_controls::item(Control::tuner_agc, g_snapshot.controls);
  const auto gain = receiver_controls::item(Control::rf_gain, g_snapshot.controls);
  const auto rtl = receiver_controls::item(Control::rtl_agc, g_snapshot.controls);
  const bool agc_ok = agc.availability == Availability::enabled;
  const bool gain_ok = gain.availability == Availability::enabled;
  const bool rtl_ok = rtl.availability == Availability::enabled;
  button(kGainAuto, !agc_ok ? "TUNER AGC N/A" : agc.active ? "TUNER AGC ON" : "TUNER AGC OFF",
         agc_ok && agc.active, agc_ok);
  button(kGainDown, "GAIN -", false, gain_ok);
  char value[32];
  if (!gain_ok)
    std::snprintf(value, sizeof(value), "RF GAIN N/A");
  else if (agc_ok && agc.active)
    std::snprintf(value, sizeof(value), "GAIN AUTO");
  else
    std::snprintf(value, sizeof(value), "%.1f dB", static_cast<double>(gain.value) / 10.0);
  M5.Display.drawRoundRect(kGainValue.x, kGainValue.y, kGainValue.w, kGainValue.h, 8, kMuted);
  text(value, cx(kGainValue), cy(kGainValue), gain_ok ? TFT_WHITE : kMuted, 2);
  button(kGainUp, "GAIN +", false, gain_ok);
  button(kRtlAgc, !rtl_ok ? "RTL AGC N/A" : rtl.active ? "RTL AGC ON" : "RTL AGC OFF",
         rtl_ok && rtl.active, rtl_ok);
  text("Shared receiver gain", 1120, 560, kMuted, 1, middle_left);
  text("(same driver controls", 1120, 578, kMuted, 1, middle_left);
  text("as AM / Shortwave)", 1120, 596, kMuted, 1, middle_left);
}

void draw_now_card() {
  card(kNowCard, state_color());
  char value[80];
  std::snprintf(value, sizeof(value), "%.3f", mhz(g_snapshot.frequency_hz));
  text(value, 48, 166, TFT_WHITE, 7, middle_left);
  text("MHz", 48, 222, kCyan, 3, middle_left);
  text(g_snapshot.current_label[0] ? g_snapshot.current_label : "AIRBAND",
       48, 258, kMuted, 2, middle_left);
  std::snprintf(value, sizeof(value), "%s   AM",
                state_name(g_snapshot.scan_state));
  text(value, 516, 255, state_color(), 2, middle_right);
  draw_signal_meter();
}

void draw_controls() {
  card(kControlsCard);
  const bool scanning = g_snapshot.scan_state != ScanState::off;
  const bool held = g_snapshot.scan_state == ScanState::held;
  button(control(0, 0), scanning ? "STOP SCAN" : "START SCAN", scanning);
  button(control(1, 0), held ? "RESUME" : "HOLD", held, scanning);
  button(control(2, 0), "SKIP", false, scanning);
  button(control(0, 1), "FREQ -");
  button(control(1, 1), "121.500 GUARD", g_snapshot.frequency_hz == kGuardFrequencyHz);
  button(control(2, 1), "FREQ +");
  button(control(0, 2), source_name(g_snapshot.scan.source),
         g_snapshot.scan.source == ScanSource::airport_bank);
  button(control(1, 2), spacing_name(g_snapshot.scan.spacing));
  button(control(2, 2), g_snapshot.catalog_loaded ? "AVIATION DATA READY" : "NO AVIATION DATA",
         g_snapshot.catalog_loaded, true, 2);
}

void draw_status() {
  card(kStatusCard);
  char value[112];
  std::snprintf(value, sizeof(value),
                "%lu stops   %lu checked   bank %u   guard %s   SQL +%d dB   hang %.1fs",
                static_cast<unsigned long>(g_snapshot.stops),
                static_cast<unsigned long>(g_snapshot.channels_checked),
                static_cast<unsigned>(g_snapshot.bank_count),
                g_snapshot.scan.priority_guard ? "ON" : "OFF",
                static_cast<int>(g_snapshot.scan.squelch_db),
                static_cast<double>(g_snapshot.scan.hang_ms) / 1000.0);
  text(value, 44, 434, TFT_WHITE, 2, middle_left);
  if (!g_snapshot.location_configured)
    text("Receiver location not set: airport labels and airport-bank scanning are off.",
         44, 474, kAmber, 2, middle_left);
  else if (!g_snapshot.catalog_loaded)
    text("Aviation data not loaded: manual tuning and full-band scan still work.",
         44, 474, kAmber, 2, middle_left);
  else
    text("Airport bank uses the closest catalog entries (database, not RF-decoded).",
         44, 474, kGreen, 2, middle_left);
}

void draw_listen() {
  M5.Display.fillRect(0, 93, 1280, kTabsY - 93, TFT_BLACK);
  draw_now_card();
  draw_controls();
  draw_status();
  draw_gain();
}

void draw_scan_status() {
  char value[96];
  card({24, 178, 1232, 104}, state_color());
  std::snprintf(value, sizeof(value), "%s   target %.3f MHz",
                state_name(g_snapshot.scan_state),
                mhz(g_snapshot.frequency_hz));
  text(value, 44, 208, state_color(), 3, middle_left);
  std::snprintf(value, sizeof(value), "%s   %s   SQL +%d dB",
                source_name(g_snapshot.scan.source), spacing_name(g_snapshot.scan.spacing),
                static_cast<int>(g_snapshot.scan.squelch_db));
  text(value, 44, 252, kMuted, 2, middle_left);
  button({958, 194, 278, 66},
         g_snapshot.scan_state == ScanState::off ? "START SCAN" : "STOP SCAN",
         g_snapshot.scan_state != ScanState::off);
}

void draw_scan_bank() {
  char value[96];
  M5.Display.fillRect(0, 300, 1280, kTabsY - 300, TFT_BLACK);
  text("SCAN BANK", 30, 308, kCyan, 2, middle_left);
  const size_t rows = std::min<size_t>(5, g_snapshot.bank_count);
  for (size_t i = 0; i < rows; ++i) {
    const Rect r{24, 330 + static_cast<int>(i) * 54, 1232, 48};
    const auto& entry = g_snapshot.bank[i];
    std::snprintf(value, sizeof(value), "%2u   %9.3f MHz   %-40.40s",
                  static_cast<unsigned>(i + 1), mhz(entry.frequency_hz), entry.label);
    button(r, value, entry.frequency_hz == g_snapshot.frequency_hz, true, 2);
  }
  if (rows == 0)
    text("No airport bank loaded. Switch to FULL BAND or reload the FAA data pack.",
         640, 430, kAmber, 2);
}

void draw_scan() {
  page_title("SMART SCAN", "Fast memory-bank scan first; full-band scan remains available.");
  draw_scan_status();
  draw_scan_bank();
}

void draw_airports() {
  page_title("AIRPORT / ATC CHANNELS",
             "Offline aviation catalog. Nearby rows are database context, not RF-decoded identity.");
  button({990, 114, 266, 58},
         g_snapshot.location_configured ? "RELOAD DATA" : "SET LOCATION");
  char radius_label[32];
  if (g_snapshot.scan.radius_nm == 0)
    std::snprintf(radius_label, sizeof(radius_label), "RADIUS: ANY");
  else
    std::snprintf(radius_label, sizeof(radius_label), "RADIUS: %u nm",
                  static_cast<unsigned>(g_snapshot.scan.radius_nm));
  button({700, 114, 274, 58}, radius_label, false, g_snapshot.location_configured);
  char value[128];
  if (!g_snapshot.location_configured) {
    card({24, 218, 1232, 270});
    text("RECEIVER LOCATION NOT SET", 640, 286, kAmber, 3);
    text("Set one OrcSDR receiver location for Airband, ADS-B, and OrcMaps.",
         640, 342, TFT_WHITE, 2);
    text("Airport-bank scanning and station labels stay disabled until location is set.",
         640, 386, kMuted, 2);
    text("Manual tuning, 121.500 Guard, and full-band scanning still work.",
         640, 430, kGreen, 2);
    return;
  }
  if (!g_snapshot.catalog_loaded || g_snapshot.catalog_count == 0) {
    card({24, 218, 1232, 270});
    switch (g_snapshot.load_result) {
      case LoadResult::no_matching_entries:
        text("NO AVIATION DATA NEAR THIS LOCATION", 640, 294, kAmber, 3);
        text("Nothing in the catalog is inside the radius shown above.", 640, 350, TFT_WHITE, 2);
        text("Increase the radius, or use manual tuning / full-band scan.", 640, 396, kMuted, 2);
        break;
      case LoadResult::bad_header:
      case LoadResult::unsupported_rows:
        text("AVIATION DATA FORMAT NOT SUPPORTED", 640, 294, kAmber, 3);
        text("The installed file is not an ORCAIR2 airport catalog.", 640, 350, TFT_WHITE, 2);
        text("Install /orcsdr/data/aviation.idx (see docs/airband/README.md).", 640, 396, kMuted, 2);
        break;
      default:
        text("AVIATION DATA NOT INSTALLED", 640, 294, kAmber, 3);
        text("Expected file: /orcsdr/data/aviation.idx (ORCAIR2).", 640, 350, TFT_WHITE, 2);
        text("Manual tuning and full-band scan remain available.", 640, 396, kMuted, 2);
        break;
    }
    return;
  }
  const size_t rows = std::min<size_t>(kListRows, g_snapshot.catalog_count);
  for (size_t i = 0; i < rows; ++i) {
    const CatalogEntry& entry = g_snapshot.catalog[i];
    std::snprintf(value, sizeof(value),
                  "%9.3f MHz   %-9s   %5.1f nm   %-9s   %-34.34s",
                  mhz(entry.frequency_hz), service_name(entry.service),
                  static_cast<double>(entry.distance_nm),
                  source_class_name(entry.source_class), entry.label);
    button(list_row(static_cast<int>(i)), value,
           entry.frequency_hz == g_snapshot.frequency_hz, true, 2);
  }
}

void format_age(uint32_t now_ms, const Activity& item, char* out, size_t size) {
  const uint32_t ended = item.start_ms + item.duration_ms;
  const uint32_t seconds = (now_ms - ended) / 1000u;
  if (seconds < 60u)
    std::snprintf(out, size, "%lus ago", static_cast<unsigned long>(seconds));
  else
    std::snprintf(out, size, "%lum ago", static_cast<unsigned long>(seconds / 60u));
}

void draw_activity() {
  page_title("ACTIVITY", "Bounded local log of transmissions that opened the airband scanner.");
  button({1010, 114, 246, 58}, "CLEAR LOG", false, g_snapshot.activity_count != 0);
  if (g_snapshot.activity_count == 0) {
    card({24, 226, 1232, 250});
    text("NO AIRBAND ACTIVITY LOGGED YET", 640, 310, TFT_WHITE, 3);
    text("Start scanning. Completed transmissions appear here with frequency, length,",
         640, 364, kMuted, 2);
    text("peak receiver level, and the catalog label when one is known.",
         640, 400, kMuted, 2);
    return;
  }
  char value[128];
  const size_t rows = std::min<size_t>(kListRows, g_snapshot.activity_count);
  for (size_t i = 0; i < rows; ++i) {
    const Activity& item = g_snapshot.activity[i];
    char age[20];
    format_age(g_snapshot.now_ms, item, age, sizeof(age));
    std::snprintf(value, sizeof(value),
                  "%9.3f MHz   %5.1fs   +%2.0f dB   %-34.34s   %s",
                  mhz(item.frequency_hz),
                  static_cast<double>(item.duration_ms) / 1000.0,
                  static_cast<double>(item.peak_snr_db), item.label, age);
    button(list_row(static_cast<int>(i)), value,
           item.frequency_hz == g_snapshot.frequency_hz, true, 2);
  }
}

void setup_value(int row, char* out, size_t size) {
  switch (row) {
    case 0: std::snprintf(out, size, "%s", spacing_name(g_snapshot.scan.spacing)); break;
    case 1: std::snprintf(out, size, "%s", source_name(g_snapshot.scan.source)); break;
    case 2:
      if (g_snapshot.scan.squelch_db == 0)
        std::snprintf(out, size, "OPEN");
      else
        std::snprintf(out, size, "+%d dB", static_cast<int>(g_snapshot.scan.squelch_db));
      break;
    case 3: std::snprintf(out, size, "%u ms", static_cast<unsigned>(g_snapshot.scan.settle_ms)); break;
    case 4:
      std::snprintf(out, size, "%.1f s", static_cast<double>(g_snapshot.scan.hang_ms) / 1000.0);
      break;
    case 5:
      std::snprintf(out, size, "%s", g_snapshot.scan.priority_guard ? "ON" : "OFF");
      break;
    case 6:
      std::snprintf(out, size, "%s", g_snapshot.catalog_loaded ? "LOADED" : "MISSING");
      break;
    default: out[0] = '\0'; break;
  }
}

void draw_setup() {
  M5.Display.fillRect(0, 93, 1280, kTabsY - 93, TFT_BLACK);
  static constexpr const char* labels[kSetupRows] = {
      "CHANNEL SPACING", "SCAN SOURCE", "AUDIO / SCAN SQUELCH", "SCAN SETTLE",
      "REPLY HANG", "121.500 PRIORITY", "OFFLINE AVIATION DATA"};
  static constexpr const char* help[kSetupRows] = {
      "25 kHz or 8.33 kHz tuning plan",
      "Closest airport frequencies or the complete civil voice band",
      "Open audio and stop scan when the carrier is this far above the noise floor",
      "Time allowed after each tuner move before evaluating activity",
      "Wait this long for a reply before resuming the scan",
      "Periodically check the civil emergency / guard frequency",
      "Global/local catalog; database context is never RF identity"};

  for (int row = 0; row < kSetupRows; ++row) {
    const int y = kSetupY + row * kSetupRowH;
    M5.Display.fillRoundRect(24, y, 1232, kSetupRowH - 7, 8, kPanel);
    text(labels[row], 42, y + 20, TFT_WHITE, 3, middle_left);
    text(help[row], 42, y + 47, kMuted, 1, middle_left);
    char value[32];
    setup_value(row, value, sizeof(value));
    const bool cycle = row == 0 || row == 1 || row == 3 || row == 5 || row == 6;
    if (!cycle) button(setup_rect(kMinusBase, row), "-");
    button(setup_rect(kValueBase, row), row == 6 && !g_snapshot.catalog_loaded
                                           ? "RELOAD DATA" : value,
           row == 5 && g_snapshot.scan.priority_guard);
    if (!cycle) button(setup_rect(kPlusBase, row), "+");
  }
}

void draw_page() {
  switch (g_tab) {
    case Tab::listen: draw_listen(); break;
    case Tab::scan: draw_scan(); break;
    case Tab::airports: draw_airports(); break;
    case Tab::activity: draw_activity(); break;
    case Tab::setup: draw_setup(); break;
    case Tab::scope: draw_scope(); break;
  }
}

Action tab_touch(int32_t x) {
  set_tab(static_cast<Tab>(std::clamp<int32_t>(x / kTabW, 0, kTabCount - 1)));
  draw_tabs();
  draw_page();
  return {};
}

Action scope_touch(int32_t x, int32_t y) {
  using waterfall_style::Screen;
  if (hit(x, y, kScopeSpanDown)) return {ActionKind::span_down};
  if (hit(x, y, kScopeSpanUp)) return {ActionKind::span_up};
  if (hit(x, y, kScopeGainDown)) return {ActionKind::gain_down};
  if (hit(x, y, kScopeGainUp)) return {ActionKind::gain_up};
  if (hit(x, y, kScopeAgc)) return {ActionKind::tuner_agc_toggle};
  if (hit(x, y, kScopePaletteChip)) {
    waterfall_style::next_palette(Screen::airband);
    draw_scope_chips();
    return {};
  }
  if (hit(x, y, kScopeSpeedChip)) {
    waterfall_style::next_speed(Screen::airband);
    draw_scope_chips();
    return {};
  }
  if (g_scope_span_hz != 0 && x >= kScopeX && x < kScopeX + kScopeW && y >= kScopeSpecY &&
      y < kScopeWfY + kScopeWfH) {
    const int64_t offset = static_cast<int64_t>(x - (kScopeX + kScopeW / 2)) * g_scope_span_hz / kScopeW;
    const int64_t target = static_cast<int64_t>(g_snapshot.frequency_hz) + offset;
    if (target > 0) return {ActionKind::tune_to, static_cast<int32_t>(target)};
  }
  return {};
}

Action listen_touch(int32_t x, int32_t y) {
  if (hit(x, y, control(0, 0))) return {ActionKind::scan_toggle};
  if (hit(x, y, control(1, 0)))
    return g_snapshot.scan_state == ScanState::off ? Action{}
                                                   : Action{ActionKind::hold_toggle};
  if (hit(x, y, control(2, 0)))
    return g_snapshot.scan_state == ScanState::off ? Action{} : Action{ActionKind::skip};
  if (hit(x, y, control(0, 1))) return {ActionKind::tune_down};
  if (hit(x, y, control(1, 1))) return {ActionKind::tune_guard};
  if (hit(x, y, control(2, 1))) return {ActionKind::tune_up};
  if (hit(x, y, control(0, 2))) return {ActionKind::source_cycle};
  if (hit(x, y, control(1, 2))) return {ActionKind::spacing_cycle};
  if (hit(x, y, control(2, 2))) return {ActionKind::reload_catalog};
  using receiver_controls::Availability;
  using receiver_controls::Control;
  const bool gain_ok = receiver_controls::item(Control::rf_gain, g_snapshot.controls)
                           .availability == Availability::enabled;
  if (hit(x, y, kGainDown) && gain_ok) return {ActionKind::gain_down};
  if (hit(x, y, kGainUp) && gain_ok) return {ActionKind::gain_up};
  if (hit(x, y, kGainAuto) &&
      receiver_controls::item(Control::tuner_agc, g_snapshot.controls).availability ==
          Availability::enabled)
    return {ActionKind::tuner_agc_toggle};
  if (hit(x, y, kRtlAgc) &&
      receiver_controls::item(Control::rtl_agc, g_snapshot.controls).availability ==
          Availability::enabled)
    return {ActionKind::rtl_agc_toggle};
  return {};
}

Action scan_touch(int32_t x, int32_t y) {
  if (hit(x, y, {958, 194, 278, 66})) return {ActionKind::scan_toggle};
  const size_t rows = std::min<size_t>(5, g_snapshot.bank_count);
  for (size_t i = 0; i < rows; ++i) {
    const Rect r{24, 330 + static_cast<int>(i) * 54, 1232, 48};
    if (hit(x, y, r)) return {ActionKind::tune_catalog, static_cast<int32_t>(i)};
  }
  return {};
}

Action airports_touch(int32_t x, int32_t y) {
  if (hit(x, y, {700, 114, 274, 58}) && g_snapshot.location_configured)
    return {ActionKind::radius_cycle};
  if (hit(x, y, {990, 114, 266, 58}))
    return g_snapshot.location_configured ? Action{ActionKind::reload_catalog}
                                          : Action{ActionKind::open_location_settings};
  const size_t rows = std::min<size_t>(kListRows, g_snapshot.catalog_count);
  for (size_t i = 0; i < rows; ++i)
    if (hit(x, y, list_row(static_cast<int>(i))))
      return {ActionKind::tune_catalog, static_cast<int32_t>(i)};
  return {};
}

Action activity_touch(int32_t x, int32_t y) {
  if (hit(x, y, {1010, 114, 246, 58}) && g_snapshot.activity_count)
    return {ActionKind::clear_activity};
  const size_t rows = std::min<size_t>(kListRows, g_snapshot.activity_count);
  for (size_t i = 0; i < rows; ++i)
    if (hit(x, y, list_row(static_cast<int>(i))))
      return {ActionKind::tune_catalog,
              -static_cast<int32_t>(i) - 1};
  return {};
}

Action setup_touch(int32_t x, int32_t y) {
  for (int row = 0; row < kSetupRows; ++row) {
    if (hit(x, y, setup_rect(kValueBase, row))) {
      switch (row) {
        case 0: return {ActionKind::spacing_cycle};
        case 1: return {ActionKind::source_cycle};
        case 3: return {ActionKind::settle_cycle};
        case 5: return {ActionKind::priority_toggle};
        case 6: return {ActionKind::reload_catalog};
        default: break;
      }
    }
    if (hit(x, y, setup_rect(kMinusBase, row))) {
      if (row == 2) return {ActionKind::squelch_down};
      if (row == 4) return {ActionKind::hang_down};
    }
    if (hit(x, y, setup_rect(kPlusBase, row))) {
      if (row == 2) return {ActionKind::squelch_up};
      if (row == 4) return {ActionKind::hang_up};
    }
  }
  return {};
}

}  // namespace

void dashboard_enter(const Snapshot& snapshot) {
  g_snapshot = snapshot;
  g_active = true;
  set_tab(Tab::listen);
  dashboard_draw();
}

void dashboard_leave() {
  if (g_active) set_tab(Tab::listen);   // restores the shared span if the scope was showing
  g_active = false;
}

void dashboard_draw() {
  if (!g_active) return;
  M5.Display.fillScreen(TFT_BLACK);
  draw_header();
  draw_tabs();
  draw_page();
}

namespace {

uint32_t g_last_meter_ms = 0;
uint32_t g_last_status_ms = 0;

bool controls_differ(const receiver_controls::State& a, const receiver_controls::State& b) {
  return a.tuner_agc != b.tuner_agc || a.rtl_agc != b.rtl_agc ||
         a.gain_tenth_db != b.gain_tenth_db ||
         a.capabilities.rf_gain != b.capabilities.rf_gain ||
         a.capabilities.tuner_agc != b.capabilities.tuner_agc ||
         a.capabilities.rtl_agc != b.capabilities.rtl_agc;
}

}  // namespace

void dashboard_update(const Snapshot& snapshot) {
  if (!g_active) return;
  const Snapshot& old = g_snapshot;
  const bool header_changed =
      snapshot.sound_enabled != old.sound_enabled ||
      snapshot.battery_percent != old.battery_percent ||
      snapshot.running != old.running;
  const bool frequency_changed = snapshot.frequency_hz != old.frequency_hz;
  const bool state_changed = snapshot.scan_state != old.scan_state;
  const bool settings_changed =
      snapshot.scan.squelch_db != old.scan.squelch_db ||
      snapshot.scan.spacing != old.scan.spacing ||
      snapshot.scan.source != old.scan.source ||
      snapshot.scan.hang_ms != old.scan.hang_ms ||
      snapshot.scan.settle_ms != old.scan.settle_ms ||
      snapshot.scan.priority_guard != old.scan.priority_guard ||
      snapshot.scan.radius_nm != old.scan.radius_nm;
  const bool data_changed =
      snapshot.catalog_loaded != old.catalog_loaded ||
      snapshot.catalog_count != old.catalog_count ||
      snapshot.load_result != old.load_result ||
      snapshot.location_configured != old.location_configured ||
      snapshot.bank_count != old.bank_count ||
      snapshot.activity_count != old.activity_count;
  const bool label_changed =
      std::strncmp(snapshot.current_label, old.current_label, sizeof(snapshot.current_label)) != 0;
  const bool gain_changed = controls_differ(snapshot.controls, old.controls);
  const bool live_changed = snapshot.squelch_open != g_meter_open ||
                            std::fabs(snapshot.snr_db - g_meter_snr_db) >= 2.0f;
  const bool counters_changed = snapshot.stops != old.stops ||
                                snapshot.channels_checked != old.channels_checked;
  const uint32_t now = snapshot.now_ms;
  const uint32_t old_stops = old.stops;
  g_snapshot = snapshot;

  M5.Display.startWrite();
  if (header_changed) draw_header();
  switch (g_tab) {
    case Tab::listen:
      // Repaint only the regions whose data changed. The level meter alone is throttled
      // because it moves constantly on a noisy channel.
      if (frequency_changed || state_changed || label_changed) {
        draw_now_card();
        g_last_meter_ms = now;
      } else if (live_changed && now - g_last_meter_ms >= 150u) {
        draw_signal_meter();
        g_last_meter_ms = now;
      }
      if (frequency_changed || state_changed || settings_changed || data_changed)
        draw_controls();
      if (settings_changed || data_changed || snapshot.stops != old_stops ||
          (counters_changed && now - g_last_status_ms >= 1000u)) {
        draw_status();
        g_last_status_ms = now;
      }
      if (gain_changed) draw_gain();
      break;
    case Tab::scan:
      if (frequency_changed || state_changed || settings_changed) draw_scan_status();
      if (frequency_changed || data_changed) draw_scan_bank();
      break;
    case Tab::activity:
      if (data_changed || now - g_last_activity_redraw_ms >= 5000u) {
        draw_page();
        g_last_activity_redraw_ms = now;
      }
      break;
    case Tab::airports:
    case Tab::setup:
      // Static pages repaint only when what they show changes, never for the live level.
      if (settings_changed || data_changed || frequency_changed) draw_page();
      break;
    case Tab::scope:
      if (frequency_changed) {
        g_scope_ceiling_valid = false;
        draw_scope_title();
        draw_scope_axis();
      }
      if (frequency_changed || (live_changed && now - g_last_meter_ms >= 250u) ||
          settings_changed) {
        draw_scope_status();
        g_last_meter_ms = now;
      }
      if (gain_changed) draw_scope_controls();
      break;
  }
  M5.Display.endWrite();
}

Action dashboard_handle_touch(int32_t x, int32_t y) {
  if (!g_active) return {};
  if (audio_header::home_hit(x, y)) return {ActionKind::exit_home};
  if (audio_header::settings_hit(x, y)) return {ActionKind::open_settings};
  if (y >= kTabsY) return tab_touch(x);
  switch (g_tab) {
    case Tab::listen: return listen_touch(x, y);
    case Tab::scan: return scan_touch(x, y);
    case Tab::airports: return airports_touch(x, y);
    case Tab::activity: return activity_touch(x, y);
    case Tab::setup: return setup_touch(x, y);
    case Tab::scope: return scope_touch(x, y);
  }
  return {};
}

void dashboard_select_tab(Tab tab) {
  if (!g_active) return;
  set_tab(tab);
  dashboard_draw();
}

bool dashboard_active() { return g_active; }
bool dashboard_spectrum_active() { return g_active && g_tab == Tab::scope; }
void dashboard_set_scope_span_hook(void (*hook)(uint32_t hz)) { g_scope_span_hook = hook; }

void dashboard_draw_spectrum(const float* levels, size_t first_bin, size_t visible_bins,
                             uint32_t span_hz) {
  if (!dashboard_spectrum_active() || levels == nullptr || visible_bins < 2 || span_hz == 0) return;
  using waterfall_style::Screen;
  constexpr int w = kScopeW - 2;
  const int x0 = kScopeX + 1;
  const uint32_t frame_started_ms = millis();
  if (span_hz != g_scope_span_hz) {
    g_scope_span_hz = span_hz;
    draw_scope_axis();
    draw_scope_status();
    draw_scope_controls();
  }
  float sum = 0.0f, strongest = -200.0f;
  for (int i = 0; i < w; ++i) {
    const float level = spectrum::peak_for_pixel(levels, first_bin, visible_bins,
                                                 static_cast<size_t>(i), static_cast<size_t>(w));
    // Light frame-to-frame smoothing so the trace is steady like the FM and Home scopes.
    g_scope_levels[i] = g_scope_levels_valid
                            ? kScopeSmoothing * level + (1.0f - kScopeSmoothing) * g_scope_levels[i]
                            : level;
    sum += g_scope_levels[i];
    strongest = std::max(strongest, g_scope_levels[i]);
  }
  g_scope_levels_valid = true;
  // Floor tracks the average (mostly noise); the ceiling follows the strongest bin quickly and falls
  // slowly so a transmission does not clip and the scale does not pump.
  const float floor_db = sum / static_cast<float>(w) - 3.0f;
  g_scope_ceiling = (!g_scope_ceiling_valid || strongest > g_scope_ceiling)
                        ? strongest
                        : g_scope_ceiling + (strongest - g_scope_ceiling) * 0.05f;
  g_scope_ceiling_valid = true;
  const float range_db = std::max(kScopeMinRangeDb, g_scope_ceiling + kScopeHeadroomDb - floor_db);

  M5.Display.startWrite();
  M5.Display.setClipRect(x0, kScopeSpecY + 1, w, kScopeSpecH - 2);
  M5.Display.fillRect(x0, kScopeSpecY + 1, w, kScopeSpecH - 2, TFT_BLACK);
  for (int i = 1; i < 5; ++i)
    M5.Display.drawFastHLine(kScopeX, kScopeSpecY + i * kScopeSpecH / 5, kScopeW, kGrid);
  for (int i = 1; i < 8; ++i)
    M5.Display.drawFastVLine(kScopeX + i * kScopeW / 8, kScopeSpecY, kScopeSpecH, kGrid);
  const int base = kScopeSpecY + kScopeSpecH - 4;
  const int height = kScopeSpecH - 8;
  int px = x0, py = base;
  for (int i = 0; i < w; ++i) {
    const float normalized = std::clamp((g_scope_levels[i] - floor_db) / range_db, 0.0f, 1.0f);
    const int x = x0 + i;
    const int y = base - static_cast<int>(normalized * height);
    if (i) M5.Display.drawLine(px, py, x, y, kGreen);
    px = x;
    py = y;
  }
  const int centre = kScopeX + kScopeW / 2;
  const int half_filter = std::clamp(
      static_cast<int>(static_cast<int64_t>(orcsdr::airband::filter_bandwidth_hz(g_snapshot.scan.spacing)) *
                       kScopeW / (2 * static_cast<int64_t>(span_hz))),
      2, kScopeW / 2 - 2);
  M5.Display.drawFastVLine(centre, kScopeSpecY, kScopeSpecH, kCyan);
  M5.Display.drawFastVLine(centre - half_filter, kScopeSpecY, kScopeSpecH, kYellow);
  M5.Display.drawFastVLine(centre + half_filter, kScopeSpecY, kScopeSpecH, kYellow);
  M5.Display.clearClipRect();

  const int rows = waterfall_style::rows_per_frame(Screen::airband);
  M5.Display.setScrollRect(x0, kScopeWfY + 1, w, kScopeWfH - 2, TFT_BLACK);
  M5.Display.scroll(0, -rows);
  for (int i = 0; i < w; ++i)
    g_scope_row[i] = waterfall_style::color565(
        Screen::airband,
        std::clamp((g_scope_levels[i] - floor_db) / kScopeWaterfallRangeDb, 0.0f, 1.0f));
  for (int row = 0; row < rows; ++row)
    M5.Display.pushImage(x0, kScopeWfY + kScopeWfH - 1 - rows + row, w, 1, g_scope_row);
  M5.Display.endWrite();
  g_scope_draw_ms = millis() - frame_started_ms;
  ++g_scope_frames;
  if (frame_started_ms - g_scope_window_ms >= 1000u) {
    g_scope_fps = g_scope_frames;
    g_scope_frames = 0;
    g_scope_window_ms = frame_started_ms;
  }
}
uint32_t dashboard_scope_fps() { return g_scope_fps; }
uint32_t dashboard_scope_draw_ms() { return g_scope_draw_ms; }
Tab dashboard_tab() { return g_tab; }

bool dashboard_self_check() {
  const bool was_active = g_active;
  const Tab saved_tab = g_tab;
  EXT_RAM_BSS_ATTR static Snapshot saved;
  saved = g_snapshot;
  g_active = true;
  reset_snapshot(g_snapshot);
  g_snapshot.scan_state = ScanState::scanning;
  g_snapshot.catalog_count = 1;
  g_snapshot.catalog[0].frequency_hz = 124900000u;
  g_snapshot.activity_count = 1;
  g_snapshot.activity[0].frequency_hz = 125800000u;

  bool ok = listen_touch(cx(control(0, 0)), cy(control(0, 0))).kind ==
                ActionKind::scan_toggle &&
            listen_touch(cx(control(1, 1)), cy(control(1, 1))).kind ==
                ActionKind::tune_guard &&
            airports_touch(cx(list_row(0)), cy(list_row(0))).value == 0 &&
            activity_touch(cx(list_row(0)), cy(list_row(0))).value == -1 &&
            setup_touch(cx(setup_rect(kValueBase, 0)),
                        cy(setup_rect(kValueBase, 0))).kind ==
                ActionKind::spacing_cycle &&
            listen_touch(cx(kGainUp), cy(kGainUp)).kind == ActionKind::none &&
            kGainCard.y + kGainCard.h <= kTabsY &&
            kStatusCard.y + kStatusCard.h <= kGainCard.y &&
            kTabsY + 90 <= 720 &&
            kStatusCard.y + kStatusCard.h <= kTabsY &&
            kScopeWfY + kScopeWfH <= 556 && kScopeAgc.y + kScopeAgc.h <= kTabsY && kScopeAxisY + kScopeAxisH <= kScopeWfY &&
            kTabW * kTabCount <= 1280;

  g_snapshot = saved;
  g_tab = saved_tab;
  g_active = was_active;
  return ok;
}

}  // namespace orcsdr::airband
