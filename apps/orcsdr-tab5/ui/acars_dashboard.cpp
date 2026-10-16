#include "acars_dashboard.hpp"

#include <M5Unified.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>
#include <utility>

#if defined(ESP_PLATFORM)
#include <esp_timer.h>
#else
#include <chrono>
#endif

#include "acars_dashboard_layout.hpp"
#include "acars_dirty_rects.hpp"
#include "acars_demo_data.hpp"
#include "dashboard_audio_control.hpp"
#include "focus_nav.hpp"

namespace orcsdr::acars_dashboard {
namespace {

using namespace orcsdr::acars_ui;
using layout::Rect;

// The layout's copy of the shared status-bar reserve must equal the real one.
static_assert(layout::kStatusBar.x == audio_header::kStatusBarX && layout::kStatusBar.y == audio_header::kStatusBarY &&
                  layout::kStatusBar.w == audio_header::kStatusBarW && layout::kStatusBar.h == audio_header::kStatusBarH,
              "ACARS layout must reserve exactly the shared status-bar rectangle");

constexpr uint16_t kBg = TFT_BLACK;
constexpr uint16_t kPanel = 0x0861;
constexpr uint16_t kSelected = 0x08a4;
constexpr uint16_t kBorder = 0x2350;
constexpr uint16_t kCyan = 0x04ff;
constexpr uint16_t kGreen = 0x6fe8;
constexpr uint16_t kAmber = 0xfd20;
constexpr uint16_t kRed = 0xf800;
constexpr uint16_t kMuted = 0x9cf3;
constexpr uint16_t kDim = 0x4a69;

enum Font : uint8_t { f18, f24, f40, mono };

Model* g_model = nullptr;
Controller* g_ui = nullptr;
HeaderHook g_header = nullptr;
bool (*g_overlay_open)() = nullptr;
bool g_active = false;
uint32_t g_drawn_ui = 0;
uint32_t g_drawn_snapshot = 0;
uint32_t g_last_draw_ms = 0;
uint32_t g_drawn_bits = 0;     // the demodulated-bit counter as last painted (ACTIVITY page only)

// All drawing goes through this shim so the same code can paint the display directly (header, and the
// fallback if no sprite can be allocated) or an off-screen sprite that holds the whole area below the
// header. The sprite is composed completely and pushed to the display once, so the screen never shows a
// half-drawn or blank frame: that is what removes the flicker. Coordinates are always absolute screen
// coordinates; the shim subtracts the sprite's origin.
class Gfx {
 public:
  void target_display() { t_ = &M5.Display; oy_ = 0; }
  void target_sprite(lgfx::LovyanGFX* sprite, int origin_y) { t_ = sprite; oy_ = origin_y; }
  void setFont(const decltype(&fonts::DejaVu18) f) { t_->setFont(f); }
  void setFont(decltype(nullptr)) { t_->setFont(nullptr); }
  void setTextSize(float v) { t_->setTextSize(v); }
  void setTextDatum(textdatum_t d) { t_->setTextDatum(d); }
  void setTextColor(uint16_t c) { t_->setTextColor(c); }
  int textWidth(const char* s) { return t_->textWidth(s); }
  void drawString(const char* s, int x, int y) { t_->drawString(s, x, y - oy_); }
  void setClipRect(int x, int y, int w, int h) { t_->setClipRect(x, y - oy_, w, h); }
  void clearClipRect() { t_->clearClipRect(); }
  void fillScreen(uint16_t c) { t_->fillScreen(c); }
  void fillRect(int x, int y, int w, int h, uint16_t c) { t_->fillRect(x, y - oy_, w, h, c); }
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) { t_->fillRoundRect(x, y - oy_, w, h, r, c); }
  void drawRoundRect(int x, int y, int w, int h, int r, uint16_t c) { t_->drawRoundRect(x, y - oy_, w, h, r, c); }
  void drawFastHLine(int x, int y, int w, uint16_t c) { t_->drawFastHLine(x, y - oy_, w, c); }
  void drawFastVLine(int x, int y, int h, uint16_t c) { t_->drawFastVLine(x, y - oy_, h, c); }
  void drawLine(int x0, int y0, int x1, int y1, uint16_t c) { t_->drawLine(x0, y0 - oy_, x1, y1 - oy_, c); }
  void drawCircle(int x, int y, int r, uint16_t c) { t_->drawCircle(x, y - oy_, r, c); }
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
    t_->fillTriangle(x0, y0 - oy_, x1, y1 - oy_, x2, y2 - oy_, c);
  }

 private:
  lgfx::LovyanGFX* t_ = &M5.Display;
  int oy_ = 0;
};
Gfx G;

// Everything below the shared header (context strip, content, tab bar) is one sprite: 1280 x 612 x 2 bytes
// = 1.5 MB in PSRAM, allocated when the screen opens and released when it closes.
constexpr int kRegionY = layout::kContext.y - 2;
constexpr int kRegionH = layout::kScreenH - kRegionY;
// Two sprites: a frame is composed in g_canvas (back) and compared with g_front (what the panel shows). Only the tiles
// that differ are pushed; pushing the whole 1.5 MB frame is slow on the rotated panel and blocks touch. After a push the
// two swap roles. Without the second sprite every frame is pushed whole.
M5Canvas* g_canvas = nullptr;
M5Canvas* g_front = nullptr;
bool g_canvas_ok = false;
bool g_front_valid = false;          // g_front holds exactly what is on the panel below the header

uint32_t now_us() {
#if defined(ESP_PLATFORM)
  return static_cast<uint32_t>(esp_timer_get_time());
#else
  return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}

PaintStats g_paint{};
constexpr size_t kMaxPushRects = 24;
constexpr int kWholeFramePercent = 55;     // above this share of changed area one whole push is cheaper than many clipped ones

M5Canvas* make_canvas() {
  M5Canvas* c = new (std::nothrow) M5Canvas(&M5.Display);
  if (!c) return nullptr;
  c->setPsram(true);
  c->setColorDepth(16);
  if (!c->createSprite(layout::kScreenW, kRegionH)) {
    delete c;
    return nullptr;
  }
  return c;
}

bool alloc_canvas() {
  if (g_canvas_ok) return true;
  g_canvas = make_canvas();
  g_canvas_ok = g_canvas != nullptr;        // false: the screen still works, drawing straight to the display
  if (g_canvas_ok && !g_front) g_front = make_canvas();   // optional: without it frames are pushed whole
  g_front_valid = false;
  return g_canvas_ok;
}

void free_canvas() {
  for (M5Canvas** c : {&g_canvas, &g_front}) {
    if (*c) {
      (*c)->deleteSprite();
      delete *c;
      *c = nullptr;
    }
  }
  g_canvas_ok = false;
  g_front_valid = false;
}

// Restricts drawing to a rectangle for its lifetime, so no text or shape can leave its card. Nests: the
// enclosing clip is restored when an inner one ends.
Rect g_clip_outer{};
bool g_clip_outer_valid = false;

class Clip {
 public:
  explicit Clip(const Rect& r) : saved_(g_clip_outer), saved_valid_(g_clip_outer_valid) {
    G.setClipRect(r.x, r.y, r.w, r.h);
    g_clip_outer = r;
    g_clip_outer_valid = true;
  }
  ~Clip() {
    g_clip_outer = saved_;
    g_clip_outer_valid = saved_valid_;
    if (saved_valid_) G.setClipRect(saved_.x, saved_.y, saved_.w, saved_.h);
    else G.clearClipRect();
  }
  Clip(const Clip&) = delete;
  Clip& operator=(const Clip&) = delete;

 private:
  Rect saved_;
  bool saved_valid_;
};

void set_font(Font f) {
  switch (f) {
    case f18: G.setFont(&fonts::DejaVu18); break;
    case f24: G.setFont(&fonts::DejaVu24); break;
    case f40: G.setFont(&fonts::DejaVu40); break;
    default: G.setFont(&fonts::FreeMono12pt7b); break;
  }
  G.setTextSize(1);
}

void text(const char* value, int x, int y, uint16_t color, Font f = f18, textdatum_t datum = middle_left) {
  set_font(f);
  G.setTextDatum(datum);
  G.setTextColor(color);
  G.drawString(value, x, y);
  G.setFont(nullptr);   // the shared header helpers draw with the built-in font
}

// Draws `value` clipped to `max_w`, ending in "..." when it had to be shortened.
void fit(const char* value, int x, int y, int max_w, uint16_t color, Font f = f18, textdatum_t datum = middle_left) {
  char line[160];
  std::snprintf(line, sizeof(line), "%s", value);
  set_font(f);
  size_t n = std::strlen(line);
  if (G.textWidth(line) > max_w) {
    while (n > 0 && G.textWidth(line) + G.textWidth("...") > max_w) line[--n] = '\0';
    if (n + 4 < sizeof(line)) std::strcat(line, "...");
  }
  G.setFont(nullptr);
  text(line, x, y, color, f, datum);
}

void card(const Rect& r, uint16_t border = kBorder) {
  G.fillRoundRect(r.x, r.y, r.w, r.h, 12, kPanel);
  G.drawRoundRect(r.x, r.y, r.w, r.h, 12, border);
}

void button(const Rect& r, const char* label, bool selected = false, bool enabled = true, Font f = f18) {
  if (enabled) focus_nav::note(r.x, r.y, r.w, r.h);
  const uint16_t border = enabled ? (selected ? kGreen : kCyan) : kDim;
  G.fillRoundRect(r.x, r.y, r.w, r.h, 8, selected ? kSelected : kPanel);
  G.drawRoundRect(r.x, r.y, r.w, r.h, 8, border);
  Clip clip(r);
  fit(label, r.x + r.w / 2, r.y + r.h / 2, r.w - 12, enabled ? (selected ? kGreen : TFT_WHITE) : kMuted, f, middle_center);
}

// Word-wrapped paragraph inside `r` (at most `max_lines`).
void paragraph(const char* s, const Rect& r, uint16_t color, int max_lines, Font f = f18) {
  set_font(f);
  const int line_h = f == f18 ? 24 : 30;
  char line[128];
  size_t pos = 0;
  const size_t len = std::strlen(s);
  for (int row = 0; row < max_lines && pos < len; ++row) {
    size_t take = 0, last_space = 0;
    while (pos + take < len && take + 1 < sizeof(line)) {
      line[take] = s[pos + take];
      line[take + 1] = '\0';
      if (s[pos + take] == ' ') last_space = take;
      if (G.textWidth(line) > r.w) {
        take = last_space ? last_space : take;
        break;
      }
      ++take;
    }
    std::memcpy(line, s + pos, take);
    line[take] = '\0';
    G.setFont(nullptr);
    text(line, r.x, r.y + row * line_h + line_h / 2, color, f);
    set_font(f);
    pos += take;
    while (pos < len && s[pos] == ' ') ++pos;
  }
  G.setFont(nullptr);
}

// ---- formatting helpers (no allocation)
void fmt_mhz(char* out, size_t n, uint32_t hz) {
  std::snprintf(out, n, "%u.%03u", static_cast<unsigned int>(hz / 1000000u),
                static_cast<unsigned int>((hz / 1000u) % 1000u));
}

void fmt_time(char* out, size_t n, bool valid, uint32_t utc, uint32_t uptime_ms) {
  if (valid) {
    std::snprintf(out, n, "%02u:%02u:%02u", static_cast<unsigned int>((utc / 3600u) % 24u),
                  static_cast<unsigned int>((utc / 60u) % 60u), static_cast<unsigned int>(utc % 60u));
  } else {
    const uint32_t s = uptime_ms / 1000u;
    std::snprintf(out, n, "+%u:%02u", static_cast<unsigned int>(std::min<uint32_t>(s / 60u, 9999u)),
                  static_cast<unsigned int>(s % 60u));
  }
}

const char* receiver_text(ReceiverState s) {
  switch (s) {
    case ReceiverState::disconnected: return "RECEIVER FEED NOT CONNECTED";
    case ReceiverState::unavailable: return "RECEIVER UNAVAILABLE";
    case ReceiverState::paused: return "RECEIVER PAUSED";
    case ReceiverState::running: return "RECEIVER RUNNING";
    default: return "RECEIVER ERROR";
  }
}

const char* receiver_short(ReceiverState s) {
  switch (s) {
    case ReceiverState::disconnected: return "NOT CONNECTED";
    case ReceiverState::unavailable: return "UNAVAILABLE";
    case ReceiverState::paused: return "PAUSED";
    case ReceiverState::running: return "RUNNING";
    default: return "ERROR";
  }
}

uint16_t receiver_color(ReceiverState s) {
  return s == ReceiverState::running ? kGreen : s == ReceiverState::error ? kRed
         : s == ReceiverState::paused ? kAmber : kMuted;
}

const char* tune_text(const Snapshot& s) {
  switch (s.tune) {
    case TuneStatus::pending: return "TUNE REQUEST PENDING";
    case TuneStatus::tuned: return "TUNED";
    case TuneStatus::failed: return "TUNE FAILED";
    default: return "NOT TUNED - NO RECEIVER RUNTIME";
  }
}

// ---- header, context strip, tabs
void draw_header() {
  G.fillRect(0, 0, 1280, layout::kHeaderH, kBg);
  audio_header::draw_brand("ACARS");
  // Nothing is drawn in the shared status-bar reserve (x420..866, y12..66); only the rule below the header.
  G.drawFastHLine(20, layout::kHeaderH - 1, 1240, kBorder);
  if (g_header != nullptr) g_header();
}

void draw_context(const Snapshot& s) {
  const Rect& r = layout::kContext;
  Clip clip(r);
  const bool demo = s.source == SourceKind::demo;
  G.fillRoundRect(r.x, r.y + 4, demo ? 150 : 90, 32, 8, demo ? kAmber : kPanel);
  text(demo ? "DEMO DATA" : "LIVE", r.x + (demo ? 75 : 45), r.y + 20, demo ? kBg : (s.receiver == ReceiverState::running ? kGreen : kMuted), f18, middle_center);
  const int x = r.x + (demo ? 166 : 106);
  fit(demo ? "SYNTHETIC - NOT RF" : receiver_text(s.receiver), x, r.y + 20, 420,
      demo ? kAmber : receiver_color(s.receiver), f18);
  if (demo) {
    char line[64];
    std::snprintf(line, sizeof(line), "%s", receiver_text(s.receiver));
    fit(line, x + 220, r.y + 20, 300, receiver_color(s.receiver), f18);
  }
  char freq[24], line[96];
  fmt_mhz(freq, sizeof(freq), s.requested_hz);
  std::snprintf(line, sizeof(line), "REQUESTED %s MHz%s  -  %s", freq, s.custom_active ? " (CUSTOM)" : "",
                tune_text(s));
  fit(line, r.x + r.w - 8, r.y + 20, 600, s.tune == TuneStatus::tuned ? kGreen : kCyan, f18, middle_right);
}

void tab_icon(int i, int x, int y, uint16_t c) {
  switch (i) {
    case 0: G.drawCircle(x, y, 13, c); G.drawCircle(x, y, 4, c); break;
    case 1: G.fillTriangle(x - 14, y + 8, x + 14, y, x - 14, y - 8, c); break;
    case 2:
      for (int k = 0; k < 4; ++k) G.fillRect(x - 16 + k * 9, y + 12 - (k + 1) * 6, 6, (k + 1) * 6, c);
      break;
    case 3:
      G.drawLine(x - 16, y + 6, x - 6, y - 8, c);
      G.drawLine(x - 6, y - 8, x + 4, y + 2, c);
      G.drawLine(x + 4, y + 2, x + 16, y - 12, c);
      break;
    case 4:
      G.drawRoundRect(x - 16, y - 10, 32, 22, 3, c);
      G.drawFastHLine(x - 16, y - 2, 32, c);
      break;
    default:
      G.drawCircle(x, y, 10, c);
      G.drawFastHLine(x - 16, y, 32, c);
      G.drawFastVLine(x, y - 16, 32, c);
      break;
  }
}

void draw_tabs() {
  static constexpr const char* labels[] = {"LIVE", "AIRCRAFT", "CHANNELS", "ACTIVITY", "ARCHIVE", "SETUP"};
  G.fillRect(0, layout::kTabsY, 1280, 74, kBg);
  G.drawFastHLine(0, layout::kTabsY, 1280, kBorder);
  for (int i = 0; i < layout::kTabCount; ++i) {
    const Rect t = layout::tab(i);
    const bool sel = static_cast<int>(g_ui->view()) == i;
    focus_nav::note(t.x, t.y, t.w, t.h);
    Clip clip(t);
    if (sel) {
      G.fillRect(t.x, t.y + 1, t.w, 73, kSelected);
      G.fillRect(t.x, t.y + 1, t.w, 3, kCyan);
    }
    if (i) G.drawFastVLine(t.x, t.y, 74, kBorder);
    const uint16_t c = sel ? kCyan : TFT_LIGHTGREY;
    tab_icon(i, t.x + 34, t.y + 38, c);
    text(labels[i], t.x + 60, t.y + 39, c, f18);
  }
}

// ---- LIVE
void integrity_cell(const MessageRecord& m, const Rect& cell, bool highlight) {
  char label[16];
  uint16_t color = kGreen;
  if (m.integrity == Integrity::crc_corrected && highlight) {
    std::snprintf(label, sizeof(label), "FIX %u", m.corrected_bits);
    color = kAmber;
  } else {
    std::snprintf(label, sizeof(label), "CRC OK");
  }
  Clip clip(cell);
  text(label, cell.x + cell.w - 6, cell.y + cell.h / 2, color, f18, middle_right);
}

void draw_live(const Snapshot& s) {
  const Rect& list = layout::kLiveList;
  card(list);
  {
    Clip clip(list);
    static constexpr struct { const char* t; int x; } cols[] = {
        {"TIME", 36}, {"TAIL", 126}, {"FLIGHT", 232}, {"LBL", 318}, {"MHz", 366}, {"TEXT", 448}, {"INTEGRITY", 744}};
    for (const auto& c : cols) text(c.t, c.x, list.y + 20, kMuted, f18);
    G.drawFastHLine(list.x + 8, list.y + 38, list.w - 16, kBorder);
    if (s.message_count == 0) {
      text(s.source == SourceKind::demo ? "NO MESSAGES IN THIS DEMO SCENARIO" : "NO MESSAGES YET", list.x + list.w / 2,
           list.y + 220, TFT_WHITE, f24, middle_center);
      text(s.source == SourceKind::demo ? "Synthetic scenario: EMPTY" :
           s.receiver == ReceiverState::running ? "Listening - no CRC-verified block decoded yet"
                                                : receiver_text(s.receiver),
           list.x + list.w / 2, list.y + 262, kMuted, f18, middle_center);
    }
  }
  for (size_t i = 0; i < s.message_count; ++i) {
    const MessageRecord& m = s.messages[i];
    const Rect row = layout::live_row(static_cast<int>(i));
    focus_nav::note(row.x, row.y, row.w, row.h);
    const bool fix = m.integrity == Integrity::crc_corrected && s.prefs.highlight_corrected;
    Clip clip(row);
    G.fillRect(row.x, row.y + 1, row.w, row.h - 2, i % 2 ? kPanel : kBg);
    if (fix) G.fillRect(row.x, row.y + 2, 4, row.h - 4, kAmber);
    char t[16], f[16];
    fmt_time(t, sizeof(t), m.time_valid, m.utc_epoch, m.uptime_ms);
    fmt_mhz(f, sizeof(f), m.frequency_hz);
    const int cy = row.y + row.h / 2;
    text(t, 36, cy, m.time_valid ? TFT_WHITE : kMuted);
    fit(m.tail[0] ? m.tail : "UNKNOWN", 126, cy, 100, m.tail[0] ? kCyan : kMuted);
    fit(m.flight[0] ? m.flight : "--", 232, cy, 80, m.flight[0] ? TFT_WHITE : kMuted);
    fit(m.label[0] ? m.label : "--", 318, cy, 44, TFT_WHITE);
    text(m.frequency_hz ? f : "--", 366, cy, kMuted);
    fit(m.has_text ? m.preview : "(no text)", 448, cy, 290, m.has_text ? TFT_WHITE : kMuted);
    integrity_cell(m, {row.x + row.w - 100, row.y, 100, row.h}, s.prefs.highlight_corrected);
  }

  const Rect& st = layout::kLiveStatus;
  card(st);
  {
    Clip clip(st);
    text("STATUS", st.x + 16, st.y + 24, kCyan, f24);
    struct Line { const char* k; char v[40]; uint16_t c; } rows[5];
    std::snprintf(rows[0].v, sizeof(rows[0].v), "%s", receiver_short(s.receiver)); rows[0].k = "RECEIVER"; rows[0].c = receiver_color(s.receiver);
    char f[16];
    fmt_mhz(f, sizeof(f), s.requested_hz);
    std::snprintf(rows[1].v, sizeof(rows[1].v), "%s MHz (REQ)", f); rows[1].k = "FREQUENCY"; rows[1].c = TFT_WHITE;
    std::snprintf(rows[2].v, sizeof(rows[2].v), "%u / %u", static_cast<unsigned>(s.messages_held), static_cast<unsigned>(kHistoryCapacity)); rows[2].k = "RECENT HELD"; rows[2].c = TFT_WHITE;
    std::snprintf(rows[3].v, sizeof(rows[3].v), "%u", static_cast<unsigned>(s.messages_total)); rows[3].k = "CRC VERIFIED"; rows[3].c = kGreen;
    std::snprintf(rows[4].v, sizeof(rows[4].v), "%u", static_cast<unsigned>(s.corrected_total)); rows[4].k = "VERIFIED REPAIRS"; rows[4].c = s.corrected_total ? kAmber : kMuted;
    for (int i = 0; i < 5; ++i) {
      const int y = st.y + 62 + i * 33;
      text(rows[i].k, st.x + 16, y, kMuted);
      text(rows[i].v, st.x + st.w - 16, y, rows[i].c, f18, middle_right);
    }
  }

  const Rect& nav = layout::kLiveNav;
  card(nav);
  {
    Clip clip(nav);
    char range[64];
    const size_t first = s.matching_count ? s.message_offset + 1 : 0;
    std::snprintf(range, sizeof(range), "%u-%u of %u%s", static_cast<unsigned>(first),
                  static_cast<unsigned>(s.message_offset + s.message_count), static_cast<unsigned>(s.matching_count),
                  s.prefs.filter == MessageFilter::corrected_only ? "  (REPAIRED ONLY)" : "");
    text("MESSAGES", nav.x + 16, nav.y + 24, kCyan, f24);
    text(range, nav.x + nav.w - 16, nav.y + 24, TFT_WHITE, f18, middle_right);
  }
  button(layout::kLiveNavUp, "NEWER", false, s.message_offset > 0);
  button(layout::kLiveNavDown, "OLDER", false, s.message_offset + s.message_count < s.matching_count);

  const Rect& nt = layout::kLiveNotice;
  card(nt, s.source == SourceKind::demo ? kAmber : kBorder);
  Clip clip(nt);
  if (s.source == SourceKind::demo) {
    text("DEMO DATA", nt.x + 16, nt.y + 24, kAmber, f24);
    paragraph("Synthetic records for previewing this screen. Not RF, never archived or sent anywhere.",
              {nt.x + 16, nt.y + 44, nt.w - 32, 80}, kMuted, 3);
  } else if (s.receiver == ReceiverState::running) {
    text("LISTENING", nt.x + 16, nt.y + 24, kGreen, f24);
    paragraph("Single-channel decoder. Rows are CRC-verified blocks only.", {nt.x + 16, nt.y + 44, nt.w - 32, 80}, kMuted, 3);
  } else {
    text("NO RECEIVER FEED", nt.x + 16, nt.y + 24, kAmber, f24);
    paragraph("The native decoder is not attached to a receiver yet. Preview this screen with SETUP > DEMO MODE.",
              {nt.x + 16, nt.y + 44, nt.w - 32, 80}, kMuted, 3);
  }
}

// ---- message detail
void field(const Rect& card_rect, int row, const char* k, const char* v, uint16_t color = TFT_WHITE) {
  const int y = card_rect.y + 56 + row * 31;
  text(k, card_rect.x + 16, y, kMuted);
  fit(v, card_rect.x + card_rect.w - 16, y, 250, color, f18, middle_right);
}

void draw_detail(const Snapshot& s) {
  const MessageRecord& m = g_ui->detail_record();
  const bool held = g_ui->detail_held(*g_model);
  card(layout::kDetailBar);
  button(layout::kDetailBack, "< BACK", false, true);
  {
    Clip clip(layout::kDetailBar);
    char title[64];
    std::snprintf(title, sizeof(title), "MESSAGE DETAIL%s", m.source == SourceKind::demo ? "  -  DEMO (SYNTHETIC)" : "");
    text(title, 640, layout::kDetailBar.y + 24, m.source == SourceKind::demo ? kAmber : TFT_WHITE, f24, middle_center);
  }
  button(layout::kDetailNewer, "NEWER >", false, g_model->neighbor_seq(m.seq, true) != 0);
  button(layout::kDetailOlder, "< OLDER", false, g_model->neighbor_seq(m.seq, false) != 0);

  const Rect& fr = layout::kDetailFields;
  card(fr);
  {
    Clip clip(fr);
    text("PARSED ACARS FIELDS", fr.x + 16, fr.y + 24, kCyan, f24);
    char v[48], f[16], t[24];
    int r = 0;
    field(fr, r++, "AIRCRAFT", m.tail[0] ? m.tail : "UNKNOWN", m.tail[0] ? kCyan : kMuted);
    field(fr, r++, "FLIGHT", m.flight[0] ? m.flight : "NOT IN BLOCK", m.flight[0] ? TFT_WHITE : kMuted);
    std::snprintf(v, sizeof(v), "%c", m.mode ? m.mode : '?');
    field(fr, r++, "MODE", v);
    field(fr, r++, "LABEL", m.label[0] ? m.label : "--");
    std::snprintf(v, sizeof(v), "%c", m.block_id ? m.block_id : '?');
    field(fr, r++, "BLOCK ID", v);
    if (m.acknowledgement == 0x15) std::snprintf(v, sizeof(v), "NAK");
    else if (m.acknowledgement >= 0x20 && m.acknowledgement < 0x7f) std::snprintf(v, sizeof(v), "%c", m.acknowledgement);
    else std::snprintf(v, sizeof(v), "?");
    field(fr, r++, "ACK", v);
    fmt_mhz(f, sizeof(f), m.frequency_hz);
    std::snprintf(v, sizeof(v), "%s MHz", m.frequency_hz ? f : "--");
    field(fr, r++, "FREQUENCY", v);
    fmt_time(t, sizeof(t), m.time_valid, m.utc_epoch, m.uptime_ms);
    std::snprintf(v, sizeof(v), m.time_valid ? "%s UTC" : "%s SINCE BOOT", t);
    field(fr, r++, "RECEIVED", v, m.time_valid ? TFT_WHITE : kMuted);
    field(fr, r++, "BLOCK END", end_marker_name(m.end_marker), m.end_marker ? TFT_WHITE : kAmber);
    if (m.integrity == Integrity::crc_corrected) std::snprintf(v, sizeof(v), "CRC OK AFTER %u-BIT REPAIR", m.corrected_bits);
    else std::snprintf(v, sizeof(v), "CRC VERIFIED");
    field(fr, r++, "INTEGRITY", v, m.integrity == Integrity::crc_corrected ? kAmber : kGreen);
    std::snprintf(v, sizeof(v), "%s  %u B", m.source == SourceKind::demo ? "DEMO" : "LIVE RF", m.raw_bytes);
    field(fr, r++, "SOURCE / SIZE", v, m.source == SourceKind::demo ? kAmber : TFT_WHITE);
    if (!held) text("NO LONGER IN HISTORY (COPY SHOWN)", fr.x + 16, fr.y + fr.h - 18, kAmber, f18);
  }

  const Rect& raw = layout::kDetailRaw;
  card(raw);
  {
    Clip clip(raw);
    char title[64];
    std::snprintf(title, sizeof(title), "RAW TEXT (%s, %u BYTES)",
                  s.prefs.format == TextFormat::plain ? "PLAIN" : "ESCAPED", m.text_length);
    text(title, raw.x + 16, raw.y + 24, kCyan, f24);
  }
  button(layout::kDetailRawUp, "UP", false, g_ui->detail_line() > 0);
  button(layout::kDetailRawDown, "DOWN", false,
         g_ui->detail_line_count() > g_ui->detail_line() + static_cast<size_t>(layout::kDetailLines));
  {
    Clip clip(layout::kDetailRawText);
    if (!s.prefs.show_raw_text) {
      text("RAW TEXT HIDDEN (SETUP > RAW TEXT)", layout::kDetailRawText.x, layout::kDetailRawText.y + 20, kMuted, f18);
    } else if (g_ui->detail_text_length() == 0) {
      text(m.has_text ? "(EMPTY)" : "NO TEXT IN THIS BLOCK", layout::kDetailRawText.x, layout::kDetailRawText.y + 20, kMuted, f18);
    } else {
      for (int i = 0; i < layout::kDetailLines; ++i) {
        const char* start;
        size_t len;
        if (!g_ui->detail_line_at(g_ui->detail_line() + static_cast<size_t>(i), &start, &len)) break;
        char line[layout::kDetailCols + 1];
        std::memcpy(line, start, len);
        line[len] = '\0';
        text(line, layout::kDetailRawText.x, layout::kDetailRawText.y + i * layout::kDetailLineH + 13, TFT_WHITE, mono);
      }
    }
  }

  const Rect& in = layout::kDetailInterp;
  card(in);
  Clip clip(in);
  text("APPLICATION INTERPRETATION", in.x + 16, in.y + 24, kCyan, f24);
  if (m.has_interpretation)
    paragraph(m.interpretation, {in.x + 16, in.y + 46, in.w - 32, 90}, TFT_WHITE, 3);
  else
    paragraph("NOT SUPPLIED. No application parser is installed, so the text above is shown exactly as received.",
              {in.x + 16, in.y + 46, in.w - 32, 90}, kMuted, 3);
}

// ---- AIRCRAFT
void draw_aircraft(const Snapshot& s) {
  const Rect& list = layout::kAcList;
  card(list);
  {
    Clip clip(list);
    static constexpr struct { const char* t; int x; } cols[] = {
        {"TAIL", 44}, {"FLIGHT", 188}, {"MSGS", 298}, {"LAST", 372}, {"SEEN", 440}, {"REPAIRED", 566}};
    for (const auto& c : cols) text(c.t, c.x, list.y + 22, kMuted);
    G.drawFastHLine(list.x + 8, list.y + 40, list.w - 16, kBorder);
    if (s.aircraft_count == 0) {
      text("NO AIRCRAFT YET", list.x + list.w / 2, list.y + 220, TFT_WHITE, f24, middle_center);
      text("Aircraft appear after a CRC-verified block carries a tail", list.x + list.w / 2, list.y + 258, kMuted, f18, middle_center);
    }
  }
  const bool has_sel = g_ui->selected_tail()[0] != '\0' || false;
  for (size_t i = 0; i < s.aircraft_count; ++i) {
    const AircraftRecord& a = s.aircraft[i];
    const Rect row = layout::ac_row(static_cast<int>(i));
    focus_nav::note(row.x, row.y, row.w, row.h);
    const bool sel = has_sel && std::strcmp(a.tail, g_ui->selected_tail()) == 0;
    Clip clip(row);
    G.fillRoundRect(row.x, row.y + 2, row.w, row.h - 4, 6, sel ? kSelected : (i % 2 ? kPanel : kBg));
    if (sel) G.drawRoundRect(row.x, row.y + 2, row.w, row.h - 4, 6, kGreen);
    const int cy = row.y + row.h / 2;
    char b[24];
    fit(a.tail[0] ? a.tail : "UNKNOWN", 44, cy, 130, a.tail[0] ? kCyan : kMuted, f24);
    fit(a.flight[0] ? a.flight : "--", 188, cy, 100, a.flight[0] ? TFT_WHITE : kMuted);
    std::snprintf(b, sizeof(b), "%u", static_cast<unsigned>(a.message_count));
    text(b, 298, cy, TFT_WHITE);
    text(a.last_label[0] ? a.last_label : "--", 372, cy, TFT_WHITE);
    fmt_time(b, sizeof(b), a.time_valid, a.last_utc, a.last_seen_ms);
    text(b, 440, cy, a.time_valid ? TFT_WHITE : kMuted);
    std::snprintf(b, sizeof(b), "%u", static_cast<unsigned>(a.corrected_count));
    text(b, 566, cy, a.corrected_count && s.prefs.highlight_corrected ? kAmber : kMuted);
  }

  card(layout::kAcControls);
  button(layout::kAcSort, s.prefs.sort == AircraftSort::recent ? "SORT: RECENT" : "SORT: MOST MSGS", false, true);
  button(layout::kAcUp, "UP", false, s.aircraft_offset > 0);
  button(layout::kAcDown, "DOWN", false, s.aircraft_offset + s.aircraft_count < s.aircraft_total);
  {
    Clip clip(layout::kAcCount);
    char b[40];
    std::snprintf(b, sizeof(b), "%u AIRCRAFT", static_cast<unsigned>(s.aircraft_total));
    text(b, layout::kAcCount.x + layout::kAcCount.w, layout::kAcCount.y + 24, TFT_WHITE, f18, middle_right);
  }

  const Rect& det = layout::kAcDetail;
  card(det);
  {
    Clip clip(det);
    char title[48];
    const bool sel = g_ui->selected_tail()[0] != '\0';
    std::snprintf(title, sizeof(title), sel ? "RECENT: %s" : "RECENT MESSAGES", g_ui->selected_tail());
    text(title, det.x + 16, det.y + 24, kCyan, f24);
    if (!sel) {
      text("Select an aircraft to list its recent messages", det.x + det.w / 2, det.y + 170, kMuted, f18, middle_center);
    } else {
      const MessageRecord* list[kAircraftRecentShown];
      const size_t n = g_model->aircraft_messages(g_ui->selected_tail(), list, kAircraftRecentShown);
      if (n == 0) text("No held messages for this aircraft", det.x + det.w / 2, det.y + 170, kMuted, f18, middle_center);
      for (size_t j = 0; j < n; ++j) {
        const MessageRecord& m = *list[j];
        const Rect row = layout::ac_msg_row(static_cast<int>(j));
        focus_nav::note(row.x, row.y, row.w, row.h);
        Clip rc(row);
        G.fillRect(row.x, row.y + 1, row.w, row.h - 2, j % 2 ? kPanel : kBg);
        if (m.integrity == Integrity::crc_corrected && s.prefs.highlight_corrected) G.fillRect(row.x, row.y + 2, 4, row.h - 4, kAmber);
        char t[16];
        fmt_time(t, sizeof(t), m.time_valid, m.utc_epoch, m.uptime_ms);
        text(t, row.x + 12, row.y + 18, m.time_valid ? TFT_WHITE : kMuted);
        text(m.label[0] ? m.label : "--", row.x + 130, row.y + 18, TFT_WHITE);
        text(m.integrity == Integrity::crc_corrected && s.prefs.highlight_corrected ? "FIX" : "CRC OK", row.x + row.w - 10, row.y + 18,
             m.integrity == Integrity::crc_corrected && s.prefs.highlight_corrected ? kAmber : kGreen, f18, middle_right);
        fit(m.has_text ? m.preview : "(no text)", row.x + 12, row.y + 42, row.w - 24, kMuted);
      }
    }
    text("POSITION / ALTITUDE / ADS-B CORRELATION: NOT AVAILABLE", layout::kAcNote.x, layout::kAcNote.y + 12, kDim, f18);
  }
}

// ---- CHANNELS
void draw_channels(const Snapshot& s) {
  card(layout::kChList);
  char b[96];
  std::snprintf(b, sizeof(b), "REGION: %s  >", region_name(s.prefs.region));
  button(layout::kChRegion, b, false, true);
  for (size_t i = 0; i < s.channel_count; ++i) {
    const ChannelRecord& c = s.channels[i];
    const Rect row = layout::ch_row(static_cast<int>(i));
    focus_nav::note(row.x, row.y, row.w, row.h);
    const bool sel = c.selected && !s.custom_active;
    Clip clip(row);
    G.fillRoundRect(row.x, row.y + 2, row.w, row.h - 4, 6, sel ? kSelected : kPanel);
    G.drawRoundRect(row.x, row.y + 2, row.w, row.h - 4, 6, sel ? kGreen : kBorder);
    fit(c.label, row.x + 14, row.y + 20, 360, sel ? kGreen : TFT_WHITE, f24);
    fit(preset(s.prefs.region, i).source, row.x + 14, row.y + 42, 560, kMuted);
    if (sel) text("REQUESTED", row.x + row.w - 14, row.y + 20, kGreen, f18, middle_right);
    if (c.decoded_messages) {
      std::snprintf(b, sizeof(b), "%u DECODED", static_cast<unsigned>(c.decoded_messages));
      text(b, row.x + row.w - 14, row.y + 42, kCyan, f18, middle_right);
    }
  }
  button(layout::kChCustomLabel, "CUSTOM", s.custom_active, false);
  button(layout::kChMinus, "- 25k", false, true);
  button(layout::kChPlus, "+ 25k", false, true);
  button(layout::kChClear, "CLEAR", false, s.custom_active);
  {
    Clip clip(layout::kChCustomValue);
    char f[16];
    fmt_mhz(f, sizeof(f), s.requested_hz);
    std::snprintf(b, sizeof(b), s.custom_active ? "%s MHz CUSTOM" : "%s MHz (PRESET)", f);
    text(b, layout::kChCustomValue.x + 12, layout::kChCustomValue.y + 26, s.custom_active ? kGreen : kMuted, f18);
  }

  const Rect& st = layout::kChStatus;
  card(st);
  {
    Clip clip(st);
    char f[16];
    fmt_mhz(f, sizeof(f), s.requested_hz);
    text("REQUESTED CHANNEL", st.x + 16, st.y + 24, kCyan, f24);
    std::snprintf(b, sizeof(b), "%s MHz", f);
    text(b, st.x + 16, st.y + 80, TFT_WHITE, f40);
    text("TUNE STATUS", st.x + 16, st.y + 134, kMuted);
    fit(tune_text(s), st.x + st.w - 16, st.y + 134, 270, s.tune == TuneStatus::tuned ? kGreen : kAmber, f18, middle_right);
    text("RECEIVING", st.x + 16, st.y + 170, kMuted);
    text(s.caps.live_reception && s.receiver == ReceiverState::running ? "NOT MEASURED" : "NO", st.x + st.w - 16, st.y + 170, TFT_WHITE, f18, middle_right);
    text("CARRIER / ACTIVITY", st.x + 16, st.y + 206, kMuted);
    text("NOT MEASURED", st.x + st.w - 16, st.y + 206, kDim, f18, middle_right);
    text("RECEIVER SESSION", st.x + 16, st.y + 242, kMuted);
    text(receiver_short(s.receiver), st.x + st.w - 16, st.y + 242, receiver_color(s.receiver), f18, middle_right);
  }
  const Rect& nt = layout::kChNotes;
  card(nt);
  Clip clip(nt);
  text("SINGLE-CHANNEL DECODER", nt.x + 16, nt.y + 24, kCyan, f24);
  paragraph("The list is a set of choices, not simultaneous reception. Choosing one only changes the request. "
            "Multi-channel reception: NOT AVAILABLE.",
            {nt.x + 16, nt.y + 44, nt.w - 32, 110}, kMuted, 4);
}

// ---- ACTIVITY
void tile(int i, const char* label, const char* value, const char* caption, uint16_t color) {
  const Rect r = layout::act_tile(i);
  card(r);
  Clip clip(r);
  text(label, r.x + 14, r.y + 18, kMuted);
  text(value, r.x + 14, r.y + 60, color, f40);
  fit(caption, r.x + 14, r.y + 98, r.w - 24, kDim);
}

void draw_activity(const Snapshot& s) {
  char b[8][24];
  const bool have = s.stats_available;
  auto num = [&](char* out, uint32_t v) {
    if (have) std::snprintf(out, 24, "%u", static_cast<unsigned>(v));
    else std::snprintf(out, 24, "--");
  };
  num(b[0], s.stats.sync_candidates); num(b[1], s.stats.crc_ok); num(b[2], s.stats.corrected);
  num(b[3], s.stats.crc_rejected); num(b[4], s.stats.framing_rejected); num(b[5], s.stats.overflows);
  num(b[6], s.stats.bits);
  std::snprintf(b[7], 24, "%u", static_cast<unsigned>(s.queue_drops));
  tile(0, "SYNC CANDIDATES", b[0], "preamble matches, not signal level", TFT_WHITE);
  tile(1, "CRC VERIFIED", b[1], "blocks that passed CRC-16", kGreen);
  tile(2, "REPAIRED", b[2], "subset of verified, FEC applied", s.stats.corrected ? kAmber : TFT_WHITE);
  tile(3, "CRC REJECTED", b[3], "dropped: CRC failed", TFT_WHITE);
  tile(4, "FRAMING REJECTED", b[4], "dropped: bad frame structure", TFT_WHITE);
  tile(5, "BUFFER OVERFLOWS", b[5], "blocks longer than 250 bytes", s.stats.overflows ? kRed : TFT_WHITE);
  tile(6, "BITS DEMODULATED", b[6], "count since decoder start", TFT_WHITE);
  tile(7, "UI QUEUE DROPS", b[7], "decoder events lost before display", s.queue_drops ? kRed : TFT_WHITE);

  const Rect& y = layout::kActYield;
  card(y);
  {
    Clip clip(y);
    text("DECODE SUMMARY", y.x + 16, y.y + 24, kCyan, f24);
    char v[48];
    int r = 0;
    auto row = [&](const char* k, const char* val, uint16_t c = TFT_WHITE) {
      const int yy = y.y + 62 + r++ * 32;
      text(k, y.x + 16, yy, kMuted);
      text(val, y.x + y.w - 16, yy, c, f18, middle_right);
    };
    if (have && s.stats.sync_candidates) std::snprintf(v, sizeof(v), "%u%%", static_cast<unsigned>((100ull * s.stats.crc_ok) / s.stats.sync_candidates));
    else std::snprintf(v, sizeof(v), "--");
    row("VERIFIED PER SYNC CANDIDATE", v);
    if (have && s.stats.crc_ok) std::snprintf(v, sizeof(v), "%u%%", static_cast<unsigned>((100ull * s.stats.corrected) / s.stats.crc_ok));
    else std::snprintf(v, sizeof(v), "--");
    row("REPAIRED SHARE OF VERIFIED", v);
    std::snprintf(v, sizeof(v), "%u / %u", static_cast<unsigned>(s.messages_held), static_cast<unsigned>(kHistoryCapacity));
    row("MESSAGES HELD", v);
    row("DECODER REPORT", have ? "RECEIVED" : "NONE YET", have ? kGreen : kAmber);
    row("SOURCE", s.source == SourceKind::demo ? "DEMO (SYNTHETIC)" : "LIVE", s.source == SourceKind::demo ? kAmber : TFT_WHITE);
  }
  const Rect& g = layout::kActSignal;
  card(g);
  Clip clip(g);
  text("SIGNAL DIAGNOSTICS", g.x + 16, g.y + 24, kCyan, f24);
  text("NOT AVAILABLE", g.x + 16, g.y + 74, kAmber, f24);
  paragraph("SNR, RSSI, carrier lock and symbol timing are not measured by the decoder yet, so nothing is estimated or drawn here. "
            "This space is reserved for them.",
            {g.x + 16, g.y + 100, g.w - 32, 110}, kMuted, 4);
}

// ---- ARCHIVE
void draw_archive(const Snapshot& s) {
  const bool demo = s.source == SourceKind::demo;
  const bool browsable = demo || s.archive_available;      // the synthetic store, or the saved log
  card(layout::kArFilters);
  {
    Clip clip(layout::kArFilters);
    text("FILTERS", layout::kArFilters.x + 16, layout::kArFilters.y + 24, kCyan, f24);
  }
  static constexpr const char* names[] = {"TAIL", "LABEL", "FLIGHT", "FREQUENCY", "TEXT SEARCH"};
  for (int i = 0; i < layout::kArFilterCount; ++i) {
    const Rect r = layout::ar_filter(i);
    const bool enabled = browsable && i < 2;
    if (enabled) focus_nav::note(r.x, r.y, r.w, r.h);
    G.fillRoundRect(r.x, r.y, r.w, r.h, 8, kPanel);
    G.drawRoundRect(r.x, r.y, r.w, r.h, 8, enabled ? kCyan : kDim);
    Clip clip(r);
    text(names[i], r.x + 14, r.y + 18, kMuted);
    const char* v = !enabled ? (browsable ? "PLANNED" : "NEEDS ARCHIVE")
                    : i == 0 ? (g_ui->archive_query().tail[0] ? g_ui->archive_query().tail : "ANY")
                             : (g_ui->archive_query().label[0] ? g_ui->archive_query().label : "ANY");
    text(v, r.x + 14, r.y + 42, enabled ? TFT_WHITE : kDim, f24);
  }
  button(layout::kArClear, "CLEAR FILTERS", false, browsable && (g_ui->archive_query().tail[0] || g_ui->archive_query().label[0]));

  const Rect& list = layout::kArList;
  card(list, demo ? kAmber : kBorder);
  {
    Clip clip(list);
    if (demo) {
      text("SYNTHETIC DEMO ARCHIVE - NOT STORED", list.x + 16, list.y + 22, kAmber, f24);
    } else if (s.archive_available) {
      char head[64];
      std::snprintf(head, sizeof(head), "SAVED BLOCKS  %u", static_cast<unsigned int>(s.archive_count));
      text(head, list.x + 16, list.y + 22, kCyan, f24);
      const char* status = g_model->archive() ? g_model->archive()->status() : "";
      const bool saving = std::strcmp(status, "SAVING TO SD") == 0;
      text(status, list.x + list.w - 190, list.y + 22, saving ? kGreen : kAmber, f18, middle_right);
    } else {
      text("ARCHIVE", list.x + 16, list.y + 22, kCyan, f24);
    }
  }
  if (!browsable) {
    Clip clip(list);
    text("ARCHIVE NOT AVAILABLE", list.x + list.w / 2, list.y + 200, TFT_WHITE, f24, middle_center);
    paragraph("No archive source is attached. Messages are kept in memory only (last 48) and are lost at power-off.",
              {list.x + 80, list.y + 240, list.w - 160, 150}, kMuted, 5);
    return;
  }
  size_t total = 0;
  const MessageRecord* rows[layout::kArRows];
  const size_t n = g_model->archive_query(g_ui->archive_query(), g_ui->archive_offset(), rows, layout::kArRows, &total);
  button(layout::kArUp, "UP", false, g_ui->archive_offset() > 0);
  button(layout::kArDown, "DOWN", false, g_ui->archive_offset() + n < total);
  if (n == 0) {
    Clip clip(list);
    text("NO MATCHING MESSAGES", list.x + list.w / 2, list.y + 260, kMuted, f24, middle_center);
    return;
  }
  for (size_t i = 0; i < n; ++i) {
    const MessageRecord& m = *rows[i];
    const Rect row = layout::ar_row(static_cast<int>(i));
    focus_nav::note(row.x, row.y, row.w, row.h);
    Clip clip(row);
    G.fillRect(row.x, row.y + 1, row.w, row.h - 2, i % 2 ? kPanel : kBg);
    char t[16];
    fmt_time(t, sizeof(t), m.time_valid, m.utc_epoch, m.uptime_ms);
    text(t, row.x + 12, row.y + 18, m.time_valid ? TFT_WHITE : kMuted);
    fit(m.tail[0] ? m.tail : "UNKNOWN", row.x + 130, row.y + 18, 120, kCyan);
    fit(m.flight[0] ? m.flight : "--", row.x + 270, row.y + 18, 100, TFT_WHITE);
    text(m.label[0] ? m.label : "--", row.x + 390, row.y + 18, TFT_WHITE);
    fit(m.has_text ? m.preview : "(no text)", row.x + 12, row.y + 42, row.w - 24, kMuted);
    if (m.integrity == Integrity::crc_corrected && s.prefs.highlight_corrected)
      text("FIX", row.x + row.w - 12, row.y + 18, kAmber, f18, middle_right);
  }
}

// ---- SETUP
void setting(int item, const char* name, const char* value, const char* hint, bool enabled) {
  const Rect r = layout::setup_item(item / 5, item % 5);
  if (enabled) focus_nav::note(r.x, r.y, r.w, r.h);
  G.fillRoundRect(r.x, r.y, r.w, r.h, 10, kPanel);
  G.drawRoundRect(r.x, r.y, r.w, r.h, 10, enabled ? kCyan : kDim);
  Clip clip(r);
  text(name, r.x + 16, r.y + 20, kMuted);
  fit(value, r.x + 16, r.y + 48, 330, enabled ? TFT_WHITE : kDim, f24);
  fit(hint, r.x + r.w - 14, r.y + 48, 250, kDim, f18, middle_right);
}

void draw_setup(const Snapshot& s) {
  const Preferences& p = s.prefs;
  const bool demo = s.source == SourceKind::demo;
  setting(0, "PRESET REGION", region_name(p.region), "tap to change", true);
  setting(1, "TEXT FORMAT", p.format == TextFormat::plain ? "PLAIN" : "ESCAPED", p.format == TextFormat::plain ? "control bytes as ." : "control bytes as <XX>", true);
  setting(2, "RAW TEXT IN DETAIL", p.show_raw_text ? "SHOWN" : "HIDDEN", "message detail page", true);
  setting(3, "CORRECTED-FRAME MARKS", p.highlight_corrected ? "ON" : "OFF", "FIX badge and bar", true);
  setting(4, "MESSAGE FILTER", p.filter == MessageFilter::all ? "ALL VERIFIED" : "REPAIRED ONLY", "LIVE list", true);
  setting(5, "AIRCRAFT SORT", p.sort == AircraftSort::recent ? "MOST RECENT" : "MOST MESSAGES", "AIRCRAFT list", true);
  setting(6, "DEMO MODE", demo ? "ON - SYNTHETIC DATA" : "OFF", demo ? "tap to leave" : "never auto-starts", true);
  setting(7, "DEMO SCENARIO", demo ? acars_demo::scenario_name(s.scenario) : "ENTER DEMO MODE FIRST", "tap to cycle", demo);
  setting(8, "ARCHIVE (SD)", "PLANNED", "no provider yet", false);
  setting(9, "MULTI-CHANNEL", "PLANNED", "single channel today", false);
}

void redraw_content(const Snapshot& s) {
  if (g_ui->detail_open()) { draw_detail(s); return; }
  switch (g_ui->view()) {
    case View::live: draw_live(s); break;
    case View::aircraft: draw_aircraft(s); break;
    case View::channels: draw_channels(s); break;
    case View::activity: draw_activity(s); break;
    case View::archive: draw_archive(s); break;
    case View::setup: draw_setup(s); break;
    default: break;
  }
}

// Paints context strip, content and tab bar as one frame. With a sprite the display only ever receives the
// finished frame; without one the region is cleared and drawn in place (works, but can flicker).
void paint_region(const Snapshot& s) {
  const bool sprite = g_canvas_ok;
  if (sprite) {
    G.target_sprite(g_canvas, kRegionY);
    g_canvas->fillSprite(kBg);
  } else {
    G.target_display();
    G.fillRect(0, kRegionY, layout::kScreenW, kRegionH, kBg);
  }
  const uint32_t t0 = now_us();
  draw_context(s);
  redraw_content(s);
  draw_tabs();
  const uint32_t t1 = now_us();
  g_paint.compose_us = t1 - t0;
  g_paint.compose_us_max = std::max(g_paint.compose_us_max, g_paint.compose_us);
  ++g_paint.frames;
  if (!sprite) return;
  G.target_display();
  // Which part of the frame changed since the panel last showed one?
  acars_ui::DirtyRect rects[kMaxPushRects];
  size_t count = 0;
  bool whole = true;
  if (g_front && g_front_valid) {
    const uint16_t* next = static_cast<const uint16_t*>(g_canvas->getBuffer());
    const uint16_t* previous = static_cast<const uint16_t*>(g_front->getBuffer());
    if (next && previous) {
      bool overflow = false;
      count = acars_ui::diff_rects(previous, next, layout::kScreenW, kRegionH, rects, kMaxPushRects, &overflow);
      const int64_t area = acars_ui::rect_area(rects, count);
      whole = overflow || area * 100 > static_cast<int64_t>(layout::kScreenW) * kRegionH * kWholeFramePercent;
    }
  }
  const uint32_t t2 = now_us();
  g_paint.diff_us = t2 - t1;
  g_paint.diff_us_max = std::max(g_paint.diff_us_max, g_paint.diff_us);
  if (whole) {
    // Pushing a whole sprite takes about 0.8 s on the rotated panel (measured: ~1.2 M pixel/s). Most of the screen
    // changing means a deliberate page change, so draw it straight onto the display with the native primitives,
    // as the dashboard did originally; the composed sprite still becomes the reference for later partial pushes.
    G.fillRect(0, kRegionY, layout::kScreenW, kRegionH, kBg);
    draw_context(s);
    redraw_content(s);
    draw_tabs();
    ++g_paint.whole_pushes;
    g_paint.pixels = static_cast<uint32_t>(layout::kScreenW) * kRegionH;
  } else {
    for (size_t i = 0; i < count; ++i) {
      M5.Display.setClipRect(rects[i].x, kRegionY + rects[i].y, rects[i].w, rects[i].h);
      g_canvas->pushSprite(0, kRegionY);
    }
    M5.Display.clearClipRect();
    g_paint.partial_pushes += count ? 1u : 0u;
    g_paint.pixels = static_cast<uint32_t>(acars_ui::rect_area(rects, count));
  }
  g_paint.rects = static_cast<uint32_t>(whole ? 1 : count);
  g_paint.push_us = now_us() - t2;
  g_paint.push_us_max = std::max(g_paint.push_us_max, g_paint.push_us);
  if (g_front) {
    std::swap(g_canvas, g_front);              // the frame just pushed is now the reference
    g_front_valid = true;
  }
}

void redraw_all() {
  const Snapshot& s = g_model->snapshot();
  focus_nav::clear();
  G.target_display();
  g_front_valid = false;                        // the panel may show another screen: push this frame whole
  if (!g_canvas_ok) G.fillScreen(kBg);          // with a sprite the pushed frame replaces the old screen
  draw_header();
  paint_region(s);
  g_drawn_bits = s.stats.bits;
  g_drawn_ui = g_ui->revision();
  g_drawn_snapshot = s.revision;
}

}  // namespace

void install(Model* model, Controller* controller, HeaderHook header_controls) {
  g_model = model;
  g_ui = controller;
  g_header = header_controls;
}

void enter() {
  if (!g_model || !g_ui) return;
  g_active = true;
  (void)alloc_canvas();
  redraw_all();
}

void leave() {
  g_active = false;
  G.target_display();
  G.clearClipRect();
  G.setFont(nullptr);
  free_canvas();
}

void draw() {
  if (g_active) redraw_all();
}

void set_overlay_probe(bool (*overlay_open)()) { g_overlay_open = overlay_open; }

void update(uint32_t now_ms) {
  if (!g_active || !g_model || !g_ui) return;
  const Snapshot& s = g_model->snapshot();
  // The ACTIVITY page shows the demodulated-bit counter, which moves constantly without bumping the revision.
  // Refresh it at most once a second; no other page cares.
  const bool bits_stale = g_ui->view() == View::activity && !g_ui->detail_open() && s.stats_available &&
                          s.stats.bits != g_drawn_bits && now_ms - g_last_draw_ms >= 1000;
  if (g_drawn_ui == g_ui->revision() && g_drawn_snapshot == s.revision && !bits_stale) return;
  // A header tray (volume) is drawn over the content by the shared header; repainting now would erase it.
  // Keep the change pending and repaint once it is closed.
  if (g_overlay_open != nullptr && g_overlay_open()) return;
  if (g_drawn_ui == g_ui->revision() && now_ms - g_last_draw_ms < 250) return;   // data-only change: throttle
  g_last_draw_ms = now_ms;
  if (g_drawn_ui != g_ui->revision()) focus_nav::clear();
  paint_region(s);
  g_drawn_bits = s.stats.bits;
  g_drawn_ui = g_ui->revision();
  g_drawn_snapshot = s.revision;
}

bool active() { return g_active; }

PaintStats paint_stats() { return g_paint; }

bool interaction_check() {
  if (!g_active || !g_model || !g_ui) return false;
  const View saved = g_ui->view();
  bool ok = true;
  for (int i = 0; i < layout::kTabCount; ++i) {
    const Rect t = layout::tab(i);
    (void)g_ui->touch(t.x + t.w / 2, t.y + t.h / 2, *g_model);
    ok = ok && static_cast<int>(g_ui->view()) == i && !g_ui->detail_open();
    update(10000u * (static_cast<uint32_t>(i) + 1));
  }
  g_ui->select_view(saved);
  redraw_all();
  return ok;
}

}  // namespace orcsdr::acars_dashboard
