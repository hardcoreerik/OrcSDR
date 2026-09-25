#include "airband_dashboard.hpp"

#include "dashboard_audio_control.hpp"

#include <M5Unified.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace orcsdr::airband {
namespace {

constexpr uint16_t kPanel = 0x0841;
constexpr uint16_t kCyan = 0x2e7f;
constexpr uint16_t kGreen = 0x6fe8;
constexpr uint16_t kYellow = 0xff24;
constexpr uint16_t kAmber = 0xfd20;
constexpr uint16_t kMuted = 0x8c71;
constexpr uint16_t kSelected = 0x1264;
constexpr uint16_t kRedDim = 0x6000;

struct Rect { int x; int y; int w; int h; };

constexpr int kTabsY = 630;
constexpr int kTabW = 256;
constexpr int kTabCount = 5;

constexpr Rect kNowCard{24, 108, 520, 286};
constexpr Rect kControlsCard{556, 108, 700, 286};
constexpr Rect kStatusCard{24, 410, 1232, 200};
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
uint32_t g_last_activity_redraw_ms = 0;

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
      "LISTEN", "SCAN", "AIRPORTS", "ACTIVITY", "SETUP"};
  for (int i = 0; i < kTabCount; ++i)
    button({i * kTabW + 4, kTabsY + 4, kTabW - 8, 82}, labels[i],
           static_cast<int>(g_tab) == i);
}

void page_title(const char* title, const char* subtitle) {
  M5.Display.fillRect(0, 93, 1280, kTabsY - 93, TFT_BLACK);
  text(title, 32, 126, TFT_WHITE, 3, middle_left);
  text(subtitle, 34, 160, kMuted, 2, middle_left);
}

void draw_signal_meter() {
  const Rect meter{48, 304, 468, 24};
  M5.Display.drawRoundRect(meter.x, meter.y, meter.w, meter.h, 6, kCyan);
  const float level = std::clamp((g_snapshot.signal_dbfs + 110.0f) / 80.0f, 0.0f, 1.0f);
  const int filled = static_cast<int>((meter.w - 4) * level);
  if (filled > 0)
    M5.Display.fillRect(meter.x + 2, meter.y + 3, filled, meter.h - 6,
                        g_snapshot.squelch_open ? kGreen : kYellow);
  char value[64];
  std::snprintf(value, sizeof(value), "%.0f dBFS   SQL %s",
                static_cast<double>(g_snapshot.signal_dbfs),
                g_snapshot.squelch_open ? "OPEN" : "CLOSED");
  text(value, meter.x, meter.y + 42,
       g_snapshot.squelch_open ? kGreen : kMuted, 2, middle_left);
}

void draw_listen() {
  M5.Display.fillRect(0, 93, 1280, kTabsY - 93, TFT_BLACK);
  card(kNowCard, state_color());
  card(kControlsCard);

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
  button(control(2, 2), g_snapshot.catalog_loaded ? "FAA DATA READY" : "NO FAA DATA",
         g_snapshot.catalog_loaded, true, 2);

  card(kStatusCard);
  text("AIRBAND STATUS", 44, 438, kCyan, 2, middle_left);
  std::snprintf(value, sizeof(value), "%lu scan stops   %lu channels checked",
                static_cast<unsigned long>(g_snapshot.stops),
                static_cast<unsigned long>(g_snapshot.channels_checked));
  text(value, 44, 480, TFT_WHITE, 2, middle_left);
  std::snprintf(value, sizeof(value), "Squelch %d dBFS   hang %.1fs   settle %ums",
                static_cast<int>(g_snapshot.scan.squelch_dbfs),
                static_cast<double>(g_snapshot.scan.hang_ms) / 1000.0,
                static_cast<unsigned>(g_snapshot.scan.settle_ms));
  text(value, 44, 516, kMuted, 2, middle_left);
  std::snprintf(value, sizeof(value), "Bank %u channels   Guard priority %s",
                static_cast<unsigned>(g_snapshot.bank_count),
                g_snapshot.scan.priority_guard ? "ON" : "OFF");
  text(value, 44, 552, kMuted, 2, middle_left);
  if (!g_snapshot.catalog_loaded)
    text("Tip: install /orcsdr/data/faa_aviation.idx for airport-aware scanning.",
         44, 586, kAmber, 2, middle_left);
  else
    text("Airport bank uses the closest available FAA entries from the offline pack.",
         44, 586, kGreen, 2, middle_left);
}

void draw_scan() {
  page_title("SMART SCAN", "Fast memory-bank scan first; full-band scan remains available.");
  char value[96];
  card({24, 178, 1232, 104}, state_color());
  std::snprintf(value, sizeof(value), "%s   target %.3f MHz",
                state_name(g_snapshot.scan_state),
                mhz(g_snapshot.frequency_hz));
  text(value, 44, 208, state_color(), 3, middle_left);
  std::snprintf(value, sizeof(value), "%s   %s   SQL %d dBFS",
                source_name(g_snapshot.scan.source), spacing_name(g_snapshot.scan.spacing),
                static_cast<int>(g_snapshot.scan.squelch_dbfs));
  text(value, 44, 252, kMuted, 2, middle_left);
  button({958, 194, 278, 66},
         g_snapshot.scan_state == ScanState::off ? "START SCAN" : "STOP SCAN",
         g_snapshot.scan_state != ScanState::off);

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

void draw_airports() {
  page_title("AIRPORT / ATC CHANNELS",
             "Offline FAA catalog. Rows are nearest-first when receiver location is configured.");
  button({990, 114, 266, 58}, "RELOAD DATA");
  char value[112];
  if (!g_snapshot.catalog_loaded || g_snapshot.catalog_count == 0) {
    card({24, 218, 1232, 270});
    text("FAA AIRBAND DATA NOT LOADED", 640, 294, kAmber, 3);
    text("Expected file: /orcsdr/data/faa_aviation.idx", 640, 350, TFT_WHITE, 2);
    text("The dashboard still supports manual tuning and full-band scan.", 640, 396,
         kMuted, 2);
    text("Add the offline pack, then tap RELOAD DATA.", 640, 438, kMuted, 2);
    return;
  }
  const size_t rows = std::min<size_t>(kListRows, g_snapshot.catalog_count);
  for (size_t i = 0; i < rows; ++i) {
    const CatalogEntry& entry = g_snapshot.catalog[i];
    const char* service = service_name(entry.service);
    if (g_snapshot.location_configured)
      std::snprintf(value, sizeof(value),
                    "%9.3f MHz   %-10s   %5.1f nm   %-45.45s",
                    mhz(entry.frequency_hz), service,
                    static_cast<double>(entry.distance_nm), entry.label);
    else
      std::snprintf(value, sizeof(value),
                    "%9.3f MHz   %-10s   %-56.56s",
                    mhz(entry.frequency_hz), service, entry.label);
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
                  "%9.3f MHz   %5.1fs   %5.0f dBFS   %-34.34s   %s",
                  mhz(item.frequency_hz),
                  static_cast<double>(item.duration_ms) / 1000.0,
                  static_cast<double>(item.peak_dbfs), item.label, age);
    button(list_row(static_cast<int>(i)), value,
           item.frequency_hz == g_snapshot.frequency_hz, true, 2);
  }
}

void setup_value(int row, char* out, size_t size) {
  switch (row) {
    case 0: std::snprintf(out, size, "%s", spacing_name(g_snapshot.scan.spacing)); break;
    case 1: std::snprintf(out, size, "%s", source_name(g_snapshot.scan.source)); break;
    case 2:
      if (g_snapshot.scan.squelch_dbfs <= -100)
        std::snprintf(out, size, "OPEN");
      else
        std::snprintf(out, size, "%d dBFS", static_cast<int>(g_snapshot.scan.squelch_dbfs));
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
      "REPLY HANG", "121.500 PRIORITY", "OFFLINE FAA DATA"};
  static constexpr const char* help[kSetupRows] = {
      "25 kHz or 8.33 kHz tuning plan",
      "Closest airport frequencies or the complete civil voice band",
      "Mute audio and stop scan only above this measured receiver level",
      "Time allowed after each tuner move before evaluating activity",
      "Wait this long for a reply before resuming the scan",
      "Periodically check the civil emergency / guard frequency",
      "Local catalog; never invents airport or controller identity"};

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
  }
}

Action tab_touch(int32_t x) {
  g_tab = static_cast<Tab>(std::clamp<int32_t>(x / kTabW, 0, kTabCount - 1));
  draw_tabs();
  draw_page();
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
  if (hit(x, y, {990, 114, 266, 58})) return {ActionKind::reload_catalog};
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
  g_tab = Tab::listen;
  dashboard_draw();
}

void dashboard_leave() { g_active = false; }

void dashboard_draw() {
  if (!g_active) return;
  M5.Display.fillScreen(TFT_BLACK);
  draw_header();
  draw_tabs();
  draw_page();
}

void dashboard_update(const Snapshot& snapshot) {
  if (!g_active) return;
  const bool header_changed =
      snapshot.sound_enabled != g_snapshot.sound_enabled ||
      snapshot.battery_percent != g_snapshot.battery_percent ||
      snapshot.running != g_snapshot.running;
  const bool page_changed =
      snapshot.frequency_hz != g_snapshot.frequency_hz ||
      snapshot.scan_state != g_snapshot.scan_state ||
      snapshot.squelch_open != g_snapshot.squelch_open ||
      snapshot.scan.squelch_dbfs != g_snapshot.scan.squelch_dbfs ||
      snapshot.scan.spacing != g_snapshot.scan.spacing ||
      snapshot.scan.source != g_snapshot.scan.source ||
      snapshot.catalog_loaded != g_snapshot.catalog_loaded ||
      snapshot.catalog_count != g_snapshot.catalog_count ||
      snapshot.activity_count != g_snapshot.activity_count ||
      snapshot.stops != g_snapshot.stops ||
      std::fabs(snapshot.signal_dbfs - g_snapshot.signal_dbfs) >= 2.0f;
  g_snapshot = snapshot;
  if (header_changed) draw_header();
  if (page_changed ||
      (g_tab == Tab::activity && snapshot.now_ms - g_last_activity_redraw_ms >= 5000u)) {
    draw_page();
    if (g_tab == Tab::activity) g_last_activity_redraw_ms = snapshot.now_ms;
  }
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
  }
  return {};
}

bool dashboard_active() { return g_active; }
Tab dashboard_tab() { return g_tab; }

bool dashboard_self_check() {
  const bool was_active = g_active;
  const Tab saved_tab = g_tab;
  EXT_RAM_BSS_ATTR static Snapshot saved;
  saved = g_snapshot;
  g_active = true;
  g_snapshot = {};
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
            kTabsY + 90 <= 720 &&
            kStatusCard.y + kStatusCard.h <= kTabsY;

  g_snapshot = saved;
  g_tab = saved_tab;
  g_active = was_active;
  return ok;
}

}  // namespace orcsdr::airband
