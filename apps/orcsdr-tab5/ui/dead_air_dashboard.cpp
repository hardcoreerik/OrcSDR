#include "dead_air_dashboard.hpp"
#include "dead_air_info.hpp"
#include "dashboard_audio_control.hpp"
#include "screen_controller.hpp"
#include <M5Unified.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <utility>

#include <chrono>
#if defined(ESP_PLATFORM)
#include <esp_timer.h>
#endif
#include "acars_dirty_rects.hpp"
#include "dead_air_sweep.hpp"
#include "dead_air_format.hpp"
#include "dead_air_palette.hpp"
#include "dead_air_bandplan.hpp"
#include "dead_air_fx.hpp"
#include "dead_air_envhistory.hpp"

namespace orcsdr::dead_air {
namespace {
constexpr uint16_t kBg = TFT_BLACK, kPanel = 0x0841, kEdge = 0x2945,
  kGreen = 0x07e0, kCyan = 0x2e7f, kViolet = 0xa15f,
  kMuted = 0x8c71, kYellow = 0xffe0, kRed = 0xf800;
constexpr int kTabY = 649, kTabX = 20, kTabWidth = 198, kTabGap = 12, kTabHeight = 52;
constexpr int kBodyY = 114, kBodyBottom = 635;
constexpr int kModeX0 = 40, kModeY0 = 290, kModeDx = 95, kModeDy = 60, kModeW = 86;
constexpr int kStepY = 452;
constexpr int kSweepY = 530;
EventModel* g_events = nullptr;
Snapshot* g_snapshot = nullptr;  // heap, not static: internal .data/.bss below the DMA-pool margin aborts boot
Page g_page = Page::overview;
Overlay g_overlay = Overlay::none;
int g_scrub_cursor = -1;      // sample the owner tapped in the scrub view (-1 = the event itself)
bool g_map_pref = true;       // show the sweep's band map instead of the live waterfall, when a map exists
bool g_prev_sweeping = false;
bool g_fx_dirty = true;       // the orb must be repainted (the body under it was just redrawn)
Fx g_fx;
M5Canvas* g_fx_big = nullptr;     // PSRAM sprites: the orb is composed off-screen and pushed in one piece, so it never flickers
M5Canvas* g_fx_beacon = nullptr;
uint32_t g_fx_last_ms = 0;
bool g_active = false;

// Body panels are drawn into an off-screen canvas and pushed once per update, so a refresh never shows a
// cleared frame (that was the flicker). Drawing code keeps absolute screen coordinates; `oy` shifts them
// into the canvas. If the canvas cannot be allocated, drawing falls back to the display directly.
struct Gfx {
  lgfx::LovyanGFX* t = nullptr;
  int oy = 0;
  lgfx::LovyanGFX& d() const { return t ? *t : static_cast<lgfx::LovyanGFX&>(M5.Display); }
  template <class D> void setTextDatum(D v) { d().setTextDatum(v); }
  void setTextSize(uint8_t s) { d().setTextSize(s); }
  void setTextColor(uint16_t c) { d().setTextColor(c); }
  void drawString(const char* s, int x, int y) { d().drawString(s, x, y - oy); }
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) { d().fillRoundRect(x, y - oy, w, h, r, c); }
  void drawRoundRect(int x, int y, int w, int h, int r, uint16_t c) { d().drawRoundRect(x, y - oy, w, h, r, c); }
  void drawFastHLine(int x, int y, int w, uint16_t c) { d().drawFastHLine(x, y - oy, w, c); }
  void drawLine(int x0, int y0, int x1, int y1, uint16_t c) { d().drawLine(x0, y0 - oy, x1, y1 - oy, c); }
  void fillRect(int x, int y, int w, int h, uint16_t c) { d().fillRect(x, y - oy, w, h, c); }
  void drawFastVLine(int x, int y, int h, uint16_t c) { d().drawFastVLine(x, y - oy, h, c); }
  void pushImage(int x, int y, int w, int h, const uint16_t* p) { d().pushImage(x, y - oy, w, h, p); }
};
Gfx g;
// Two sprites, as in the ACARS dashboard: a frame is composed in g_canvas and compared with g_front (what the
// panel shows). Only differing tiles are pushed, because a whole-frame push is a slow strided write on the rotated
// panel and shows as a right-to-left sweep. A page change draws straight to the display with the native
// primitives; the composed sprite then becomes the reference for later partial pushes.
M5Canvas* g_canvas = nullptr;
M5Canvas* g_front = nullptr;
bool g_front_valid = false;
constexpr int kBodyHeight = 522;  // kBodyBottom - kBodyY + 1
constexpr size_t kMaxPushRects = 96;
constexpr int kWholeFramePercent = 55;

M5Canvas* make_canvas() {
  M5Canvas* c = new (std::nothrow) M5Canvas(&M5.Display);
  if (!c) return nullptr;
  c->setColorDepth(16);
  c->setPsram(true);
  if (!c->createSprite(1280, kBodyHeight)) { delete c; return nullptr; }
  return c;
}
M5Canvas* body_canvas() {
  if (g_canvas) return g_canvas;
  g_canvas = make_canvas();
  if (g_canvas && !g_front) g_front = make_canvas();  // optional: without it every refresh draws directly
  g_front_valid = false;
  return g_canvas;
}
void release_canvas() {
  for (M5Canvas** c : {&g_canvas, &g_front}) {
    if (*c) { (*c)->deleteSprite(); delete *c; *c = nullptr; }
  }
  g_front_valid = false;
}

bool hit(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}
void label(const char* value, int x, int y, uint16_t color = TFT_WHITE, uint8_t scale = 2) {
  g.setTextDatum(middle_left);
  g.setTextSize(scale);
  g.setTextColor(color);
  g.drawString(value, x, y);
}
void box(int x, int y, int w, int h, uint16_t border = kEdge) {
  g.fillRoundRect(x, y, w, h, 8, kPanel);
  g.drawRoundRect(x, y, w, h, 8, border);
}
void heading(const char* text, int x, int y) { label(text, x, y, kCyan, 2); }
void divider(int x, int y, int width) { g.drawFastHLine(x, y, width, kEdge); }
uint32_t now_us() {
#if defined(ESP_PLATFORM)
  return static_cast<uint32_t>(esp_timer_get_time());
#else
  return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}
// ---- Ectoplasm spectrum and waterfall --------------------------------------------------------------------------
// Live data is drawn straight onto the panel, the way the Home, LoRa and Shortwave dashboards do it: the trace erases
// its previous polyline and draws the new one, and the waterfall scrolls with the panel's own scroll plus one new row.
// The off-screen page holds only the static frame (boxes, grid, legend), which does not change between spectrum
// frames, so the tile diff never repaints this area. Colour is only a map over measured data (dead_air_palette.hpp).
constexpr int kPlotX = 373, kPlotY = 166, kPlotW = 862, kPlotH = 130;
constexpr int kWfX = 373, kWfY = 306, kWfW = 862, kWfH = 150;
constexpr int kLegendY = 464;
constexpr float kTrailDecayDbPerS = 12.0f;
constexpr uint32_t kWaterfallRowMs = 100;  // about 10 waterfall rows per second, whatever the trace rate
struct Scope {
  uint16_t lut[viz::kLutSize];
  viz::Trail trail;
  int16_t prev_x[kTracePoints], prev_y[kTracePoints], prev_trail_y[kTracePoints];
  size_t prev_n = 0;
  uint16_t row[kWfW];  // native RGB565, pushed as rgb565_t
  uint32_t last_row_ms = 0, last_frame_ms = 0, last_epoch = 0;
  bool have_epoch = false;
  bool was_valid = false;
  int16_t a_prev_half[kBins];  // audio envelope as last drawn (half height in pixels)
  size_t a_prev_n = 0;
  int a_state = -1;            // 0 = notice shown, 1 = live envelope drawn
  uint32_t a_text_ms = 0;
  int16_t pk_x = 0, pk_y = 0;  // the peak marker as last drawn
  bool pk_valid = false;
  uint32_t pk_text_ms = 0;
};
Scope* g_scope = nullptr;
PaintStats g_stats{};

uint16_t plot_bg() { return viz::rgb565(6, 3, 18); }
uint16_t plot_grid() { return viz::rgb565(34, 18, 66); }

void scope_alloc() {
  if (g_scope) return;
  g_scope = new (std::nothrow) Scope();
  if (g_scope) viz::build_lut(g_scope->lut);
}
void scope_release() {
  delete g_scope;
  g_scope = nullptr;
}

int level_y(float dbfs, int top, int height) {
  return top + height - 1 - static_cast<int>(viz::normalize(dbfs) * static_cast<float>(height - 2));
}
void draw_plot_grid(lgfx::LovyanGFX& d) {
  const int x0 = kPlotX + 1, w = kPlotW - 2;
  for (int db = -20; db >= -100; db -= 20) d.drawFastHLine(x0, level_y(static_cast<float>(db), kPlotY, kPlotH), w, plot_grid());
}

// Static page elements (drawn with the rest of the page, never per spectrum frame).
void draw_trace() {
  g.fillRect(kPlotX, kPlotY, kPlotW, kPlotH, plot_bg());
  for (int db = -20; db >= -100; db -= 20)
    g.drawFastHLine(kPlotX, level_y(static_cast<float>(db), kPlotY, kPlotH), kPlotW, plot_grid());
  g.drawRoundRect(kPlotX, kPlotY, kPlotW, kPlotH, 0, plot_grid());
  label("-20", kPlotX + 6, level_y(-20.0f, kPlotY, kPlotH) + 10, kMuted, 2);
  label("-60", kPlotX + 6, level_y(-60.0f, kPlotY, kPlotH) + 10, kMuted, 2);
  label("-100", kPlotX + 6, level_y(-100.0f, kPlotY, kPlotH) - 10, kMuted, 2);
}
void draw_waterfall() {
  g.fillRect(kWfX, kWfY, kWfW, kWfH, plot_bg());
  g.drawRoundRect(kWfX, kWfY, kWfW, kWfH, 0, plot_grid());
}
void draw_legend() {
  if (g_scope) {
    for (int x = 0; x < kWfW; ++x)
      g.drawFastVLine(kWfX + x, kLegendY, 10, g_scope->lut[x * (viz::kLutSize - 1) / (kWfW - 1)]);
  }
  static const char* const kTicks[] = {"-110", "-85", "-60", "-35", "-10 dBFS"};
  for (int i = 0; i < 5; ++i) {
    const int len = static_cast<int>(std::strlen(kTicks[i]));
    int x = kWfX + i * (kWfW - 1) / 4 - len * 6;
    x = std::max(kWfX, std::min(x, kWfX + kWfW - len * 12));
    label(kTicks[i], x, 489, kMuted, 2);
  }
}

// Erases the live trace (not the waterfall history) and forgets it.
void scope_erase_trace() {
  if (!g_scope) return;
  Scope& s = *g_scope;
  M5.Display.startWrite();
  M5.Display.fillRect(kPlotX + 1, kPlotY + 1, kPlotW - 2, kPlotH - 2, plot_bg());
  draw_plot_grid(M5.Display);
  M5.Display.endWrite();
  s.prev_n = 0;
  s.trail.reset();
}

void waterfall_push_row(Scope& s) {
  M5.Display.scroll(0, -1);
  M5.Display.pushImage(kWfX + 1, kWfY + kWfH - 3, kWfW - 2, 1, reinterpret_cast<const lgfx::rgb565_t*>(s.row));
}

void button(const char* text, int x, int y, int w, uint16_t accent, bool enabled) {
  box(x, y, w, 52, enabled ? accent : kEdge);
  label(text, x + 16, y + 26, enabled ? TFT_WHITE : kMuted);
}
void reading(const char* name, const Reading& value, int x, int y, const char* format) {
  char output[32] = "--";
  const Status state = reading_status(value);
  if (state == Status::current) snprintf(output, sizeof(output), format, static_cast<double>(value.value));
  label(name, x, y, kMuted);
  label(output, x + 230, y, state == Status::current ? TFT_WHITE : kMuted, 2);
  if (state == Status::stale) label("STALE", x + 400, y, kYellow, 2);
}
const char* recorder_text(RecorderState state) {
  switch (state) {
    case RecorderState::starting: return "STARTING";
    case RecorderState::active: return "SAVING TO SD";
    case RecorderState::failed: return "SD FAILED";
    case RecorderState::stopped: return "STOPPED";
    default: return "NO SD / OFF";
  }
}
uint16_t recorder_color(RecorderState state) {
  switch (state) {
    case RecorderState::active: return kGreen;
    case RecorderState::starting: return kYellow;
    case RecorderState::failed: return kRed;
    default: return kMuted;
  }
}
void nav() {
  static constexpr const char* tabs[] = {"Overview", "Spectrum", "Audio", "Environment", "Events", "Review"};
  M5.Display.fillRect(0, 641, 1280, 79, kBg);
  for (uint8_t i = 0; i < static_cast<uint8_t>(Page::count); ++i) {
    const int x = kTabX + i * (kTabWidth + kTabGap);
    box(x, kTabY, kTabWidth, kTabHeight, g_page == static_cast<Page>(i) ? kGreen : kEdge);
    label(tabs[i], x + 17, kTabY + 26,
          g_page == static_cast<Page>(i) ? kGreen : TFT_WHITE, 2);
  }
}
void sanitize_snapshot() {
  // A disconnected module may leave cached readings. Never render those live.
  if (!g_snapshot->environment_sensor_attached) {
    g_snapshot->temperature_c = {};
    g_snapshot->humidity_rh = {};
    g_snapshot->pressure_hpa = {};
  }
  if (!g_snapshot->receiver_ready) {
    g_snapshot->rf_level_dbfs = {};
    g_snapshot->spectrum_valid = false;
  }
  if (!g_snapshot->audio_available) {
    g_snapshot->audio_level_dbfs = {};
    g_snapshot->audio_envelope_valid = false;
  }
}
// The (i) button and its "about this page" popup, the same idea as the FT8 dashboard's. The popup covers the body
// (below the header, above the tab bar); while it is open the body is not repainted underneath it.
bool g_info_open = false;
size_t g_info_page = 0;
constexpr int kInfoBtnX = 374, kInfoBtnY = 17, kInfoBtnW = 44, kInfoBtnH = 44;
constexpr int kInfoPanelX = 160, kInfoPanelY = 108, kInfoPanelW = 960, kInfoPanelH = 520;
constexpr size_t kInfoLinesPerPage = 12;
constexpr int kInfoPrevX = 180, kInfoNextX = 350, kInfoCloseX = 950, kInfoBtnRowY = 560, kInfoBtnRowW = 150;
info::Topic info_topic() {
  const size_t p = static_cast<size_t>(g_page);
  return p < static_cast<size_t>(info::Topic::count) ? static_cast<info::Topic>(p) : info::Topic::overview;
}
size_t info_page_count() {
  const info::Page p = info::page(info_topic());
  return (p.line_count + kInfoLinesPerPage - 1) / kInfoLinesPerPage;
}
void draw_info_button() {
  const int cx = kInfoBtnX + kInfoBtnW / 2, cy = kInfoBtnY + kInfoBtnH / 2;
  const uint16_t c = g_info_open ? kGreen : kCyan;
  M5.Display.fillCircle(cx, cy, 21, g_info_open ? kEdge : kPanel);
  M5.Display.drawCircle(cx, cy, 21, c);
  M5.Display.drawCircle(cx, cy, 20, c);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(3);
  M5.Display.setTextColor(c);
  M5.Display.drawString("i", cx, cy);
}
void draw_info_panel() {
  g.t = &M5.Display;
  g.oy = 0;
  const info::Page p = info::page(info_topic());
  const size_t pages = info_page_count();
  if (g_info_page >= pages) g_info_page = pages ? pages - 1 : 0;
  box(kInfoPanelX, kInfoPanelY, kInfoPanelW, kInfoPanelH, kCyan);
  char heading[64];
  snprintf(heading, sizeof(heading), "ABOUT THIS PAGE  -  %s", p.title);
  label(heading, kInfoPanelX + 22, kInfoPanelY + 28, kCyan, 2);
  divider(kInfoPanelX + 20, kInfoPanelY + 52, kInfoPanelW - 40);
  for (size_t i = 0; i < kInfoLinesPerPage; ++i) {
    const size_t index = g_info_page * kInfoLinesPerPage + i;
    if (index >= p.line_count) break;
    if (p.lines[index][0]) label(p.lines[index], kInfoPanelX + 24, kInfoPanelY + 78 + static_cast<int>(i) * 30, TFT_WHITE, 2);
  }
  char page_text[24];
  snprintf(page_text, sizeof(page_text), "PAGE %u/%u", static_cast<unsigned>(g_info_page + 1), static_cast<unsigned>(pages ? pages : 1));
  label(page_text, 560, kInfoBtnRowY + 26, kMuted, 2);
  button("PREV", kInfoPrevX, kInfoBtnRowY, kInfoBtnRowW, kCyan, g_info_page > 0);
  button("NEXT", kInfoNextX, kInfoBtnRowY, kInfoBtnRowW, kCyan, g_info_page + 1 < pages);
  button("CLOSE", kInfoCloseX, kInfoBtnRowY, kInfoBtnRowW, kCyan, true);
}

// Always-visible state of the optional microphone. Blank while it is off.
void mic_banner() {
  M5.Display.fillRect(420, 12, 446, 54, kBg);
  const uint8_t m = g_snapshot->mic_state;
  if (!m) return;
  const uint16_t color = m == 1 ? kRed : m == 2 ? kYellow : kRed;
  M5.Display.fillRoundRect(420, 12, 446, 54, 8, kPanel);
  M5.Display.drawRoundRect(420, 12, 446, 54, 8, color);
  if (m == 1) M5.Display.fillCircle(446, 39, 9, color);
  label(m == 1 ? "MIC LISTENING" : m == 2 ? "MIC PAUSED: SPEAKER ON" : "MIC FAILED", m == 1 ? 466 : 440, 39, color, 2);
}
void chrome() {
  M5.Display.setFont(nullptr);
  M5.Display.fillScreen(kBg);
  audio_header::draw_brand("DEAD AIR");
  audio_header::draw_battery(g_snapshot->battery_percent);
  audio_header::draw_home_button();
  audio_header::draw_mute_button(g_snapshot->sound_enabled);
  audio_header::draw_visualizer_button(false);
  audio_header::draw_settings_button();
  mic_banner();
  draw_info_button();
  nav();
}
constexpr int kFxBigX = 500, kFxBigY = 138, kFxBigS = 120;      // the orb on the Overview
constexpr int kFxBeaconX = 800, kFxBeaconY = 13, kFxBeaconS = 52;  // the same orb, small, in every tab's header
constexpr int kSessionBtnX = 498, kSessionBtnY = 305, kSessionBtnW = 122;

const char* sensitivity_name(Sensitivity s) {
  return s == Sensitivity::quiet ? "QUIET" : s == Sensitivity::sensitive ? "SENSITIVE" : "STANDARD";
}

// ---- Overview RF strip: the last minute of the measured level against its baseline and event limit ----------------
// Drawn with the rest of the page (the tile diff pushes only the columns that changed). Gaps in the measurement break
// the line; nothing is interpolated across a retune, a sweep or a lost receiver.
constexpr int kStripX = 670, kStripY = 236, kStripW = 566, kStripH = 88;
uint16_t mark_color(uint8_t mark) {
  switch (static_cast<HistMark>(mark)) {
    case HistMark::rf_rise: return kRed;
    case HistMark::audio_burst: return 0xfd20;  // orange
    case HistMark::manual: return kViolet;
    default: return kMuted;
  }
}
void dashed_hline(int x, int y, int w, uint16_t color) {
  for (int i = 0; i < w; i += 10) g.drawFastHLine(x + i, y, std::min(5, w - i), color);
}
void draw_rf_strip() {
  const Snapshot& n = *g_snapshot;
  g.fillRect(kStripX, kStripY, kStripW, kStripH, plot_bg());
  g.drawRoundRect(kStripX, kStripY, kStripW, kStripH, 0, plot_grid());
  const int x0 = kStripX + 1, w = kStripW - 2, top = kStripY + 3, h = kStripH - 6;
  const size_t count = std::min<size_t>(n.rf_history_count, kRfHistory);
  const bool lines = g_events && n.rf_level_dbfs.baseline_valid;
  const float base = n.rf_level_dbfs.baseline;
  const float limit = lines ? base + rf_rise_threshold_db(g_events->sensitivity()) : 0.0f;
  const StripScale scale = strip_scale(n.rf_history, count, lines, base, limit);
  const auto sx = [&](size_t i) {  // the newest sample sits at the right edge
    return x0 + w - 1 - static_cast<int>((count - 1 - i) * static_cast<size_t>(w - 1) / (kRfHistory - 1));
  };
  bool any = false;
  for (size_t i = 0; i < count; ++i) any = any || n.rf_history[i] != kHistGap;
  for (size_t i = 0; i < count; ++i)
    if (n.rf_history_event[i]) g.drawFastVLine(sx(i), top, h, mark_color(n.rf_history_event[i]));
  if (lines) {
    dashed_hline(x0, strip_y(base, scale, top, h), w, kGreen);
    dashed_hline(x0, strip_y(limit, scale, top, h), w, kYellow);
  }
  int px = 0, py = 0;
  bool have_prev = false;
  for (size_t i = 0; i < count; ++i) {
    if (n.rf_history[i] == kHistGap) { have_prev = false; continue; }
    const int x = sx(i), y = strip_y(static_cast<float>(n.rf_history[i]) / 10.0f, scale, top, h);
    if (have_prev) g.drawLine(px, py, x, y, kCyan);
    px = x; py = y; have_prev = true;
  }
  if (have_prev) g.fillRect(px - 1, py - 1, 3, 3, TFT_WHITE);  // newest valid sample
  if (!any) label(n.receiver_ready ? "WAITING FOR A SETTLED LEVEL" : "NO LIVE RF LEVEL", kStripX + 14, kStripY + kStripH / 2, kMuted, 2);
  else if (!lines) label("BASELINE WARMING", kStripX + 10, kStripY + 14, kYellow, 2);
}

void overview() {
  box(20, 120, 612, 252);
  heading("INVESTIGATION", 40, 143);
  label(g_snapshot->recording ? "RECORDING" : "READY", 40, 190,
        g_snapshot->recording ? kRed : kGreen, 3);
  char buffer[52];
  snprintf(buffer, sizeof(buffer), "ELAPSED %02lu:%02lu:%02lu",
    static_cast<unsigned long>(g_snapshot->session_elapsed_s / 3600),
    static_cast<unsigned long>(g_snapshot->session_elapsed_s / 60 % 60),
    static_cast<unsigned long>(g_snapshot->session_elapsed_s % 60));
  label(buffer, 40, 240, TFT_WHITE);
  if (g_snapshot->receiver_ready) {
    snprintf(buffer, sizeof(buffer), "%.3f MHz  %s", g_snapshot->frequency_hz / 1e6,
             g_snapshot->demod);
    label(buffer, 40, 291, kCyan, 3);
  } else label("RECEIVER UNAVAILABLE", 40, 291, kYellow, 2);
  char journal[64];
  snprintf(journal, sizeof(journal), "EVENT LOG: %s", recorder_text(g_snapshot->recorder_state));
  label(journal, 40, 343, recorder_color(g_snapshot->recorder_state), 2);
  // The orb is drawn live at (kFxBigX, kFxBigY) beside these; the session card opens from here.
  button("SESSION", kSessionBtnX, kSessionBtnY, kSessionBtnW, kCyan, true);
  box(646, 120, 614, 252);
  heading("LIVE OBSERVATIONS", 668, 143);
  label("RF 60 s", 920, 143, kMuted, 2);
  label("BASE", 1030, 143, kGreen, 2);
  label("LIMIT", 1100, 143, kYellow, 2);
  reading("RF (dBFS)", g_snapshot->rf_level_dbfs, 670, 178, "%.1f");
  reading("AUDIO (dBFS)", g_snapshot->audio_level_dbfs, 670, 210, "%.1f");
  draw_rf_strip();
  label("Sensitivity", 670, 350, kMuted);
  label(sensitivity_name(g_events ? g_events->sensitivity() : Sensitivity::standard), 935, 350, kGreen);
  box(20, 390, 612, 241);
  heading("LATEST EVENTS", 40, 413);
  if (!g_events || !g_events->count()) label("No recorded observations yet", 40, 483, kMuted);
  else for (size_t i = 0; i < std::min<size_t>(4, g_events->count()); ++i) {
    const Event* e = g_events->newest(i); if (!e) continue;
    char row[96];
    format_event_row(*e, row, sizeof(row));
    label(row, 40, 462 + i * 38, i == 0 ? kGreen : TFT_WHITE, 2);
  }
  box(646, 390, 614, 241);
  heading("ENV III / OPTIONAL", 668, 413);
  label(g_snapshot->environment_sensor_attached ? "MODULE DETECTED" : "MODULE NOT CONNECTED",
        668, 464, g_snapshot->environment_sensor_attached ? kGreen : kMuted, 2);
  reading("TEMP (C)", g_snapshot->temperature_c, 668, 505, "%.1f");
  reading("RH (%)", g_snapshot->humidity_rh, 668, 550, "%.1f");
  reading("PRESSURE (hPa)", g_snapshot->pressure_hpa, 668, 594, "%.1f");
}
// ---- Sweep band map: the level measured on each channel of the last sweep, drawn where the waterfall would be --------
// Each bar is the peak level the receiver measured on that channel while it dwelt there. A channel not yet reached is a
// faint dash, never a bar. Tapping a bar tunes to that channel.
constexpr int kChipX = 1130, kChipY = 128, kChipW = 106, kChipH = 32;
bool map_shown() { return g_snapshot && g_snapshot->map.count > 0 && g_map_pref; }
int map_channel_x(const BandMapView& m, int i) { return kWfX + 1 + i * (kWfW - 2) / m.count; }
void draw_band_map() {
  const BandMapView& m = g_snapshot->map;
  const int bottom = kWfY + kWfH - 3, top = kWfY + 24, h = bottom - top;
  int peak = -1;
  for (int i = 0; i < m.count; ++i) {
    const int xa = map_channel_x(m, i), xb = map_channel_x(m, i + 1);
    const int bw = std::max(1, xb - xa - (xb - xa > 3 ? 1 : 0));
    if (m.level[i] == kHistGap) { g.fillRect(xa, bottom - 1, bw, 2, plot_grid()); continue; }
    const float db = static_cast<float>(m.level[i]) / 10.0f;
    const int bh = std::max(2, static_cast<int>(viz::normalize(db) * static_cast<float>(h)));
    g.fillRect(xa, bottom - bh + 1, bw, bh, g_scope ? viz::color_at(g_scope->lut, db) : kCyan);
    if (peak < 0 || m.level[i] > m.level[peak]) peak = i;
  }
  if (g_snapshot->sweeping && m.step_hz && g_snapshot->frequency_hz >= m.start_hz) {  // the channel being listened to now
    const int cur = static_cast<int>((g_snapshot->frequency_hz - m.start_hz + m.step_hz / 2) / m.step_hz);
    if (cur >= 0 && cur < m.count) g.drawFastVLine(map_channel_x(m, cur), top - 2, h + 2, TFT_WHITE);
  }
  char text[80];
  std::snprintf(text, sizeof(text), "SWEEP MAP  %u/%u CHANNELS", static_cast<unsigned>(m.visited), static_cast<unsigned>(m.count));
  label(text, kWfX + 8, kWfY + 12, kMuted, 2);
  if (peak >= 0) {
    std::snprintf(text, sizeof(text), "PEAK %.3f MHz  %.1f dBFS",
                  static_cast<double>(m.start_hz + static_cast<uint32_t>(peak) * m.step_hz) / 1e6,
                  static_cast<double>(m.level[peak]) / 10.0);
    label(text, kWfX + 340, kWfY + 12, kYellow, 2);
  }
}

void spectrum() {
  box(20, 120, 312, 511);
  heading("TUNING", 40, 144);
  char hz[48];
  if (g_snapshot->receiver_ready) snprintf(hz, sizeof(hz), "%.3f MHz", g_snapshot->frequency_hz / 1e6);
  else snprintf(hz, sizeof(hz), "NO RECEIVER");
  label(hz, 40, 201, kGreen, 3);
  divider(40, 248, 271);
  label("MODE (RECEIVER DEMODULATOR)", 40, 270, kMuted, 2);
  for (int i = 0; i < kModeCount; ++i) {
    const bool selected = g_snapshot->receiver_ready && std::strcmp(g_snapshot->demod, kModeNames[i]) == 0;
    const int x = kModeX0 + (i % 3) * kModeDx, y = kModeY0 + (i / 3) * kModeDy;
    box(x, y, kModeW, 52, selected ? kGreen : g_snapshot->receiver_ready ? kCyan : kEdge);
    label(kModeNames[i], x + 14, y + 26, selected ? kGreen : g_snapshot->receiver_ready ? TFT_WHITE : kMuted);
  }
  label("TUNE (BAND STEP)", 40, 432, kMuted, 2);
  button("- STEP", 40, 452, 128, kCyan, g_snapshot->receiver_ready && !g_snapshot->sweeping);
  button("+ STEP", 183, 452, 128, kCyan, g_snapshot->receiver_ready && !g_snapshot->sweeping);
  label(g_snapshot->receiver_settled ? "SETTLED" : "SETTLING...", 40, 535,
        g_snapshot->receiver_settled ? kGreen : kYellow, 2);
  label("Live trace is one tuned", 40, 580, kMuted, 2);
  label("channel, not a sweep.", 40, 605, kMuted, 2);
  box(346, 120, 914, 511);
  heading("RF SPECTRUM  /  dBFS", 366, 144);
  if (g_snapshot->receiver_ready) label(hz, 986, 144, kGreen, 2);
  if (!(g_snapshot->receiver_ready && g_snapshot->spectrum_valid))
    label(g_snapshot->receiver_ready ? "SETTLING" : "NO LIVE SPECTRUM", 640, 144, kYellow, 2);
  draw_trace();
  draw_waterfall();
  draw_legend();
  if (g_snapshot->map.count > 0) {  // a sweep has measured channels: choose between its map and the live waterfall
    box(kChipX, kChipY, kChipW, kChipH, kCyan);
    label(map_shown() ? "LIVE" : "MAP", kChipX + 28, kChipY + kChipH / 2, kCyan, 2);
  }
  if (map_shown()) draw_band_map();
  heading("SWEEP  /  GENERATED SCHEDULE", 366, 509);
  const bool ready = g_snapshot->receiver_ready;
  const bool run = g_snapshot->sweeping;
  button(run ? "STOP SWEEP" : "START SWEEP", 366, kSweepY, 230, run ? kRed : kGreen, ready);
  button(sweep_preset(g_snapshot->sweep_preset).name, 606, kSweepY, 300, kCyan, ready && !run);
  char dwell[32];
  snprintf(dwell, sizeof(dwell), "DWELL %u ms", static_cast<unsigned>(g_snapshot->sweep_dwell_ms));
  button(dwell, 916, kSweepY, 320, kCyan, ready && !run);
  char info[96];
  if (run)
    snprintf(info, sizeof(info), "HOP %lu  UNOBSERVED %.1f s  RF DETECTOR OFF",
             static_cast<unsigned long>(g_snapshot->sweep_hops), g_snapshot->sweep_unobserved_ms / 1000.0);
  else
    snprintf(info, sizeof(info), "One tuner, one channel at a time. Not simultaneous coverage.");
  label(info, 366, 608, run ? kYellow : kMuted, 2);
}
constexpr int kAuX = 42, kAuY = 171, kAuW = 1194, kAuH = 180;
constexpr int kAuStep = 6;  // pixels the plot rolls per 80 ms level reading
constexpr float kAudioFloorDb = -70.0f;  // envelope scale: -70..0 dBFS maps to 0..full half height

int audio_half(float norm) {
  const int max_half = kAuH / 2 - 3;
  return static_cast<int>(std::clamp(norm, 0.0f, 1.0f) * static_cast<float>(max_half));
}
void draw_audio_grid(lgfx::LovyanGFX& d, int oy) {
  const int cy = kAuY + kAuH / 2 - oy;
  d.drawFastHLine(kAuX + 1, cy, kAuW - 2, plot_grid());
  for (int db = -20; db >= -60; db -= 20) {
    const int h = audio_half((static_cast<float>(db) - kAudioFloorDb) / -kAudioFloorDb);
    d.drawFastHLine(kAuX + 1, cy - h, kAuW - 2, plot_grid());
    d.drawFastHLine(kAuX + 1, cy + h, kAuW - 2, plot_grid());
  }
}
void audio() {
  box(20, 120, 1240, 251);
  heading("DEMODULATED AUDIO  /  PEAK ENVELOPE", 40, 143);
  label("0 = full scale", 900, 143, kMuted, 2);
  g.fillRect(kAuX, kAuY, kAuW, kAuH, plot_bg());
  draw_audio_grid(g.d(), g.oy);
  g.drawRoundRect(kAuX, kAuY, kAuW, kAuH, 0, plot_grid());
  label("-20", kAuX + 6, kAuY + kAuH / 2 - audio_half(50.0f / 70.0f) - 12, kMuted, 2);
  label("-40", kAuX + 6, kAuY + kAuH / 2 - audio_half(30.0f / 70.0f) - 12, kMuted, 2);
  box(20, 388, 773, 243);
  heading("AUDIO ANALYSIS", 40, 415);
  label("RMS LEVEL", 40, 470, kMuted);
  label("PEAK", 40, 515, kMuted);
  label("BASELINE", 40, 560, kMuted);
  label("Level meter only. No speech claims.", 40, 606, kMuted, 2);
  box(809, 388, 451, 243);
  heading("SOURCE", 829, 415);
  label(g_snapshot->receiver_ready ? g_snapshot->demod : "NO AUDIO SOURCE", 829, 462, TFT_WHITE, 3);
  label("Clips and replay: Review tab", 829, 606, kMuted, 2);
}
// ---- Environment trend charts: one-minute averages of the ENV III readings, newest at the right edge. A minute with no
// valid reading is a gap and the line breaks there; nothing is interpolated or carried over.
constexpr int kEnChX = 666, kEnChW = 572, kEnChH = 88;
constexpr int kEnChTop[3] = {238, 360, 482};
constexpr int kEnWinY = 154, kEnWinW = 70, kEnWinH = 40, kEnWinX0 = 1010, kEnWinDx = 76;
constexpr size_t kEnSpans[3] = {60, 360, 1440};  // buckets (minutes) shown by the 1 H, 6 H and 24 H windows
constexpr const char* kEnWinText[3] = {"1 H", "6 H", "24 H"};
constexpr const char* kEnSpanStart[3] = {"-60 m", "-6 h", "-24 h"};
constexpr const char* kEnSpanMid[3] = {"-30 m", "-3 h", "-12 h"};
int g_env_window = -1;  // -1: the smallest window that holds everything recorded so far
int g_env_cursor = -1;  // chart column the owner tapped, or -1
int env_window_index() {
  if (g_env_window >= 0) return g_env_window;
  const size_t have = g_snapshot->env_history ? g_snapshot->env_history->count() : 1;
  for (int i = 0; i < 2; ++i)
    if (have <= kEnSpans[i]) return i;
  return 2;
}
void env_chart(size_t ch, uint16_t color, const char* name, const Reading& now_reading, const char* format, float min_span,
               size_t span) {
  const int top = kEnChTop[ch];
  const EnvHistory* history = g_snapshot->env_history;
  char text[40];
  label(name, kEnChX, top - 14, kMuted, 2);
  g.fillRect(kEnChX, top, kEnChW, kEnChH, plot_bg());
  g.drawRoundRect(kEnChX, top, kEnChW, kEnChH, 0, plot_grid());
  float lo = 0.0f, hi = 0.0f;
  bool any = false;
  if (history) {
    for (int col = 0; col < kEnChW; ++col) {
      const EnvColumn c = env_column(*history, ch, span, kEnChW, static_cast<size_t>(col));
      if (!c.valid) continue;
      if (!any || c.value < lo) lo = c.value;
      if (!any || c.value > hi) hi = c.value;
      any = true;
    }
  }
  // The value at the cursor, else the newest accepted reading.
  const char* tag = "NOW";
  float shown = 0.0f;
  bool have_shown = false;
  if (g_env_cursor >= 0 && history) {
    const EnvColumn c = env_column(*history, ch, span, kEnChW, static_cast<size_t>(g_env_cursor));
    tag = "AT CURSOR";
    have_shown = c.valid;
    shown = c.value;
  } else if (reading_status(now_reading) == Status::current) {
    have_shown = true;
    shown = now_reading.value;
  }
  if (have_shown) std::snprintf(text, sizeof(text), format, static_cast<double>(shown));
  else std::snprintf(text, sizeof(text), "--");
  std::snprintf(text + std::strlen(text), sizeof(text) - std::strlen(text), "  %s", tag);
  label(text, kEnChX + 330, top - 14, have_shown ? TFT_WHITE : kMuted, 2);
  if (!any) {
    label(g_snapshot->environment_sensor_attached ? "NO READINGS IN THIS WINDOW YET" : "NO ENV III READINGS", kEnChX + 14,
          top + kEnChH / 2, kMuted, 2);
    return;
  }
  if (hi - lo < min_span) {  // a quiet room must not look like a trend
    const float mid = (hi + lo) / 2.0f;
    lo = mid - min_span / 2.0f;
    hi = mid + min_span / 2.0f;
  }
  const float pad = (hi - lo) * 0.08f;
  lo -= pad;
  hi += pad;
  const auto y_of = [&](float v) { return top + 2 + static_cast<int>((hi - v) / (hi - lo) * static_cast<float>(kEnChH - 5)); };
  dashed_hline(kEnChX + 1, top + kEnChH / 2, kEnChW - 2, plot_grid());
  int px = 0, py = 0;
  bool have_prev = false;
  for (int col = 0; col < kEnChW; ++col) {
    const EnvColumn c = env_column(*history, ch, span, kEnChW, static_cast<size_t>(col));
    if (!c.valid) { have_prev = false; continue; }
    const int x = kEnChX + col, y = std::max(top + 1, std::min(top + kEnChH - 2, y_of(c.value)));
    if (have_prev) g.drawLine(px, py, x, y, color);
    else g.fillRect(x, y, 1, 1, color);  // an isolated minute still shows
    px = x; py = y; have_prev = true;
  }
  if (have_prev && g_env_cursor < 0) g.fillRect(px - 1, py - 1, 3, 3, TFT_WHITE);  // newest valid column
  std::snprintf(text, sizeof(text), format, static_cast<double>(hi));
  label(text, kEnChX + 6, top + 12, kMuted, 2);
  std::snprintf(text, sizeof(text), format, static_cast<double>(lo));
  label(text, kEnChX + 6, top + kEnChH - 12, kMuted, 2);
}
void environment_charts() {
  const int win = env_window_index();
  const size_t span = kEnSpans[win];
  heading("TREND (1-MIN AVERAGES)", 666, 144);
  for (int i = 0; i < 3; ++i) {
    const int x = kEnWinX0 + i * kEnWinDx;
    const bool on = i == win;
    box(x, kEnWinY, kEnWinW, kEnWinH, on ? kGreen : kEdge);
    label(kEnWinText[i], x + 14, kEnWinY + kEnWinH / 2, on ? kGreen : TFT_WHITE, 2);
  }
  env_chart(0, 0xfd20, "TEMPERATURE (C)", g_snapshot->temperature_c, "%.1f", 2.0f, span);
  env_chart(1, kCyan, "HUMIDITY (%RH)", g_snapshot->humidity_rh, "%.1f", 10.0f, span);
  env_chart(2, kGreen, "PRESSURE (hPa)", g_snapshot->pressure_hpa, "%.1f", 4.0f, span);
  if (g_env_cursor >= 0) {
    for (int ch = 0; ch < 3; ++ch) g.drawFastVLine(kEnChX + g_env_cursor, kEnChTop[ch] + 1, kEnChH - 2, TFT_WHITE);
  }
  label(kEnSpanStart[win], kEnChX, 590, kMuted, 2);
  label(kEnSpanMid[win], kEnChX + kEnChW / 2 - 30, 590, kMuted, 2);
  label("NOW", kEnChX + kEnChW - 36, 590, kMuted, 2);
  if (g_env_cursor >= 0) {
    char age[16], text[64];
    const size_t offset = static_cast<size_t>(kEnChW - 1 - g_env_cursor) * span / kEnChW;
    if (offset == 0) std::snprintf(age, sizeof(age), "NOW");
    else format_duration(static_cast<uint32_t>(offset) * 60u, age, sizeof(age));
    std::snprintf(text, sizeof(text), "CURSOR %s%s  (tap it again to clear)", offset ? "-" : "", age);
    label(text, kEnChX, 612, kViolet, 2);
  } else {
    label("Tap a chart to read a moment.", kEnChX, 612, kMuted, 2);
  }
}
void environment() {
  box(20, 120, 610, 511);
  heading("ENV III / OPTIONAL SENSOR", 40, 144);
  label(g_snapshot->environment_sensor_attached ? "CONNECTED" : "UNAVAILABLE",
        40, 208, g_snapshot->environment_sensor_attached ? kGreen : kMuted, 3);
  reading("TEMPERATURE (C)", g_snapshot->temperature_c, 40, 280, "%.1f");
  reading("HUMIDITY (%)", g_snapshot->humidity_rh, 40, 355, "%.1f");
  reading("PRESSURE (hPa)", g_snapshot->pressure_hpa, 40, 430, "%.1f");
  reading("DEVICE MOTION (g)", g_snapshot->motion_g, 40, 505, "%.3f");
  label("Tab5 motion, NOT room movement.", 40, 548, kMuted, 2);
  label("Core RF investigation works without ENV III.", 40, 598, kMuted, 2);
  box(644, 120, 616, 511);
  environment_charts();
}
bool event_has_clip(uint32_t sequence) {
  for (uint8_t i = 0; i < g_snapshot->clip_event_seq_count && i < 16; ++i)
    if (g_snapshot->clip_event_seqs[i] == sequence) return true;
  return false;
}
// Events list: five rows per page, tap a row for its detail, NEWER / OLDER to page through the whole ring (48 events).
// The selection follows the event's sequence number, and a page that is not the newest keeps its place as events arrive.
constexpr size_t kEvRows = 5;
constexpr int kEvRowX = 34, kEvRowY0 = 166, kEvRowDy = 40, kEvRowW = 739, kEvRowH = 38;
constexpr int kEvDetailY = 374, kEvDetailH = 188;
constexpr int kEvNavY = 568, kEvNavW = 150, kEvNewerX = 40, kEvOlderX = 206;
constexpr int kEvBtnY = 506, kEvScrubBtnX = 50, kEvClipBtnX = 264, kEvBtnW = 200;  // detail-box buttons
constexpr int kEvMarkY = 450, kEvCtlBtnX = 818, kEvCtlBtnY = 510, kEvCtlBtnW = 250, kEvChimeBtnX = 1082, kEvChimeBtnW = 156;
bool g_ev_has_sel = false;
uint32_t g_ev_sel_seq = 0;
size_t g_ev_offset = 0;   // rows between the newest event and the first row shown
uint32_t g_ev_total = 0;  // EventModel::total() when the offset was last adjusted

void ev_reset() {
  g_ev_has_sel = false;
  g_ev_sel_seq = 0;
  g_ev_offset = 0;
  g_ev_total = g_events ? g_events->total() : 0;
}
void ev_sync() {
  if (!g_events) return;
  const uint32_t total = g_events->total();
  if (total < g_ev_total) { g_ev_has_sel = false; g_ev_offset = 0; }  // the model was reset
  else if (total > g_ev_total && g_ev_offset > 0) g_ev_offset += total - g_ev_total;
  g_ev_total = total;
  const size_t count = g_events->count();
  const size_t max_offset = count > kEvRows ? count - kEvRows : 0;
  if (g_ev_offset > max_offset) g_ev_offset = max_offset;
}
const Event* ev_find(uint32_t sequence) {
  for (size_t i = 0; g_events && i < g_events->count(); ++i) {
    const Event* e = g_events->newest(i);
    if (e && e->sequence == sequence) return e;
  }
  return nullptr;
}
// The event the detail box describes: the tapped one while it is still in the ring, otherwise the newest.
const Event* ev_detail() {
  if (!g_events || !g_events->count()) return nullptr;
  if (g_ev_has_sel) {
    if (const Event* e = ev_find(g_ev_sel_seq)) return e;
  }
  return g_events->newest(0);
}
// Index into the snapshot's clip list of a clip that covers this event; a radio clip is preferred to a microphone one.
int clip_for_event(uint32_t sequence) {
  int found = -1;
  for (uint8_t i = 0; i < g_snapshot->clip_count && i < kClipInfoCount; ++i) {
    const ClipInfo& c = g_snapshot->clips[i];
    for (uint8_t k = 0; k < c.seq_count && k < kClipInfoSeqs; ++k) {
      if (c.event_seqs[k] != sequence) continue;
      if (!c.mic) return i;
      if (found < 0) found = i;
    }
  }
  return found;
}

// "  FM broadcast (typical)" after a frequency, or nothing when no receiver was tuned. Allocations differ by country.
const char* band_suffix(uint32_t hz, char* out, size_t capacity) {
  out[0] = 0;
  if (hz) std::snprintf(out, capacity, "  %s%s", band_name(hz), band_for(hz) ? " (typical)" : "");
  return out;
}

void draw_event_detail(const Event& e) {
  box(kEvRowX, kEvDetailY, kEvRowW, kEvDetailH, kEdge);
  char line[112], clock[16], suffix[64];
  const int x = kEvRowX + 16;
  int y = kEvDetailY + 22;
  const auto next = [&](uint16_t color) { label(line, x, y, color, 2); y += 24; };
  std::snprintf(line, sizeof(line), "EVENT #%lu  %s", static_cast<unsigned long>(e.sequence), kind_name(e.kind));
  next(kCyan);
  format_clock(e.unix_time, e.monotonic_ms, clock, sizeof(clock));
  std::snprintf(line, sizeof(line), e.unix_time ? "TIME %s UTC" : "TIME %s since boot (clock not set)", clock);
  next(TFT_WHITE);
  char where[24];
  if (e.frequency_hz) std::snprintf(where, sizeof(where), "%.3f MHz", static_cast<double>(e.frequency_hz) / 1e6);
  else std::snprintf(where, sizeof(where), "no receiver");
  switch (e.kind) {
    case Kind::rf_rise:
    case Kind::audio_burst:
      std::snprintf(line, sizeof(line), "%.1f dBFS  base %.1f  +%.1f dB (limit +%.0f)", static_cast<double>(e.value),
                    static_cast<double>(e.reference), static_cast<double>(e.value - e.reference), static_cast<double>(e.threshold));
      next(TFT_WHITE);
      std::snprintf(line, sizeof(line), "%s%s", where, band_suffix(e.frequency_hz, suffix, sizeof(suffix)));
      next(kMuted);
      std::snprintf(line, sizeof(line), "sensitivity %s", sensitivity_name(e.sensitivity));
      next(kMuted);
      break;
    case Kind::temperature_change:
    case Kind::humidity_change:
    case Kind::pressure_change: {
      const char* unit = e.kind == Kind::temperature_change ? "C" : e.kind == Kind::humidity_change ? "%RH" : "hPa";
      std::snprintf(line, sizeof(line), "%.2f %s  base %.2f  %+.2f (limit %.2f)", static_cast<double>(e.value), unit,
                    static_cast<double>(e.reference), static_cast<double>(e.value - e.reference), static_cast<double>(e.threshold));
      next(TFT_WHITE);
      std::snprintf(line, sizeof(line), "ENV III on Port A. Held away from its baseline, in either direction.");
      next(kMuted);
      std::snprintf(line, sizeof(line), "sensitivity %s", sensitivity_name(e.sensitivity));
      next(kMuted);
      break;
    }
    case Kind::manual_marker:
      std::snprintf(line, sizeof(line), "Marked by you. No measurement attached.");
      next(TFT_WHITE);
      std::snprintf(line, sizeof(line), "%s%s", where, band_suffix(e.frequency_hz, suffix, sizeof(suffix)));
      next(kMuted);
      break;
    case Kind::sweep_start:
      std::snprintf(line, sizeof(line), "%.3f-%.3f MHz  step %.0f kHz", static_cast<double>(e.frequency_hz) / 1e6,
                    static_cast<double>(e.sweep_end_hz) / 1e6, static_cast<double>(e.sweep_step_hz) / 1e3);
      next(TFT_WHITE);
      std::snprintf(line, sizeof(line), "dwell %lu ms  settle %lu ms", static_cast<unsigned long>(e.sweep_dwell_ms),
                    static_cast<unsigned long>(e.sweep_settle_ms));
      next(kMuted);
      break;
    case Kind::sweep_stop:
      std::snprintf(line, sizeof(line), "%lu hops  %.1f s not observed", static_cast<unsigned long>(e.sweep_hops),
                    static_cast<double>(e.sweep_unobserved_ms) / 1000.0);
      next(TFT_WHITE);
      break;
    default: break;
  }
  if (e.source == Source::sweep) return;  // generated schedule records carry no clips
  button("SCRUB", kEvScrubBtnX, kEvBtnY, kEvBtnW, kCyan, true);
  const int clip = clip_for_event(e.sequence);
  const int text_x = kEvClipBtnX + kEvBtnW + 14, text_y = kEvBtnY + 26;
  if (clip >= 0) {
    button("VIEW CLIP", kEvClipBtnX, kEvBtnY, kEvBtnW, kGreen, true);
    std::snprintf(line, sizeof(line), "Clip: %s", g_snapshot->clips[clip].name);
    label(line, text_x, text_y, kGreen, 2);
  } else {
    button("VIEW CLIP", kEvClipBtnX, kEvBtnY, kEvBtnW, kCyan, false);
    label(event_has_clip(e.sequence) ? "Older clip not listed" : "No clip for this event", text_x, text_y, kMuted, 2);
  }
}

void events() {
  ev_sync();
  box(20, 120, 767, 511);
  const size_t count = g_events ? g_events->count() : 0;
  char title[48] = "EVENT LOG";
  if (count) {
    const size_t last = std::min(count, g_ev_offset + kEvRows);
    std::snprintf(title, sizeof(title), "EVENT LOG / %u-%u OF %u", static_cast<unsigned>(g_ev_offset + 1),
                  static_cast<unsigned>(last), static_cast<unsigned>(count));
  }
  heading(title, 40, 144);
  if (!count) {
    label("NO EVENTS RECORDED", 43, 266, kMuted, 3);
  } else {
    for (size_t r = 0; r < kEvRows && g_ev_offset + r < count; ++r) {
      const Event* e = g_events->newest(g_ev_offset + r); if (!e) continue;
      char line[96];
      format_event_row(*e, line, sizeof(line));
      const int y = kEvRowY0 + static_cast<int>(r) * kEvRowDy;
      const bool chosen = g_ev_has_sel && e->sequence == g_ev_sel_seq;
      box(kEvRowX, y, kEvRowW, kEvRowH, chosen ? kGreen : kEdge);
      label(line, kEvRowX + 14, y + kEvRowH / 2,
            e->source == Source::manual ? kViolet : e->source == Source::sweep ? kMuted : TFT_WHITE, 2);
      if (event_has_clip(e->sequence)) label("CLIP", kEvRowX + kEvRowW - 70, y + kEvRowH / 2, kGreen, 2);
    }
    if (const Event* detail = ev_detail()) draw_event_detail(*detail);
    button("NEWER", kEvNewerX, kEvNavY, kEvNavW, kCyan, g_ev_offset > 0);
    button("OLDER", kEvOlderX, kEvNavY, kEvNavW, kCyan, g_ev_offset + kEvRows < count);
    label("Tap an event for details", 380, kEvNavY + 26, kMuted, 2);
  }
  box(801, 120, 459, 511);
  heading("DETECTION", 822, 144);
  label("AUTO: RF RISE + AUDIO RISE", 822, 205, kCyan, 2);
  const Sensitivity sens = g_events ? g_events->sensitivity() : Sensitivity::standard;
  char threshold[64];
  snprintf(threshold, sizeof(threshold), "Threshold +%.0f dB", rf_rise_threshold_db(sens));
  label(threshold, 822, 258, TFT_WHITE, 3);
  const bool rf_ready = g_events && g_events->baseline_ready();
  label(rf_ready ? "RF BASELINE READY" : "RF BASELINE WARMING", 822, 310, rf_ready ? kGreen : kYellow, 2);
  label(!g_snapshot->audio_available ? "AUDIO: NOT FLOWING" : g_snapshot->audio_baseline_ready ? "AUDIO BASELINE READY" : "AUDIO BASELINE WARMING",
        822, 342, !g_snapshot->audio_available ? kMuted : g_snapshot->audio_baseline_ready ? kGreen : kYellow, 2);
  const char* names[3] = {"QUIET", "STANDARD", "SENSITIVE"};
  const int xs[3] = {818, 956, 1094}, ws[3] = {135, 135, 145};
  for (int i = 0; i < 3; ++i) {
    const bool selected = static_cast<int>(sens) == i;  // Sensitivity: quiet, standard, sensitive
    box(xs[i], 382, ws[i], 52, selected ? kGreen : kCyan);
    label(names[i], xs[i] + 16, 408, selected ? kGreen : TFT_WHITE);
  }
  button("MARK EVENT", 818, kEvMarkY, 420, kRed, g_events != nullptr);
  button("CONTROL RUN", kEvCtlBtnX, kEvCtlBtnY, kEvCtlBtnW, kCyan, g_events != nullptr);
  button(g_snapshot->chime_on ? "CHIME ON" : "CHIME OFF", kEvChimeBtnX, kEvCtlBtnY, kEvChimeBtnW,
         g_snapshot->chime_on ? kGreen : kCyan, true);
}
constexpr int kClipBtnX = 40, kClipBtnY = 366, kClipBtnW = 300, kClipBtnH = 52;
constexpr int kMicBtnX = 40, kMicBtnY = 548, kMicBtnW = 300, kMicBtnH = 52;
constexpr int kRowX = 517, kRowY0 = 172, kRowDy = 44, kRowW = 723, kRowH = 40;
constexpr int kVerifyBtnX = 517, kVerifyBtnY = 466, kVerifyBtnW = 260, kVerifyBtnH = 52;
constexpr int kPlayBtnX = 797, kPlayBtnY = 466, kPlayBtnW = 260, kPlayBtnH = 52;
void review() {
  box(20, 120, 460, 511);
  heading("SESSION ARCHIVE", 40, 144);
  label("EVENT LOG (JSONL)", 40, 178, kMuted, 2);
  label(recorder_text(g_snapshot->recorder_state), 40, 206, recorder_color(g_snapshot->recorder_state), 3);
  char line[80];
  snprintf(line, sizeof(line), "Saved %lu  Lost %lu  Dropped %lu", static_cast<unsigned long>(g_snapshot->recorder_saved),
           static_cast<unsigned long>(g_snapshot->recorder_missing), static_cast<unsigned long>(g_snapshot->recorder_dropped));
  label(line, 40, 240, g_snapshot->recorder_missing || g_snapshot->recorder_dropped ? kYellow : TFT_WHITE, 2);
  divider(40, 266, 420);
  label("RECEIVER CLIPS (RADIO AUDIO)", 40, 284, kMuted, 2);
  const char* clip_text = !g_snapshot->clips_enabled ? "OFF" : g_snapshot->clip_state == 1 ? "CAPTURING" :
                          g_snapshot->clip_state == 2 ? "WRITING" : "READY";
  label(clip_text, 40, 316, g_snapshot->clips_enabled ? (g_snapshot->clip_state ? kYellow : kGreen) : kMuted, 3);
  snprintf(line, sizeof(line), "Saved %lu  Failed %lu  Skipped %lu", static_cast<unsigned long>(g_snapshot->clips_saved),
           static_cast<unsigned long>(g_snapshot->clips_failed), static_cast<unsigned long>(g_snapshot->clips_refused));
  label(line, 40, 348, g_snapshot->clips_failed ? kYellow : TFT_WHITE, 2);
  button(g_snapshot->clips_enabled ? "CLIPS ON" : "CLIPS OFF", kClipBtnX, kClipBtnY, kClipBtnW,
         g_snapshot->clips_enabled ? kGreen : kCyan, true);
  divider(40, 436, 420);
  label("MICROPHONE CLIPS (ROOM AUDIO)", 40, 454, kMuted, 2);
  const uint8_t m = g_snapshot->mic_state;
  const char* mic_text = m == 0 ? "OFF" : m == 1 ? (g_snapshot->mic_clip_state == 1 ? "CAPTURING" :
                         g_snapshot->mic_clip_state == 2 ? "WRITING" : "LISTENING") :
                         m == 2 ? "PAUSED: SPEAKER ON" : "FAILED";
  label(mic_text, 40, 486, m == 0 ? kMuted : m == 1 ? kRed : m == 2 ? kYellow : kRed, 3);
  snprintf(line, sizeof(line), "Saved %lu  Failed %lu  Skipped %lu", static_cast<unsigned long>(g_snapshot->mic_clips_saved),
           static_cast<unsigned long>(g_snapshot->mic_clips_failed), static_cast<unsigned long>(g_snapshot->mic_clips_refused));
  label(line, 40, 518, g_snapshot->mic_clips_failed ? kYellow : TFT_WHITE, 2);
  button(m == 1 || m == 2 ? "STOP MIC" : "START MIC", kMicBtnX, kMicBtnY, kMicBtnW, m == 1 || m == 2 ? kRed : kCyan, true);
  if (g_snapshot->mic_error[0]) label(g_snapshot->mic_error, 40, 616, kYellow, 2);
  box(497, 120, 763, 511);
  heading("EVIDENCE REVIEW", 517, 144);
  if (!g_snapshot->clip_count) {
    label("NO CLIPS THIS SESSION", 517, 220, kMuted, 3);
    label("A clip is saved when an event fires:", 517, 290, kMuted, 2);
    label("5 s before and 5 s after the event.", 517, 328, kMuted, 2);
    label("Radio clips are the demodulated audio;", 517, 366, kMuted, 2);
    label("mic clips are the room, never mixed.", 517, 404, kMuted, 2);
  } else {
    const uint8_t sel = std::min<uint8_t>(g_snapshot->selected_clip, static_cast<uint8_t>(g_snapshot->clip_count - 1));
    for (uint8_t i = 0; i < g_snapshot->clip_count && i < kClipInfoCount; ++i) {
      const ClipInfo& o = g_snapshot->clips[i];
      const bool chosen = i == sel;
      box(kRowX, kRowY0 + i * kRowDy, kRowW, kRowH, chosen ? (o.mic ? kRed : kGreen) : kEdge);
      snprintf(line, sizeof(line), "%u %s %s  %u.%u s%s", static_cast<unsigned>(i + 1), o.mic ? "MIC  " : "RADIO", o.name,
               static_cast<unsigned>(o.duration_ds / 10), static_cast<unsigned>(o.duration_ds % 10), o.truncated ? "  TRUNCATED" : "");
      label(line, kRowX + 14, kRowY0 + i * kRowDy + kRowH / 2, o.truncated ? kYellow : chosen ? TFT_WHITE : kMuted, 2);
    }
    const ClipInfo& c = g_snapshot->clips[sel];
    if (c.mic)
      snprintf(line, sizeof(line), "ROOM MIC  speaker %s%s", c.speaker_on ? "ON" : "MUTED", c.truncated ? "  TRUNCATED" : "");
    else
      snprintf(line, sizeof(line), "%s  %.3f MHz  speaker %s%s", c.demod, static_cast<double>(c.frequency_hz) / 1e6,
               c.speaker_on ? "ON" : "MUTED", c.truncated ? "  TRUNCATED" : "");
    label(line, 517, 370, c.truncated ? kYellow : TFT_WHITE, 2);
    snprintf(line, sizeof(line), "sha256 %s...", c.sha16);
    label(line, 517, 400, kMuted, 2);
    const uint8_t v = g_snapshot->verify_state;
    label(v == 1 ? "VERIFYING..." : v == 2 ? "VERIFIED: file on the card matches" : v == 3 ? "MISMATCH: file differs from its hash" :
          v == 4 ? "FILE MISSING from the card" : "NOT RE-CHECKED YET", 517, 432,
          v == 2 ? kGreen : (v == 3 || v == 4) ? kRed : v == 1 ? kYellow : kMuted, 2);
    const uint8_t rp = g_snapshot->replay_state;
    button("VERIFY SELECTED", kVerifyBtnX, kVerifyBtnY, kVerifyBtnW, kCyan, v != 1 && rp != 1 && rp != 2);
    button(rp == 2 ? "STOP REPLAY" : rp == 1 ? "LOADING..." : "PLAY SELECTED", kPlayBtnX, kPlayBtnY, kPlayBtnW,
           rp == 2 ? kRed : kGreen, rp != 1);
    label(rp == 2 ? "PLAYING through the speaker; mic paused" : rp == 1 ? "Reading the clip and checking its hash" :
          rp == 3 ? g_snapshot->replay_note : "Plays only if its hash matches the card.", 517, 546,
          rp == 2 ? kGreen : rp == 3 ? kYellow : kMuted, 2);
    label("Replay mutes the radio audio while it plays.", 517, 580, kMuted, 2);
  }
}
// ---- Overlays: scrub an event, control run, session card ---------------------------------------------------------------
// Each covers the whole body and is drawn from the snapshot like a page. The tab bar stays; a tab tap closes the overlay.
constexpr int kOvCloseX = 1070, kOvCloseY = 128, kOvCloseW = 170;
constexpr int kOvBtnY = 560;
void overlay_frame(const char* title) {
  box(20, 120, 1240, 511);
  heading(title, 40, 144);
  button("CLOSE", kOvCloseX, kOvCloseY, kOvCloseW, kCyan, true);
}

constexpr int kScX = 60, kScW = 1160, kScRfY = 190, kScRfH = 104, kScAuY = 302, kScAuH = 62;
constexpr int kScPlayX = 50, kScPlayW = 240;
int scrub_x(size_t i) { return kScX + static_cast<int>(i * static_cast<size_t>(kScW - 1) / (kTraceSamples - 1)); }
size_t scrub_cursor_index() {
  const ScrubView& sv = g_snapshot->scrub;
  if (sv.cursor_valid) {
    const long idx = static_cast<long>(kTracePre) - 1 + std::lround(static_cast<double>(sv.cursor_ms) / kTraceStepMs);
    return static_cast<size_t>(std::min<long>(static_cast<long>(kTraceSamples) - 1, std::max<long>(0, idx)));
  }
  if (g_scrub_cursor >= 0) return static_cast<size_t>(g_scrub_cursor);
  return kTracePre - 1;
}
bool scrub_collected(size_t i) { return i < kTracePre + g_snapshot->scrub.post_filled; }
void scrub_strip(int y, int h, const int16_t* data, const StripScale& scale, uint16_t color, bool lines,
                 float base, float limit) {
  g.fillRect(kScX, y, kScW, h, plot_bg());
  g.drawRoundRect(kScX, y, kScW, h, 0, plot_grid());
  if (lines) {
    dashed_hline(kScX + 1, strip_y(base, scale, y + 2, h - 4), kScW - 2, kGreen);
    dashed_hline(kScX + 1, strip_y(limit, scale, y + 2, h - 4), kScW - 2, kYellow);
  }
  g.drawFastVLine(scrub_x(kTracePre - 1), y + 1, h - 2, TFT_WHITE);  // the event
  int px = 0, py = 0;
  bool have = false;
  for (size_t i = 0; i < kTraceSamples; ++i) {
    if (!scrub_collected(i) || data[i] == kHistGap) { have = false; continue; }  // a gap breaks the line
    const int x = scrub_x(i), yy = strip_y(static_cast<float>(data[i]) / 10.0f, scale, y + 2, h - 4);
    if (have) g.drawLine(px, py, x, yy, color);
    else g.fillRect(x - 1, yy - 1, 3, 3, color);
    px = x; py = yy; have = true;
  }
}
void scrub() {
  const ScrubView& sv = g_snapshot->scrub;
  char text[112];
  std::snprintf(text, sizeof(text), "EVENT #%lu  /  RF AND AUDIO AROUND IT", static_cast<unsigned long>(sv.seq));
  overlay_frame(text);
  if (!sv.found) {
    label("No trace was kept for this event.", 40, 230, kMuted, 3);
    label("Traces are kept for events logged while Dead Air is open.", 40, 290, kMuted, 2);
    return;
  }
  const Event* e = ev_find(sv.seq);
  const bool rf_lines = e && e->kind == Kind::rf_rise;
  const bool au_lines = e && e->kind == Kind::audio_burst;
  const float base = e ? e->reference : 0.0f, limit = e ? e->reference + e->threshold : 0.0f;
  const StripScale rf_scale = strip_scale(sv.rf, kTraceSamples, rf_lines, base, limit);
  const StripScale au_scale = strip_scale(sv.audio, kTraceSamples, au_lines, base, limit);
  scrub_strip(kScRfY, kScRfH, sv.rf, rf_scale, kCyan, rf_lines, base, limit);
  scrub_strip(kScAuY, kScAuH, sv.audio, au_scale, 0xfd20, au_lines, base, limit);
  label("RF", kScX + 8, kScRfY + 14, kMuted, 2);
  label("AUDIO", kScX + 8, kScAuY + 14, kMuted, 2);
  if (sv.post_filled < kTraceSamples - kTracePre) {
    std::snprintf(text, sizeof(text), "COLLECTING +%.0f s OF +10 s", static_cast<double>(sv.post_filled) * kTraceStepMs / 1000.0);
    label(text, kScX + kScW - 330, kScRfY + 14, kYellow, 2);
  }
  static constexpr struct { size_t index; const char* text; } kTicks[] = {
      {0, "-10 s"}, {19, "-5 s"}, {kTracePre - 1, "EVENT"}, {59, "+5 s"}, {kTraceSamples - 1, "+10 s"}};
  for (const auto& tick : kTicks) {
    const int w = static_cast<int>(std::strlen(tick.text)) * 12;
    label(tick.text, std::max(kScX, std::min(scrub_x(tick.index) - w / 2, kScX + kScW - w)), kScAuY + kScAuH + 16, kMuted, 2);
  }
  if (!sv.cursor_valid && g_scrub_cursor >= 0) {  // the owner's cursor
    g.drawFastVLine(scrub_x(scrub_cursor_index()), kScRfY + 1, kScAuY + kScAuH - kScRfY - 1, kViolet);
  }
  if (sv.cursor_valid) g.drawFastVLine(scrub_x(scrub_cursor_index()), kScRfY + 1, kScAuY + kScAuH - kScRfY - 1, kGreen);
  // Readout at the cursor.
  const size_t ci = scrub_cursor_index();
  char rf_text[24] = "--", au_text[24] = "--";
  if (scrub_collected(ci) && sv.rf[ci] != kHistGap) std::snprintf(rf_text, sizeof(rf_text), "%.1f dBFS", static_cast<double>(sv.rf[ci]) / 10.0);
  if (scrub_collected(ci) && sv.audio[ci] != kHistGap) std::snprintf(au_text, sizeof(au_text), "%.1f dBFS", static_cast<double>(sv.audio[ci]) / 10.0);
  std::snprintf(text, sizeof(text), "%s %+.2f s     RF %s     AUDIO %s", sv.cursor_valid ? "PLAYING" : "CURSOR",
                (static_cast<double>(ci) - static_cast<double>(kTracePre - 1)) * kTraceStepMs / 1000.0, rf_text, au_text);
  label(text, 40, 424, sv.cursor_valid ? kGreen : TFT_WHITE, 2);
  if (e && (rf_lines || au_lines)) {
    std::snprintf(text, sizeof(text), "AT THE EVENT  %.1f dBFS  baseline %.1f  +%.1f dB  limit +%.0f dB",
                  static_cast<double>(e->value), static_cast<double>(e->reference), static_cast<double>(e->value - e->reference),
                  static_cast<double>(e->threshold));
    label(text, 40, 452, TFT_WHITE, 2);
  } else if (e && e->source == Source::manual) {
    label("A mark you made: it carries no measurement, only the trace around it.", 40, 452, TFT_WHITE, 2);
  } else if (e) {
    label("A sensor reading moved away from its baseline; see the Events detail.", 40, 452, TFT_WHITE, 2);
  } else {
    label("This event has left the 48 kept in memory; its trace is still shown.", 40, 452, kMuted, 2);
  }
  if (e && e->frequency_hz) {
    char suffix[64];
    std::snprintf(text, sizeof(text), "%.3f MHz%s", static_cast<double>(e->frequency_hz) / 1e6, band_suffix(e->frequency_hz, suffix, sizeof(suffix)));
    label(text, 40, 480, kMuted, 2);
  }
  const uint8_t rp = g_snapshot->replay_state;
  const int clip = clip_for_event(sv.seq);
  label(rp == 1 ? "Reading the clip and checking its hash" :
        rp == 2 ? "PLAYING through the speaker; the cursor follows the clip" :
        rp == 3 ? g_snapshot->replay_note :
        clip >= 0 ? "Plays only if its hash matches the card." : "No clip for this event.", 40, 508,
        rp == 2 ? kGreen : rp == 3 ? kYellow : kMuted, 2);
  label("Tap the plot to move the cursor. Samples are 0.25 s apart.", 40, 536, kMuted, 2);
  button(rp == 2 ? "STOP" : rp == 1 ? "LOADING..." : "PLAY CLIP", kScPlayX, kOvBtnY, kScPlayW, rp == 2 ? kRed : kGreen,
         rp == 2 || (clip >= 0 && rp != 1));
  if (clip >= 0 && !sv.cursor_valid && rp == 2) label("Clip is anchored to an earlier event", kScPlayX + kScPlayW + 20, kOvBtnY + 26, kMuted, 2);
}

constexpr int kCtlBtnX = 50, kCtlBtnW = 300;
void control() {
  overlay_frame("CONTROL RUN  /  FALSE-ALARM CHECK");
  const ControlView& c = g_snapshot->control;
  label("Run this when there is nothing to detect: antenna unplugged,", 40, 192, TFT_WHITE, 2);
  label("a dummy load, or an empty channel. It counts what each sensitivity", 40, 220, TFT_WHITE, 2);
  label("would have logged. Automatic detection pauses while it runs; MARK still works.", 40, 248, TFT_WHITE, 2);
  const char* status = c.state == 1 ? "RUNNING" : c.state == 2 ? "COMPLETE" : c.state == 3 ? "STOPPED EARLY" : "READY";
  label(status, 40, 304, c.state == 1 ? kYellow : c.state == 2 ? kGreen : c.state == 3 ? kYellow : kCyan, 3);
  char text[112], a[16], b[16];
  if (c.state) {
    format_duration(c.elapsed_s, a, sizeof(a));
    format_duration(c.duration_s, b, sizeof(b));
    std::snprintf(text, sizeof(text), "%s of %s", a, b);
    label(text, 360, 304, TFT_WHITE, 2);
  }
  g.fillRect(40, 336, 700, 18, plot_bg());
  g.drawRoundRect(40, 336, 700, 18, 0, plot_grid());
  if (c.state && c.duration_s)
    g.fillRect(41, 337, static_cast<int>(698ull * std::min(c.elapsed_s, c.duration_s) / c.duration_s), 16, c.state == 1 ? kYellow : kGreen);
  label("SENSITIVITY", 40, 384, kMuted, 2);
  label("LIMIT", 300, 384, kMuted, 2);
  label("ALARMS", 440, 384, kMuted, 2);
  label("PER HOUR", 600, 384, kMuted, 2);
  static constexpr const char* kNames[3] = {"QUIET", "STANDARD", "SENSITIVE"};
  for (int i = 0; i < 3; ++i) {
    const int y = 418 + i * 32;
    label(kNames[i], 40, y, TFT_WHITE, 2);
    std::snprintf(text, sizeof(text), "+%.0f dB", static_cast<double>(rf_rise_threshold_db(static_cast<Sensitivity>(i))));
    label(text, 300, y, kMuted, 2);
    if (c.state) {
      std::snprintf(text, sizeof(text), "%lu", static_cast<unsigned long>(c.count[i]));
      label(text, 440, y, TFT_WHITE, 2);
      if (c.rate_ok) std::snprintf(text, sizeof(text), "%.0f", static_cast<double>(c.per_hour[i]));
      else std::snprintf(text, sizeof(text), "--");
      label(text, 600, y, c.rate_ok ? kGreen : kMuted, 2);
    } else {
      label("--", 440, y, kMuted, 2);
      label("--", 600, y, kMuted, 2);
    }
  }
  if (c.state) {
    format_duration(c.observed_s, a, sizeof(a));
    std::snprintf(text, sizeof(text), "Measured %s. Retunes and a lost receiver are not counted.", a);
    label(text, 40, 526, kMuted, 2);
    if (!c.rate_ok) label("A per-hour rate needs at least 1 minute measured.", 40, 498, kYellow, 2);
  }
  label("A real event is worth a look only", 800, 418, kMuted, 2);
  label("if its sensitivity fires rarely", 800, 446, kMuted, 2);
  label("on the empty setup (rate at left).", 800, 474, kMuted, 2);
  const bool running = c.state == 1;
  button(running ? "STOP RUN" : c.state == 0 ? "START 5 MIN" : "RUN AGAIN", kCtlBtnX, kOvBtnY, kCtlBtnW, running ? kRed : kGreen,
         running || g_snapshot->receiver_ready);
  if (!running && !g_snapshot->receiver_ready) label("Needs a running receiver.", kCtlBtnX + kCtlBtnW + 20, kOvBtnY + 26, kYellow, 2);
}

void session() {
  overlay_frame("SESSION CARD");
  const SessionSummary& s = g_snapshot->summary;
  char value[64], span[24];
  int y = 196;
  const auto row = [&](const char* name, const char* text, uint16_t color = TFT_WHITE) {
    label(name, 40, y, kMuted, 2);
    label(text, 400, y, color, 2);
    y += 34;
  };
  format_duration(s.elapsed_s, span, sizeof(span));
  row("ELAPSED", span);
  format_duration(s.measured_s, span, sizeof(span));
  std::snprintf(value, sizeof(value), "%s of level measured", span);
  row("MEASURED", value);
  std::snprintf(value, sizeof(value), "%lu RF   %lu AUDIO   %lu MARKS", static_cast<unsigned long>(s.events_rf),
                static_cast<unsigned long>(s.events_audio), static_cast<unsigned long>(s.events_marks));
  row("EVENTS", value);
  if (s.elapsed_s >= 600) std::snprintf(value, sizeof(value), "%.1f findings per hour", static_cast<double>(s.events_rf + s.events_audio) * 3600.0 / s.elapsed_s);
  else std::snprintf(value, sizeof(value), "after 10 minutes");
  row("RATE", value, s.elapsed_s >= 600 ? TFT_WHITE : kMuted);
  if (s.have_level) {
    std::snprintf(value, sizeof(value), "%.1f dBFS", static_cast<double>(s.level_max_dbfs));
    row("LOUDEST RF", value);
    std::snprintf(value, sizeof(value), "%.1f dBFS", static_cast<double>(s.level_min_dbfs));
    row("QUIETEST RF", value);
  } else {
    row("LOUDEST RF", "--", kMuted);
    row("QUIETEST RF", "--", kMuted);
  }
  format_duration(s.longest_quiet_s, span, sizeof(span));
  row("LONGEST QUIET", span);
  std::snprintf(value, sizeof(value), "%u%s", static_cast<unsigned>(s.channels), s.channels >= 32 ? "+" : "");
  row("CHANNELS TUNED", value);
  std::snprintf(value, sizeof(value), "%lu sweeps   %lu clips saved", static_cast<unsigned long>(s.sweeps), static_cast<unsigned long>(s.clips));
  row("SWEEPS, CLIPS", value);
  label("Only counts of what the receiver", 800, 230, kMuted, 2);
  label("measured and what was logged.", 800, 258, kMuted, 2);
  label("No score, no interpretation.", 800, 286, kMuted, 2);
}

void paint_page() {
  if (g_overlay != Overlay::none) {
    if (g_overlay == Overlay::scrub) scrub();
    else if (g_overlay == Overlay::control) control();
    else session();
    return;
  }
  switch (g_page) {
    case Page::overview: overview(); break;
    case Page::spectrum: spectrum(); break;
    case Page::audio: audio(); break;
    case Page::environment: environment(); break;
    case Page::events: events(); break;
    case Page::review: review(); break;
    default: break;
  }
}
void paint_direct() {
  g.t = &M5.Display;
  g.oy = 0;
  M5.Display.fillRect(0, kBodyY, 1280, kBodyBottom - kBodyY + 1, kBg);
  paint_page();
  if (g_page == Page::spectrum)  // the waterfall scrolls inside this rectangle, like the LoRa dashboard
    M5.Display.setScrollRect(kWfX + 1, kWfY + 1, kWfW - 2, kWfH - 2, plot_bg());
  if (g_page == Page::audio)  // the envelope rolls inside this one
    M5.Display.setScrollRect(kAuX + 1, kAuY + 1, kAuW - 2, kAuH - 2, plot_bg());
}
// page_change: another screen is replacing this one, so most pixels differ and tile pushes would not help.
void draw_body(bool page_change = false) {
  if (g_info_open) return;  // the popup owns the body until it is closed
  M5Canvas* canvas = body_canvas();
  if (!canvas) { paint_direct(); g_fx_dirty = true; return; }
  const uint32_t t0 = now_us();
  g.t = canvas;
  g.oy = kBodyY;
  canvas->fillSprite(kBg);
  paint_page();
  g.t = &M5.Display;
  g.oy = 0;
  const uint32_t t1 = now_us();
  acars_ui::DirtyRect rects[kMaxPushRects];
  size_t count = 0;
  bool whole = true;
  if (!page_change && g_front && g_front_valid) {
    const uint16_t* next = static_cast<const uint16_t*>(canvas->getBuffer());
    const uint16_t* previous = static_cast<const uint16_t*>(g_front->getBuffer());
    if (next && previous) {
      bool overflow = false;
      count = acars_ui::diff_rects(previous, next, 1280, kBodyHeight, rects, kMaxPushRects, &overflow);
      const int64_t area = acars_ui::rect_area(rects, count);
      whole = overflow || area * 100 > static_cast<int64_t>(1280) * kBodyHeight * kWholeFramePercent;
      g_stats.pixels = static_cast<uint32_t>(area);
    }
  }
  const uint32_t t2 = now_us();
  if (whole) {
    paint_direct();  // native primitives straight to the panel, like the other dashboards
    ++g_stats.whole_pushes;
    g_stats.pixels = 1280u * kBodyHeight;
  } else {
    for (size_t i = 0; i < count; ++i) {
      M5.Display.setClipRect(rects[i].x, kBodyY + rects[i].y, rects[i].w, rects[i].h);
      canvas->pushSprite(0, kBodyY);
    }
    M5.Display.clearClipRect();
    ++g_stats.partial_pushes;
  }
  const uint32_t t3 = now_us();
  g_fx_dirty = true;  // the orb is drawn over the body, so it repaints after every body refresh
  g_stats.compose_us = t1 - t0;
  g_stats.diff_us = t2 - t1;
  g_stats.push_us = t3 - t2;
  g_stats.rects = whole ? 1u : static_cast<uint32_t>(count);
  g_stats.push_us_max = std::max(g_stats.push_us_max, g_stats.push_us);
  if (g_front) {
    std::swap(g_canvas, g_front);  // the frame now on the panel becomes the reference
    g_front_valid = true;
  }
}
} // namespace

void audio_text(int x, int y, int w, const char* text, uint16_t color);

bool draw_spectrum_frame(const float* dbfs, size_t count, uint32_t epoch, uint32_t now_ms) {
  if (g_info_open || !g_active || !g_scope || !g_snapshot || !dbfs || count < 2) return false;
  if (!screens::owns(screens::Id::dead_air) || g_page != Page::spectrum || g_overlay != Overlay::none) return false;
  const uint32_t t0 = now_us();
  Scope& s = *g_scope;
  const size_t n = std::min(count, kTracePoints);
  const uint16_t bg = plot_bg();
  const int x0 = kPlotX + 1, w = kPlotW - 2, bottom = kPlotY + kPlotH - 2;
  const bool new_epoch = !s.have_epoch || epoch != s.last_epoch;
  if (new_epoch) s.trail.reset();  // a retune or setting change breaks the peak hold
  const float dt = s.last_frame_ms && now_ms > s.last_frame_ms ? static_cast<float>(now_ms - s.last_frame_ms) / 1000.0f : 0.0f;
  s.trail.update(dbfs, n, kTrailDecayDbPerS * dt);
  int16_t xs[kTracePoints], ys[kTracePoints], ts[kTracePoints];
  for (size_t i = 0; i < n; ++i) {
    xs[i] = static_cast<int16_t>(x0 + static_cast<int>(i) * (w - 1) / static_cast<int>(n - 1));
    ys[i] = static_cast<int16_t>(level_y(dbfs[i], kPlotY, kPlotH));
    ts[i] = static_cast<int16_t>(level_y(s.trail.at(i), kPlotY, kPlotH));
  }
  M5.Display.startWrite();
  if (s.prev_n == n) {  // erase last frame's drawing, as the LoRa and Home scopes do
    if (s.pk_valid) M5.Display.fillRect(s.pk_x - 6, s.pk_y - 11, 13, 12, bg);  // the peak marker
    for (size_t i = 0; i < n; ++i)
      if (bottom > s.prev_y[i] + 2) M5.Display.drawFastVLine(s.prev_x[i], s.prev_y[i] + 2, bottom - s.prev_y[i] - 1, bg);
    for (size_t i = 1; i < n; ++i) {
      for (int dy = -1; dy <= 1; ++dy) M5.Display.drawLine(s.prev_x[i - 1], s.prev_y[i - 1] + dy, s.prev_x[i], s.prev_y[i] + dy, bg);
      M5.Display.drawLine(s.prev_x[i - 1], s.prev_trail_y[i - 1], s.prev_x[i], s.prev_trail_y[i], bg);
    }
  } else {
    M5.Display.fillRect(x0, kPlotY + 1, w, kPlotH - 2, bg);
    s.pk_valid = false;
  }
  draw_plot_grid(M5.Display);
  const uint16_t curtain = viz::rgb565(26, 10, 60);
  for (size_t i = 1; i < n; ++i)  // fading afterimage (peak hold), behind the live trace
    M5.Display.drawLine(xs[i - 1], ts[i - 1], xs[i], ts[i], viz::color_at(s.lut, s.trail.at(i) - 10.0f));
  for (size_t i = 0; i < n; ++i)
    if (bottom > ys[i] + 2) M5.Display.drawFastVLine(xs[i], ys[i] + 2, bottom - ys[i] - 1, curtain);
  for (size_t i = 1; i < n; ++i) {
    const float t = viz::normalize(dbfs[i]);
    const uint16_t halo = s.lut[static_cast<int>(t * 110.0f)];
    const uint16_t core = s.lut[std::min(255, 190 + static_cast<int>(t * 65.0f))];
    M5.Display.drawLine(xs[i - 1], ys[i - 1] - 1, xs[i], ys[i] - 1, halo);
    M5.Display.drawLine(xs[i - 1], ys[i - 1] + 1, xs[i], ys[i] + 1, halo);
    M5.Display.drawLine(xs[i - 1], ys[i - 1], xs[i], ys[i], core);
  }
  for (size_t i = 0; i < n; ++i) { s.prev_x[i] = xs[i]; s.prev_y[i] = ys[i]; s.prev_trail_y[i] = ts[i]; }
  s.prev_n = n;
  // Peak marker and readout: the strongest point of the trace, skipping the centre where the tuner's own DC spike sits.
  size_t pk = 0;
  float best = -1e9f;
  const size_t centre = n / 2;
  for (size_t i = 0; i < n; ++i) {
    if (i + 2 >= centre && i <= centre + 2) continue;
    if (dbfs[i] > best) { best = dbfs[i]; pk = i; }
  }
  if (best > -1e8f) {
    const int mx = xs[pk], ty = std::max(kPlotY + 13, static_cast<int>(ys[pk]) - 3);
    M5.Display.fillTriangle(mx, ty, mx - 5, ty - 10, mx + 5, ty - 10, kYellow);
    s.pk_x = static_cast<int16_t>(mx);
    s.pk_y = static_cast<int16_t>(ty);
    s.pk_valid = true;
    if ((now_ms - s.pk_text_ms >= 250 || s.pk_text_ms == 0) && g_snapshot->frequency_hz && g_snapshot->sample_rate_sps) {
      s.pk_text_ms = now_ms;
      char text[56];
      const double hz = static_cast<double>(g_snapshot->frequency_hz) +
                        (static_cast<double>(pk) / static_cast<double>(n - 1) - 0.5) * static_cast<double>(g_snapshot->sample_rate_sps);
      std::snprintf(text, sizeof(text), "PEAK %.3f MHz  %.1f dBFS", hz / 1e6, static_cast<double>(best));
      audio_text(620, 144, 360, text, kYellow);
    }
  }
  // Waterfall: a marker row at every retune/setting change so continuity is never implied, then one measured row. While
  // the sweep's band map is shown in its place, the waterfall is not drawn.
  if (!map_shown() && (now_ms - s.last_row_ms >= kWaterfallRowMs || new_epoch)) {
    if (s.have_epoch && new_epoch) {
      viz::render_gap_row(s.row, kWfW - 2);
      waterfall_push_row(s);
    }
    viz::render_row(dbfs, n, s.lut, s.row, kWfW - 2);
    waterfall_push_row(s);
    s.last_row_ms = now_ms;
  }
  M5.Display.endWrite();
  s.last_epoch = epoch;
  s.have_epoch = true;
  s.last_frame_ms = now_ms;
  const uint32_t us = now_us() - t0;
  g_stats.frame_us = us;
  g_stats.frame_us_max = std::max(g_stats.frame_us_max, us);
  ++g_stats.frames;
  return true;
}

void audio_text(int x, int y, int w, const char* text, uint16_t color) {
  M5.Display.fillRect(x, y - 11, w, 22, kPanel);
  M5.Display.setTextDatum(middle_left);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(color);
  M5.Display.drawString(text, x, y);
}

// One envelope bar, mirrored about the centre line, in the column [x, x + kAuStep). Colour rises with the measured level.
void draw_audio_bar(const Scope& s, int x, float norm) {
  const uint16_t bg = plot_bg();
  const int cy = kAuY + kAuH / 2;
  const float t = std::clamp(norm, 0.0f, 1.0f);
  const int h = audio_half(t);
  M5.Display.fillRect(x, kAuY + 1, kAuStep, kAuH - 2, bg);
  for (int db = -20; db >= -60; db -= 20) {
    const int gh = audio_half((static_cast<float>(db) - kAudioFloorDb) / -kAudioFloorDb);
    M5.Display.drawFastHLine(x, cy - gh, kAuStep, plot_grid());
    M5.Display.drawFastHLine(x, cy + gh, kAuStep, plot_grid());
  }
  M5.Display.drawFastHLine(x, cy, kAuStep, plot_grid());
  if (h > 0) {
    M5.Display.fillRect(x, cy - h, kAuStep - 1, 2 * h + 1, s.lut[static_cast<int>(t * 110.0f)]);
    const uint16_t cap = s.lut[std::min(255, 150 + static_cast<int>(t * 105.0f))];
    M5.Display.fillRect(x, cy - h, kAuStep - 1, 2, cap);
    M5.Display.fillRect(x, cy + h - 1, kAuStep - 1, 2, cap);
  }
}

bool draw_audio_frame(const AudioView& v, uint32_t now_ms) {
  if (g_info_open || !g_active || !g_scope || !g_snapshot) return false;
  if (!screens::owns(screens::Id::dead_air) || g_page != Page::audio || g_overlay != Overlay::none) return false;
  const uint32_t t0 = now_us();
  Scope& s = *g_scope;
  const uint16_t bg = plot_bg();
  const int cy = kAuY + kAuH / 2;
  const int right = kAuX + kAuW - 1 - kAuStep;  // x of the newest bar
  char text[48];
  M5.Display.startWrite();
  if (!v.flowing || !v.envelope || v.count < 2) {
    if (s.a_state != 0) {  // one plain notice; no trace is invented
      M5.Display.fillRect(kAuX + 1, kAuY + 1, kAuW - 2, kAuH - 2, bg);
      draw_audio_grid(M5.Display, 0);
      M5.Display.setTextDatum(middle_left);
      M5.Display.setTextSize(2);
      M5.Display.setTextColor(kYellow);
      M5.Display.drawString("NO DEMODULATED AUDIO", kAuX + 20, cy - 14);
      M5.Display.setTextColor(kMuted);
      M5.Display.drawString("Receiver idle or between channels.", kAuX + 20, cy + 14);
      audio_text(300, 470, 300, "--", kMuted);
      audio_text(300, 515, 300, "--", kMuted);
      audio_text(300, 560, 300, "--", kMuted);
      audio_text(829, 515, 410, "AUDIO: NONE", kMuted);
      s.a_state = 0;
    }
    M5.Display.endWrite();
    return true;
  }
  if (s.a_state != 1) {
    // First frame after the tab opens or audio returns: start clean and seed the recent history from the envelope.
    M5.Display.fillRect(kAuX + 1, kAuY + 1, kAuW - 2, kAuH - 2, bg);
    draw_audio_grid(M5.Display, 0);
    const size_t n = std::min<size_t>(v.count, static_cast<size_t>((kAuW - 2) / kAuStep));
    for (size_t i = 0; i < n; ++i)
      draw_audio_bar(s, right - static_cast<int>(n - 1 - i) * kAuStep, v.envelope[v.count - n + i]);
    M5.Display.setScrollRect(kAuX + 1, kAuY + 1, kAuW - 2, kAuH - 2, bg);
    s.a_state = 1;
  } else {
    // Roll the plot left with the panel's own scroll and draw only the new bars on the right: nothing is erased and
    // redrawn, so there is no flicker.
    const size_t fresh = v.fresh ? std::min<size_t>(v.fresh_count, 8) : 0;
    for (size_t i = 0; i < fresh; ++i) {
      M5.Display.scroll(-kAuStep, 0);
      draw_audio_bar(s, right, v.fresh[i]);
    }
  }
  // Numbers refresh at about 4 Hz so they stay steady to read.
  if (now_ms - s.a_text_ms >= 250 || s.a_text_ms == 0) {
    s.a_text_ms = now_ms;
    std::snprintf(text, sizeof(text), "%.1f dBFS", static_cast<double>(v.rms_dbfs));
    audio_text(300, 470, 300, text, TFT_WHITE);
    std::snprintf(text, sizeof(text), "%.1f dBFS", static_cast<double>(v.peak_dbfs));
    audio_text(300, 515, 300, text, TFT_WHITE);
    if (v.baseline_ready) std::snprintf(text, sizeof(text), "%.1f dBFS", static_cast<double>(v.baseline_dbfs));
    else std::snprintf(text, sizeof(text), "WARMING UP");
    audio_text(300, 560, 300, text, v.baseline_ready ? TFT_WHITE : kYellow);
    audio_text(829, 515, 410, "AUDIO: FLOWING", kGreen);
  }
  M5.Display.endWrite();
  const uint32_t us = now_us() - t0;
  g_stats.frame_us = us;
  g_stats.frame_us_max = std::max(g_stats.frame_us_max, us);
  ++g_stats.frames;
  return true;
}

void attach_events(EventModel* events) { g_events = events; }
void enter(const Snapshot& snapshot) {
  if (!screens::is_active(screens::Id::dead_air)) return;
  if (!g_snapshot) g_snapshot = new (std::nothrow) Snapshot();
  if (!g_snapshot) return;
  *g_snapshot = snapshot;
  sanitize_snapshot();
  g_page = Page::overview;
  g_info_open = false;
  g_overlay = Overlay::none;
  g_scrub_cursor = -1;
  g_env_cursor = -1;
  g_env_window = -1;
  g_map_pref = true;
  g_prev_sweeping = false;
  g_fx.reset();
  g_fx_dirty = true;
  ev_reset();
  g_active = true;
  scope_alloc();
  chrome();
  draw_body(true);
}
void update(const Snapshot& snapshot) {
  if (!g_active || !screens::owns(screens::Id::dead_air) ||
      snapshot.revision == g_snapshot->revision) return;
  const int battery = g_snapshot->battery_percent;
  const bool sound = g_snapshot->sound_enabled;
  const uint8_t mic = g_snapshot->mic_state;
  *g_snapshot = snapshot;
  sanitize_snapshot();
  if (g_scope) {  // a lost or settling stream erases the trace; the waterfall history stays
    const bool live = g_snapshot->receiver_ready && g_snapshot->spectrum_valid;
    if (!live && g_scope->was_valid && g_page == Page::spectrum) scope_erase_trace();
    g_scope->was_valid = live;
  }
  if (sound != snapshot.sound_enabled) audio_header::draw_mute_button(snapshot.sound_enabled);
  if (battery != snapshot.battery_percent) audio_header::draw_battery(snapshot.battery_percent);
  if (mic != snapshot.mic_state) { mic_banner(); g_fx_dirty = true; }
  if (g_snapshot->sweeping && !g_prev_sweeping) g_map_pref = true;  // a new sweep shows its map as it fills in
  g_prev_sweeping = g_snapshot->sweeping;
  draw_body();
}
void leave() {
  g_active = false;
  g_info_open = false;
  g_overlay = Overlay::none;
  release_canvas();
  scope_release();
  for (M5Canvas** c : {&g_fx_big, &g_fx_beacon}) {
    if (*c) { (*c)->deleteSprite(); delete *c; *c = nullptr; }
  }
}
bool active() { return g_active; }
PaintStats paint_stats() { return g_stats; }
Page page() { return g_page; }
Overlay overlay() { return g_overlay; }

// ---- The orb ---------------------------------------------------------------------------------------------------------
void fx_event(const Event& event, uint32_t now_ms) {
  FxKind kind = FxKind::none;
  if (event.kind == Kind::rf_rise) kind = FxKind::rf;
  else if (event.kind == Kind::audio_burst) kind = FxKind::audio;
  else if (event.kind == Kind::manual_marker) kind = FxKind::mark;
  g_fx.trigger(kind, event.value - event.reference, event.sequence, now_ms);
}
void fx_idle(bool live, float proximity) {
  if (g_fx.live() != live) g_fx_dirty = true;  // losing the receiver dims the orb even when nothing else changed
  g_fx.set_idle(live, proximity);
}

namespace {
M5Canvas* make_fx_canvas(int size) {
  M5Canvas* c = new (std::nothrow) M5Canvas(&M5.Display);
  if (!c) return nullptr;
  c->setColorDepth(16);
  c->setPsram(true);
  if (!c->createSprite(size, size)) { delete c; return nullptr; }
  return c;
}
void render_orb(M5Canvas& c, int size, uint16_t bg, const FxFrame& f) {
  c.fillSprite(bg);
  const int cx = size / 2, cy = size / 2, half = size / 2 - 2;
  if (!f.live && !f.burst) {  // nothing is being measured: a dim, still ring
    c.drawCircle(cx, cy, std::max(3, static_cast<int>(f.core * static_cast<float>(half))), kEdge);
    c.fillCircle(cx, cy, 2, kMuted);
    return;
  }
  const uint16_t accent = f.burst ? fx_color(f.kind) : fx_color(FxKind::none);
  for (uint8_t i = 0; i < f.ring_count && i < kFxRings; ++i) {
    if (f.ring_a[i] <= 0.02f) continue;
    const int r = std::max(2, static_cast<int>(f.ring_r[i] * static_cast<float>(half)));
    const uint16_t ring = blend565(accent, bg, f.ring_a[i]);
    c.drawCircle(cx, cy, r, ring);
    if (size >= 100) c.drawCircle(cx, cy, r - 1, ring);
  }
  const int core = std::max(2, static_cast<int>(f.core * static_cast<float>(half)));
  const int step = size >= 100 ? 4 : 2;
  for (int k = 3; k >= 1; --k) c.fillCircle(cx, cy, core + k * step, blend565(accent, bg, f.glow * 0.22f / static_cast<float>(k)));
  c.fillCircle(cx, cy, core, blend565(accent, bg, 0.45f + 0.55f * f.glow));
  if (f.glow > 0.55f) c.fillCircle(cx, cy, std::max(1, core / 2), blend565(TFT_WHITE, accent, (f.glow - 0.55f) / 0.45f));
  const int dot = size >= 100 ? 2 : 1;
  const uint16_t spark = blend565(TFT_WHITE, accent, 0.5f);
  for (uint8_t i = 0; i < f.spark_count; ++i) {
    if (f.spark_a[i] <= 0.03f) continue;
    c.fillCircle(cx + static_cast<int>(f.spark_x[i] * static_cast<float>(half)),
                 cy + static_cast<int>(f.spark_y[i] * static_cast<float>(half)), dot, blend565(spark, bg, f.spark_a[i]));
  }
}
}  // namespace

bool draw_fx(uint32_t now_ms) {
  if (!g_active || !g_snapshot || !screens::owns(screens::Id::dead_air)) return false;
  const bool animating = g_fx.animating(now_ms);
  const FxFrame f = g_fx.frame(now_ms);
  // A burst runs at about 15 frames a second, the idle breath at 4, and a still orb is drawn only after the body under
  // it was repainted.
  if (!g_fx_dirty && (!animating && !f.live)) return false;
  if (!g_fx_dirty && now_ms - g_fx_last_ms < (animating ? 66u : 250u)) return false;
  g_fx_last_ms = now_ms;
  g_fx_dirty = false;
  bool painted = false;
  if (!g_fx_beacon) g_fx_beacon = make_fx_canvas(kFxBeaconS);
  if (g_fx_beacon) {
    render_orb(*g_fx_beacon, kFxBeaconS, g_snapshot->mic_state ? kPanel : kBg, f);  // inside the mic banner when it shows
    g_fx_beacon->pushSprite(kFxBeaconX, kFxBeaconY);
    painted = true;
  }
  if (g_page == Page::overview && !g_info_open && g_overlay == Overlay::none) {
    if (!g_fx_big) g_fx_big = make_fx_canvas(kFxBigS);
    if (g_fx_big) {
      render_orb(*g_fx_big, kFxBigS, kPanel, f);
      g_fx_big->pushSprite(kFxBigX, kFxBigY);
      painted = true;
    }
  }
  return painted;
}
void close_info() {
  g_info_open = false;
  if (g_scope) { g_scope->a_state = -1; g_scope->a_prev_n = 0; g_scope->prev_n = 0; }  // the plots repaint from scratch
  draw_info_button();
  draw_body(true);
}
// Another tab replaces this one: the live plots repaint from scratch and the body is redrawn whole.
static void switch_page(Page page) {
  g_page = page;
  g_overlay = Overlay::none;
  g_scrub_cursor = -1;
  if (g_scope) { g_scope->a_state = -1; g_scope->a_prev_n = 0; g_scope->prev_n = 0; }
  nav();
  draw_body(true);
}
Action handle_touch(int32_t x, int32_t y, uint32_t now_ms) {
  if (!g_active || !screens::owns(screens::Id::dead_air)) return {};
  if (audio_header::home_hit(x, y)) return {ActionKind::close};
  if (hit(x, y, kInfoBtnX, kInfoBtnY, kInfoBtnW, kInfoBtnH)) {  // the (i) button works on every tab and toggles the popup
    if (g_info_open) close_info();
    else {
      g_info_open = true;
      g_info_page = 0;
      draw_info_button();
      draw_info_panel();
    }
    return {};
  }
  if (g_info_open) {
    if (hit(x, y, kInfoCloseX, kInfoBtnRowY, kInfoBtnRowW, 52) ||
        !hit(x, y, kInfoPanelX, kInfoPanelY, kInfoPanelW, kInfoPanelH)) {
      close_info();
    } else if (hit(x, y, kInfoPrevX, kInfoBtnRowY, kInfoBtnRowW, 52) && g_info_page > 0) {
      --g_info_page;
      draw_info_panel();
    } else if (hit(x, y, kInfoNextX, kInfoBtnRowY, kInfoBtnRowW, 52) && g_info_page + 1 < info_page_count()) {
      ++g_info_page;
      draw_info_panel();
    }
    return {};
  }
  if (audio_header::mute_hit(x, y)) return {ActionKind::toggle_mute};
  if (audio_header::settings_hit(x, y)) return {ActionKind::open_settings};
  if (y >= kTabY && y < kTabY + kTabHeight) {
    for (uint8_t i = 0; i < static_cast<uint8_t>(Page::count); ++i) {
      if (hit(x, y, kTabX + i * (kTabWidth + kTabGap), kTabY, kTabWidth, kTabHeight)) {
        if (g_page != static_cast<Page>(i)) {
          switch_page(static_cast<Page>(i));
          return {};
        }
        if (g_overlay == Overlay::none) return {};
        // A tap on the current page's tab dismisses the open overlay, like its CLOSE button.
        const Overlay closed = g_overlay;
        g_overlay = Overlay::none;
        g_scrub_cursor = -1;
        if (g_scope) { g_scope->a_state = -1; g_scope->a_prev_n = 0; g_scope->prev_n = 0; }
        draw_body(true);
        return closed == Overlay::scrub ? Action{ActionKind::scrub_close, 0} : Action{};
      }
    }
  }
  if (g_overlay != Overlay::none) {
    if (hit(x, y, kOvCloseX, kOvCloseY, kOvCloseW, 52)) {
      const Overlay closed = g_overlay;
      g_overlay = Overlay::none;
      g_scrub_cursor = -1;
      if (g_scope) { g_scope->a_state = -1; g_scope->a_prev_n = 0; g_scope->prev_n = 0; }  // the page's live plots repaint
      draw_body(true);
      return closed == Overlay::scrub ? Action{ActionKind::scrub_close, 0} : Action{};
    }
    if (g_overlay == Overlay::scrub) {
      const ScrubView& sv = g_snapshot->scrub;
      const uint8_t rp = g_snapshot->replay_state;
      if (hit(x, y, kScPlayX, kOvBtnY, kScPlayW, 52)) {
        if (rp == 2) return {ActionKind::stop_replay, 0};
        if (rp != 1 && clip_for_event(sv.seq) >= 0) return {ActionKind::play_event_clip, static_cast<int32_t>(sv.seq)};
        return {};
      }
      if (sv.found && !sv.cursor_valid && hit(x, y, kScX, kScRfY, kScW, kScAuY + kScAuH - kScRfY)) {
        g_scrub_cursor = static_cast<int>(static_cast<int64_t>(x - kScX) * static_cast<int64_t>(kTraceSamples - 1) / (kScW - 1));
        g_scrub_cursor = std::max(0, std::min(g_scrub_cursor, static_cast<int>(kTraceSamples) - 1));
        draw_body();
      }
    } else if (g_overlay == Overlay::control) {
      const ControlView& c = g_snapshot->control;
      if (hit(x, y, kCtlBtnX, kOvBtnY, kCtlBtnW, 52)) {
        if (c.state == 1) return {ActionKind::control_stop, 0};
        if (g_snapshot->receiver_ready) return {ActionKind::control_start, 0};
      }
    }
    return {};  // an overlay owns the body: nothing under it can be tapped
  }
  if (g_page == Page::overview && hit(x, y, kSessionBtnX, kSessionBtnY, kSessionBtnW, 52)) {
    g_overlay = Overlay::session;
    draw_body(true);
    return {};
  }
  if (g_page == Page::environment) {
    for (int i = 0; i < 3; ++i) {
      if (!hit(x, y, kEnWinX0 + i * kEnWinDx, kEnWinY, kEnWinW, kEnWinH)) continue;
      g_env_window = i;
      g_env_cursor = -1;  // a column means a different moment in another window
      draw_body();
      return {};
    }
    if (hit(x, y, kEnChX, kEnChTop[0], kEnChW, kEnChTop[2] + kEnChH - kEnChTop[0])) {
      const int col = x - kEnChX;
      g_env_cursor = (g_env_cursor >= 0 && std::abs(col - g_env_cursor) <= 12) ? -1 : col;
      draw_body();
      return {};
    }
  }
  if (g_page == Page::spectrum && g_snapshot->map.count > 0 && hit(x, y, kChipX, kChipY, kChipW, kChipH)) {
    g_map_pref = !g_map_pref;  // band map <-> live waterfall
    if (g_scope) { g_scope->a_state = -1; g_scope->a_prev_n = 0; g_scope->prev_n = 0; }
    draw_body(true);
    return {};
  }
  if (g_page == Page::spectrum && g_snapshot->receiver_ready) {
    if (hit(x, y, 366, kSweepY, 230, 52))
      return {g_snapshot->sweeping ? ActionKind::stop_scan : ActionKind::start_scan, 0};
    if (!g_snapshot->sweeping) {
      if (hit(x, y, 606, kSweepY, 300, 52)) return {ActionKind::sweep_preset, 0};
      if (hit(x, y, 916, kSweepY, 320, 52)) return {ActionKind::sweep_dwell, 0};
      if (hit(x, y, 40, kStepY, 128, 52)) return {ActionKind::tune_down, -1};
      if (hit(x, y, 183, kStepY, 128, 52)) return {ActionKind::tune_up, 1};
      for (int i = 0; i < kModeCount; ++i)
        if (hit(x, y, kModeX0 + (i % 3) * kModeDx, kModeY0 + (i / 3) * kModeDy, kModeW, 52))
          return {ActionKind::set_demod, i};
      // Tap-to-tune: on the band map, the tapped channel; on the live plot or waterfall, the tapped frequency.
      const BandMapView& m = g_snapshot->map;
      if (map_shown() && hit(x, y, kWfX + 1, kWfY, kWfW - 2, kWfH) && m.step_hz) {
        const int idx = std::min<int>(m.count - 1, std::max<int>(0, (static_cast<int>(x) - (kWfX + 1)) * m.count / (kWfW - 2)));
        return {ActionKind::tune_to, static_cast<int32_t>(m.start_hz + static_cast<uint32_t>(idx) * m.step_hz)};
      }
      if ((hit(x, y, kPlotX + 1, kPlotY, kPlotW - 2, kPlotH) || (!map_shown() && hit(x, y, kWfX + 1, kWfY, kWfW - 2, kWfH))) &&
          g_snapshot->frequency_hz && g_snapshot->sample_rate_sps) {
        const double frac = static_cast<double>(x - (kPlotX + 1)) / static_cast<double>(kPlotW - 2);
        const double hz = static_cast<double>(g_snapshot->frequency_hz) +
                          (std::min(1.0, std::max(0.0, frac)) - 0.5) * static_cast<double>(g_snapshot->sample_rate_sps);
        const double rounded = std::floor(hz / 1000.0 + 0.5) * 1000.0;  // tune to the nearest kHz
        if (rounded > 0.0 && rounded < 2.0e9) return {ActionKind::tune_to, static_cast<int32_t>(rounded)};
      }
    }
  }
  if (g_page == Page::review && hit(x, y, kClipBtnX, kClipBtnY, kClipBtnW, kClipBtnH)) return {ActionKind::toggle_clips, 0};
  if (g_page == Page::review && hit(x, y, kMicBtnX, kMicBtnY, kMicBtnW, kMicBtnH)) return {ActionKind::toggle_mic, 0};
  if (g_page == Page::review) {
    for (uint8_t i = 0; i < g_snapshot->clip_count && i < kClipInfoCount; ++i)
      if (hit(x, y, kRowX, kRowY0 + i * kRowDy, kRowW, kRowH)) return {ActionKind::select_clip, i};
    const uint8_t rp = g_snapshot->replay_state;
    if (g_snapshot->clip_count && g_snapshot->verify_state != 1 && rp != 1 && rp != 2 &&
        hit(x, y, kVerifyBtnX, kVerifyBtnY, kVerifyBtnW, kVerifyBtnH)) return {ActionKind::verify_clip, 0};
    if (g_snapshot->clip_count && hit(x, y, kPlayBtnX, kPlayBtnY, kPlayBtnW, kPlayBtnH)) {
      if (rp == 2) return {ActionKind::stop_replay, 0};
      if (rp != 1) return {ActionKind::play_clip, 0};
    }
  }
  if (g_page == Page::events && g_events != nullptr) {
    ev_sync();
    const size_t count = g_events->count();
    for (size_t r = 0; r < kEvRows && g_ev_offset + r < count; ++r) {
      if (!hit(x, y, kEvRowX, kEvRowY0 + static_cast<int>(r) * kEvRowDy, kEvRowW, kEvRowH)) continue;
      if (const Event* e = g_events->newest(g_ev_offset + r)) {
        g_ev_has_sel = true;
        g_ev_sel_seq = e->sequence;
        draw_body();
      }
      return {};
    }
    if (hit(x, y, kEvNewerX, kEvNavY, kEvNavW, 52) && g_ev_offset > 0) {
      g_ev_offset = g_ev_offset > kEvRows ? g_ev_offset - kEvRows : 0;
      draw_body(); return {};
    }
    if (hit(x, y, kEvOlderX, kEvNavY, kEvNavW, 52) && g_ev_offset + kEvRows < count) {
      g_ev_offset += kEvRows;
      ev_sync();
      draw_body(); return {};
    }
    if (hit(x, y, kEvScrubBtnX, kEvBtnY, kEvBtnW, 52)) {
      const Event* e = ev_detail();
      if (e && e->source != Source::sweep) {
        g_overlay = Overlay::scrub;
        g_scrub_cursor = -1;
        draw_body(true);
        return {ActionKind::scrub_open, static_cast<int32_t>(e->sequence)};
      }
    }
    if (hit(x, y, kEvClipBtnX, kEvBtnY, kEvBtnW, 52)) {
      const Event* e = ev_detail();
      const int clip = e && e->source != Source::sweep ? clip_for_event(e->sequence) : -1;
      if (clip >= 0) {
        // Show the chosen clip at once; the app confirms the selection in its next snapshot.
        g_snapshot->selected_clip = static_cast<uint8_t>(clip);
        g_snapshot->verify_state = 0;
        switch_page(Page::review);
        return {ActionKind::select_clip, clip};
      }
    }
    if (hit(x, y, kEvCtlBtnX, kEvCtlBtnY, kEvCtlBtnW, 52)) {
      g_overlay = Overlay::control;
      draw_body(true);
      return {};
    }
    if (hit(x, y, kEvChimeBtnX, kEvCtlBtnY, kEvChimeBtnW, 52)) return {ActionKind::toggle_chime, 0};
    if (hit(x, y, 818, kEvMarkY, 420, 52)) {
      g_events->mark_manual(now_ms, g_snapshot->unix_time, g_snapshot->frequency_hz);
      draw_body(); return {};
    }
    if (hit(x, y, 818, 382, 421, 52)) {
      g_events->set_sensitivity(x < 956 ? Sensitivity::quiet :
                                 x < 1094 ? Sensitivity::standard : Sensitivity::sensitive);
      draw_body(); return {};
    }
  }
  return {};
}
bool self_check() {
  return static_cast<uint8_t>(Page::count) == 6 && kTabX > 0 &&
    kTabX + 5 * (kTabWidth + kTabGap) + kTabWidth <= 1280 &&
    kBodyBottom < kTabY && kBodyY >= 110;
}

} // namespace orcsdr::dead_air
