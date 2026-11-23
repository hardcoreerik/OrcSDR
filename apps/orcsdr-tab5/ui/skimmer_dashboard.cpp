#include "skimmer_dashboard.hpp"
#include "dashboard_audio_control.hpp"

#include <M5Unified.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace orcsdr::skimmer {
namespace {
constexpr int kScreenW = 1280, kScreenH = 720;
constexpr int kTopH = 100, kTabsY = 646, kTabW = 320;
constexpr int kLeftX = 20, kLeftW = 782, kRightX = 814, kRightW = 446;
constexpr int kCardY = 196, kCardH = 432;
constexpr int kPlotX = 38, kPlotY = 262, kPlotW = 746, kPlotH = 209;
constexpr uint16_t kBg = TFT_BLACK;
constexpr uint16_t kCard = 0x0882;
constexpr uint16_t kBorder = 0x2350;
constexpr uint16_t kGrid = 0x1985;
constexpr uint16_t kCyan = 0x05ff;
constexpr uint16_t kGreen = 0x5f2b;
constexpr uint16_t kAmber = 0xfe80;
constexpr uint16_t kMuted = 0x9cd3;

bool g_active = false;
bool g_demo = true;  // Preview-first branch: explicitly labelled DEMO until backend exists.
Tab g_tab = Tab::live;
Mode g_filter = Mode::both;
int g_selected = 0;
Snapshot g_live{};
uint32_t g_drawn_signal_rev = 0, g_drawn_sample_rev = 0;
void (*g_header_hook)() = nullptr;

bool hit(int32_t x, int32_t y, int bx, int by, int bw, int bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}
void txt(const char* str, int x, int y, uint16_t color = TFT_WHITE,
         bool big = false, textdatum_t datum = middle_left) {
  M5.Display.setFont(big ? &fonts::DejaVu24 : &fonts::DejaVu18);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(color);
  M5.Display.setTextDatum(datum);
  M5.Display.drawString(str, x, y);
}
void card(int x, int y, int w, int h) {
  M5.Display.fillRoundRect(x, y, w, h, 10, kCard);
  M5.Display.drawRoundRect(x, y, w, h, 10, kBorder);
}
void button(int x, int y, int w, int h, const char* label, bool active = false) {
  const uint16_t bg = active ? 0x0990 : 0x10c3;
  M5.Display.fillRoundRect(x, y, w, h, 8, bg);
  M5.Display.drawRoundRect(x, y, w, h, 8, active ? kCyan : kBorder);
  txt(label, x + w / 2, y + h / 2, active ? kCyan : TFT_WHITE, false, middle_center);
}
const char* mode_name(Mode mode) {
  switch (mode) { case Mode::cw: return "CW"; case Mode::rtty: return "RTTY"; default: return "BOTH"; }
}
const Snapshot& current() {
  return g_live;
}
Snapshot make_demo() {
  Snapshot out{};
  out.source = Source::demo;
  out.center_hz = 14030000;
  out.span_hz = 40000;
  out.sample_revision = 1;
  out.signal_revision = 1;
  out.count = 5;
  const uint32_t freqs[] = {14015300, 14022300, 14028500, 14035300, 14044000};
  const Mode modes[] = {Mode::cw, Mode::cw, Mode::cw, Mode::rtty, Mode::rtty};
  const int snrs[] = {-6, 9, -12, 5, -3};
  const char* labels[] = {"DEMO-A", "DEMO-B", "DEMO-C", "DEMO-D", "DEMO-E"};
  const char* texts[] = {"CQ TEST CQ TEST DE DEMO-A", "DE DEMO-B TESTING CW", "TEST DE DEMO-C",
                         "RYRY TEST MESSAGE DEMO-D", "CQ RTTY TEST DEMO-E"};
  for (size_t i = 0; i < out.count; ++i) {
    Signal& s = out.signals[i];
    s.frequency_hz = freqs[i];
    s.mode = modes[i];
    s.snr_db = static_cast<int16_t>(snrs[i]);
    s.confidence = static_cast<uint8_t>(82 + (i % 3) * 7);
    s.decoded = true;
    std::snprintf(s.callsign, sizeof(s.callsign), "%s", labels[i]);
    std::snprintf(s.message, sizeof(s.message), "%s", texts[i]);
  }
  return out;
}
size_t matches(size_t* indices, size_t max_count) {
  size_t n = 0;
  const Snapshot& snapshot = current();
  for (size_t i = 0; i < std::min<size_t>(snapshot.count, kMaxSignals) && n < max_count; ++i) {
    if (g_filter != Mode::both && snapshot.signals[i].mode != g_filter) continue;
    indices[n++] = i;
  }
  return n;
}
void header() {
  M5.Display.fillRect(0, 0, kScreenW, kTopH, kBg);
  audio_header::draw_brand("CW / RTTY SKIMMER");
  M5.Display.drawFastHLine(20, 98, 1240, kBorder);
  if (g_header_hook) g_header_hook();
  else {
    audio_header::draw_home_button();
    audio_header::draw_mute_button(false);
    audio_header::draw_visualizer_button(false);
    audio_header::draw_settings_button();
  }
}
void tabs() {
  static constexpr const char* labels[] = {"LIVE", "STATIONS", "DECODES", "SETUP"};
  M5.Display.fillRect(0, kTabsY, kScreenW, kScreenH - kTabsY, kBg);
  M5.Display.drawFastHLine(0, kTabsY, kScreenW, kBorder);
  for (int i = 0; i < 4; ++i) {
    if (i) M5.Display.drawFastVLine(i * kTabW, kTabsY, 74, kBorder);
    if (static_cast<int>(g_tab) == i) {
      M5.Display.fillRect(i * kTabW + 1, kTabsY + 1, kTabW - 2, 72, 0x0864);
      M5.Display.fillRect(i * kTabW + 1, kTabsY + 1, kTabW - 2, 4, kCyan);
    }
    txt(labels[i], i * kTabW + kTabW / 2, kTabsY + 39,
        static_cast<int>(g_tab) == i ? kCyan : kMuted, true, middle_center);
  }
}
void summary() {
  card(20, 110, 1240, 76);
  const Snapshot& s = current();
  char label[40];
  std::snprintf(label, sizeof(label), "%.3f MHz", s.center_hz / 1e6);
  txt(label, 40, 148, TFT_WHITE, true);
  const char* band = s.center_hz >= 14000000 && s.center_hz <= 14350000 ? "20m HF" :
                     s.center_hz >= 7000000 && s.center_hz <= 7300000 ? "40m HF" : "HF / CUSTOM";
  txt(band, 240, 148, kCyan);
  const char* state = g_demo ? "UI PREVIEW - NO LIVE RF" :
                      s.source == Source::live ? "LIVE DECODER DATA" : "DECODER NOT CONNECTED";
  txt(state, 395, 148, g_demo ? kAmber : (s.source == Source::live ? kGreen : kMuted));
  button(853, 122, 114, 49, "CW", g_filter == Mode::cw);
  button(974, 122, 114, 49, "RTTY", g_filter == Mode::rtty);
  button(1095, 122, 143, 49, "BOTH", g_filter == Mode::both);
}
int frequency_x(uint32_t hz) {
  const Snapshot& s = current();
  const int64_t left_hz = static_cast<int64_t>(s.center_hz) - s.span_hz / 2;
  const int64_t delta = static_cast<int64_t>(hz) - left_hz;
  return kPlotX + static_cast<int>(delta * kPlotW / std::max<uint32_t>(1, s.span_hz));
}
void plot() {
  // Lightweight M5GFX-native review canvas. No FFT/IQ samples are synthesized as real measurements.
  M5.Display.fillRect(kPlotX, kPlotY, kPlotW, kPlotH, 0x0041);
  for (int i = 0; i <= 8; ++i) {
    int x = kPlotX + kPlotW * i / 8;
    M5.Display.drawFastVLine(x, kPlotY, kPlotH, kGrid);
    char freq[24];
    uint32_t hz = current().center_hz - current().span_hz / 2 + current().span_hz * i / 8;
    std::snprintf(freq, sizeof(freq), "%.3f", hz / 1e6);
    txt(freq, x, kPlotY - 14, kMuted, false, middle_center);
  }
  for (int j = 1; j < 5; ++j)
    M5.Display.drawFastHLine(kPlotX, kPlotY + kPlotH * j / 5, kPlotW, kGrid);
  if (g_demo) {
    // Deterministic graphical texture is explicitly a preview, NOT IQ/spectrum evidence.
    for (int i = 0; i < 50; ++i) {
      const int x = kPlotX + (i * 139 + 31) % kPlotW;
      const int y = kPlotY + (i * 31) % kPlotH;
      M5.Display.drawFastVLine(x, y, 12 + i % 18, 0x08c5);
    }
  }
  size_t ids[kMaxSignals]; const size_t n = matches(ids, kMaxSignals);
  for (size_t i = 0; i < n; ++i) {
    const Signal& signal = current().signals[ids[i]];
    const int x = frequency_x(signal.frequency_hz);
    if (x < kPlotX + 3 || x >= kPlotX + kPlotW - 3) continue;
    uint16_t col = signal.mode == Mode::cw ? kGreen : kCyan;
    M5.Display.fillRect(x - 2, kPlotY + 4, 5, kPlotH - 12, col);
    M5.Display.fillRect(x - 8, kPlotY + 12 + (static_cast<int>(i) * 13) % 36,
                        17, kPlotH - 40, signal.mode == Mode::cw ? 0x13a6 : 0x05b5);
    txt(signal.callsign, std::clamp(x, kPlotX + 54, kPlotX + kPlotW - 54),
        kPlotY + 18 + (i % 2) * 33, TFT_WHITE, false, middle_center);
  }
}
void live_rows() {
  M5.Display.fillRect(kRightX + 16, kCardY + 58, kRightW - 32, 347, kCard);
  size_t ids[kMaxSignals]; const size_t n = matches(ids, kMaxSignals);
  if (!n) { txt("No decoded stations", kRightX + 26, kCardY + 96, kMuted); return; }
  for (size_t i = 0; i < std::min<size_t>(n, 5); ++i) {
    const Signal& s = current().signals[ids[i]];
    const int y = kCardY + 75 + static_cast<int>(i) * 64;
    const bool selected = static_cast<int>(ids[i]) == g_selected;
    if (selected) M5.Display.fillRoundRect(kRightX + 12, y - 19, kRightW - 24, 58, 6, 0x0926);
    txt(s.callsign, kRightX + 27, y, selected ? kCyan : TFT_WHITE, true);
    char detail[48];
    std::snprintf(detail, sizeof(detail), "%s  %.3f MHz  %+d dB",
                  mode_name(s.mode), s.frequency_hz / 1e6, static_cast<int>(s.snr_db));
    txt(detail, kRightX + 27, y + 23, kMuted);
  }
}
void latest_decode() {
  M5.Display.fillRect(kLeftX + 16, 489, kLeftW - 32, 127, kCard);
  txt("SELECTED TRANSMISSION", kLeftX + 26, 506, kCyan);
  const Snapshot& s = current();
  if (g_selected < 0 || g_selected >= s.count || g_selected >= static_cast<int>(kMaxSignals)) {
    txt("No transmission selected", kLeftX + 26, 552, kMuted, true);
    return;
  }
  const Signal& signal = s.signals[g_selected];
  char excerpt[51];
  std::snprintf(excerpt, sizeof(excerpt), "%.48s",
                signal.decoded ? signal.message : "Signal detected; not decoded");
  txt(excerpt, kLeftX + 26, 555, TFT_WHITE, true);
  char detail[70];
  std::snprintf(detail, sizeof(detail), "%s  |  %.3f MHz  |  confidence %u%%", mode_name(signal.mode),
                signal.frequency_hz / 1e6, static_cast<unsigned>(signal.confidence));
  txt(detail, kLeftX + 26, 592, kMuted);
}
void live() {
  card(kLeftX, kCardY, kLeftW, kCardH);
  card(kRightX, kCardY, kRightW, kCardH);
  txt("WIDEBAND SIGNAL VIEW", kLeftX + 24, kCardY + 23, kCyan, true);
  txt(g_demo ? "PREVIEW" : "NO FFT BACKEND", kLeftX + kLeftW - 28, kCardY + 24,
      g_demo ? kAmber : kMuted, false, middle_right);
  txt("DECODED STATIONS", kRightX + 24, kCardY + 28, kCyan, true);
  plot();
  M5.Display.drawFastHLine(kLeftX + 16, 480, kLeftW - 32, kBorder);
  latest_decode();
  live_rows();
}
void table_page(bool decodes) {
  card(20, 197, 1240, 431);
  txt(decodes ? "DECODED MESSAGES" : "HEARD STATIONS", 46, 227, kCyan, true);
  txt("Frequency", 45, 277, kMuted);
  txt("Mode", 264, 277, kMuted);
  txt("Station", 430, 277, kMuted);
  txt(decodes ? "Decoded text" : "SNR / confidence", 720, 277, kMuted);
  M5.Display.drawFastHLine(37, 301, 1206, kBorder);
  size_t ids[kMaxSignals]; const size_t n = matches(ids, kMaxSignals);
  if (!n) { txt("No decoder results. Open SETUP to review the UI preview.", 48, 355, kMuted, true); return; }
  for (size_t i = 0; i < std::min<size_t>(n, 6); ++i) {
    const Signal& s = current().signals[ids[i]];
    int y = 330 + static_cast<int>(i) * 47;
    if (static_cast<int>(ids[i]) == g_selected)
      M5.Display.fillRect(37, y - 20, 1206, 43, 0x0926);
    char freq[24], meta[40];
    std::snprintf(freq, sizeof(freq), "%.3f MHz", s.frequency_hz / 1e6);
    std::snprintf(meta, sizeof(meta), "%+d dB  /  %u%%", static_cast<int>(s.snr_db),
                  static_cast<unsigned>(s.confidence));
    txt(freq, 45, y, TFT_WHITE);
    txt(mode_name(s.mode), 264, y, s.mode == Mode::cw ? kGreen : kCyan);
    txt(s.callsign, 430, y, TFT_WHITE, true);
    // Trim, rather than paint beyond the right edge.
    char short_text[55];
    std::snprintf(short_text, sizeof(short_text), "%.51s", decodes ? s.message : meta);
    txt(short_text, 720, y, kMuted);
  }
}
void setup_page() {
  card(20, 197, 1240, 431);
  txt("SKIMMER DASHBOARD SETUP", 46, 234, kCyan, true);
  txt("UI prototype only: no CW/RTTY DSP, retune or DX spotting is active.", 46, 275, kAmber);
  txt("SIGNAL SOURCE", 46, 339, TFT_WHITE, true);
  button(46, 365, 320, 58, g_demo ? "DEMO PREVIEW: ON" : "DEMO PREVIEW: OFF", g_demo);
  txt("All DEMO-A through DEMO-E transmissions are fictional sample records.", 390, 394, kMuted);
  txt("MODE FILTER", 46, 471, TFT_WHITE, true);
  button(46, 498, 180, 56, "CW", g_filter == Mode::cw);
  button(236, 498, 180, 56, "RTTY", g_filter == Mode::rtty);
  button(426, 498, 180, 56, "BOTH", g_filter == Mode::both);
  txt("Coming next: real IQ channelizer, CW/RTTY decoders, and station tracker.", 46, 592, kMuted);
}
void view() {
  M5.Display.fillRect(0, kTopH, kScreenW, kTabsY - kTopH, kBg);
  summary();
  switch (g_tab) {
    case Tab::live: live(); break;
    case Tab::stations: table_page(false); break;
    case Tab::decodes: table_page(true); break;
    case Tab::setup: setup_page(); break;
  }
  tabs();
  g_drawn_signal_rev = current().signal_revision;
  g_drawn_sample_rev = current().sample_revision;
}
}
void set_header_hook(void (*draw_controls)()) { g_header_hook = draw_controls; }
void enter() {
  g_active = true;
  if (g_demo) g_live = make_demo();
  g_selected = 0;
  draw();
}
void leave() { g_active = false; }
bool active() { return g_active; }
Tab tab() { return g_tab; }
Mode filter() { return g_filter; }
Source source() { return g_demo ? Source::demo : g_live.source; }
void draw() {
  if (!g_active) return;
  M5.Display.fillScreen(kBg); // Only structural entry, not a value update.
  header();
  view();
}
void update() {
  if (!g_active || g_demo) return;
  if (g_drawn_signal_rev != g_live.signal_revision) {
    summary(); // Source/decoder state changes are confined to the summary strip.
    if (g_tab == Tab::live) { live_rows(); latest_decode(); }
    else if (g_tab == Tab::stations || g_tab == Tab::decodes) { table_page(g_tab == Tab::decodes); }
    g_drawn_signal_rev = g_live.signal_revision;
  }
  if (g_drawn_sample_rev != g_live.sample_revision) {
    if (g_tab == Tab::live) plot();
    g_drawn_sample_rev = g_live.sample_revision;
  }
}
void set_snapshot(const Snapshot& data) {
  if (g_demo) return; // Preview and true RF data never mix.
  g_live = data;
  if (g_live.count > kMaxSignals) g_live.count = kMaxSignals;
  if (g_live.source == Source::demo) g_live.source = Source::unavailable;
  if (g_selected < 0 && g_live.count) g_selected = 0;
  if (g_selected >= g_live.count) g_selected = g_live.count ? g_live.count - 1 : -1;
}
void show_demo(bool enable) {
  if (g_demo == enable) return;
  g_demo = enable;
  g_live = enable ? make_demo() : Snapshot{};
  g_selected = enable ? 0 : -1;
  if (g_active) draw();
}
bool handle_touch(int32_t x, int32_t y) {
  if (!g_active) return false;
  if (hit(x, y, 0, kTabsY, kScreenW, kScreenH - kTabsY)) {
    const Tab next = static_cast<Tab>(x / kTabW);
    if (next != g_tab) { g_tab = next; draw(); }
    return true;
  }
  Mode next_filter = g_filter;
  if (hit(x, y, 853, 122, 114, 49)) next_filter = Mode::cw;
  else if (hit(x, y, 974, 122, 114, 49)) next_filter = Mode::rtty;
  else if (hit(x, y, 1095, 122, 143, 49)) next_filter = Mode::both;
  else if (g_tab == Tab::setup) {
    if (hit(x, y, 46, 365, 320, 58)) { show_demo(!g_demo); return true; }
    if (hit(x, y, 46, 498, 180, 56)) next_filter = Mode::cw;
    else if (hit(x, y, 236, 498, 180, 56)) next_filter = Mode::rtty;
    else if (hit(x, y, 426, 498, 180, 56)) next_filter = Mode::both;
  }
  if (next_filter != g_filter) { g_filter = next_filter; view(); return true; }
  size_t ids[kMaxSignals]; const size_t n = matches(ids, kMaxSignals);
  if (g_tab == Tab::live && hit(x, y, kRightX + 12, kCardY + 52, kRightW - 24, 340)) {
    const int row = (y - (kCardY + 56)) / 64;
    if (row >= 0 && static_cast<size_t>(row) < std::min<size_t>(n, 5)) {
      g_selected = static_cast<int>(ids[row]);
      live_rows(); latest_decode(); return true;
    }
  }
  if ((g_tab == Tab::stations || g_tab == Tab::decodes) && hit(x, y, 37, 307, 1206, 308)) {
    int row = (y - 307) / 47;
    if (row >= 0 && static_cast<size_t>(row) < std::min<size_t>(n, 6)) {
      g_selected = static_cast<int>(ids[row]);
      table_page(g_tab == Tab::decodes); return true;
    }
  }
  return false; // Shared header Home/Settings buttons belong to main UI touch router.
}
}  // namespace orcsdr::skimmer
