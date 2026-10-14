#include "sstv_dashboard.hpp"
#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include "dashboard_audio_control.hpp"
#include "focus_nav.hpp"
#include "freq_keypad.hpp"
#include "sstv_archive.hpp"
#include "sstv_audio_scope.hpp"
#include "sstv_auto_detect.hpp"
#include "sstv_status.hpp"
#include "sstv_tune.hpp"
#include "time_service.hpp"

// Standard OrcSDR dashboard layout: shared brand header (badge, title, frequency, shared controls),
// a body area, and the tab bar along the bottom. Palette, fonts and button style are the ones the
// FM, LoRa and FT8 dashboards use. Home, Settings and Mute are the shared header controls and are
// handled globally, so this file draws no private Home button.
namespace orcsdr::sstv {
namespace {
namespace arc = orcsdr::sstv_archive;
using orcsdr::sstv_settings::Detection;

constexpr uint16_t kPanel = 0x0841;
constexpr uint16_t kGrid = 0x2945;
constexpr uint16_t kCyan = 0x2e7f;
constexpr uint16_t kGreen = 0x6fe8;
constexpr uint16_t kAmber = 0xfd20;
constexpr uint16_t kMuted = 0x8c71;
constexpr uint16_t kSelected = 0x1264;
constexpr uint16_t kImageBg = 0x0000;

struct Rect { int x, y, w, h; };
constexpr Rect kBody{24, 104, 1232, 514};
constexpr int kTabsY = 630;
constexpr int kTabCount = 5;
constexpr int kTabW = 1280 / kTabCount;
constexpr const char* kPages[kTabCount] = {"RECEIVE", "IMAGES", "SIGNAL", "TUNE", "SETUP"};

// RECEIVE
constexpr Rect kImagePanel{24, 104, 760, 514};
constexpr Rect kInfoPanel{796, 104, 460, 514};
constexpr Rect kAbort{812, 540, 208, 64};
constexpr Rect kCapture{1032, 540, 208, 64};
// IMAGES
constexpr Rect kGalleryPanel{24, 104, 844, 514};
constexpr Rect kDetailPanel{880, 104, 376, 514};
constexpr Rect kSave{896, 500, 344, 50};
constexpr Rect kClearGallery{896, 558, 344, 50};
Rect tile(int i) { return {42 + (i % 2) * 412, 148 + (i / 2) * 232, 400, 220}; }
// SIGNAL
constexpr Rect kMonitorPanel{24, 104, 862, 514};
constexpr Rect kDecoderPanel{898, 104, 358, 514};
// SETUP
constexpr Rect kModePanel{24, 104, 724, 514};
constexpr Rect kDetectPanel{760, 104, 496, 514};
Rect mode_button(int i) { return {40 + ((i - 1) % 3) * 232, 188 + ((i - 1) / 3) * 78, 216, 64}; }
constexpr Rect kAuto{776, 146, 464, 56};
constexpr Rect kVisOnly{776, 210, 464, 56};
constexpr Rect kManual{776, 274, 464, 56};
constexpr Rect kAutosave{776, 372, 464, 56};

Page g_page = Page::receive;
UiSnapshot g_ui{};
bool g_active = false;
uint8_t g_profile = 1;
void draw_all();
size_t g_gallery_selected = 0;
char g_notice[84]{};
uint32_t g_notice_until = 0;
uint32_t g_last_render_ms = 0;
void (*g_header_hook)() = nullptr;
// 15 KiB thumbnail scratch lives in PSRAM (internal RAM is nearly full), allocated on first use.
uint16_t* g_thumb = nullptr;
UiStats g_ui_stats{};
// Measures one screen update (display latency for bench diagnostics).
struct DrawTimer {
  int64_t started = esp_timer_get_time();
  ~DrawTimer() {
    const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - started);
    g_ui_stats.draw_us_last = us;
    if (us > g_ui_stats.draw_us_max) g_ui_stats.draw_us_max = us;
    ++g_ui_stats.draws;
  }
};

bool hit(int32_t x, int32_t y, const Rect& r) {
  return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}
int cx(const Rect& r) { return r.x + r.w / 2; }
int cy(const Rect& r) { return r.y + r.h / 2; }

// Same text helper as the other dashboards: size 0 = DejaVu18, 1 = DejaVu24, 2+ = scaled built-in font.
void text(const char* value, int x, int y, uint16_t color = TFT_WHITE, int size = 2,
          textdatum_t datum = middle_center) {
  M5.Display.setTextDatum(datum);
  if (size <= 1) {
    M5.Display.setFont(size == 1 ? &fonts::DejaVu24 : &fonts::DejaVu18);
    M5.Display.setTextSize(1);
  } else {
    M5.Display.setTextSize(size);
  }
  M5.Display.setTextColor(color);
  M5.Display.drawString(value, x, y);
  if (size <= 1) M5.Display.setFont(nullptr);   // the shared header helpers draw with the built-in font
}
void frame(const Rect& r, uint16_t border = kCyan) {
  M5.Display.fillRoundRect(r.x, r.y, r.w, r.h, 10, kPanel);
  M5.Display.drawRoundRect(r.x, r.y, r.w, r.h, 10, border);
}
// Everything a page draws stays inside the body area. A long line (for example the amber "...START ROW UNKNOWN" or a
// sync line with missed pulses) used to run past the right edge of its panel; panels only repaint inside their own
// rectangle, so the overflow stayed on the screen until the next page change. The margin beside the body is also
// cleared so any stale overflow from an earlier draw disappears.
struct BodyClip {
  BodyClip() {
    const int right = kBody.x + kBody.w;
    M5.Display.fillRect(right, kBody.y, 1280 - right, kBody.h, TFT_BLACK);
    M5.Display.fillRect(0, kBody.y, kBody.x, kBody.h, TFT_BLACK);
    M5.Display.setClipRect(kBody.x, kBody.y, kBody.w, kBody.h);
  }
  ~BodyClip() { M5.Display.clearClipRect(); }
};
void button(const Rect& r, const char* label, bool selected = false, bool enabled = true, int size = 2) {
  if (enabled) focus_nav::note(r.x, r.y, r.w, r.h);
  const uint16_t border = enabled ? (selected ? kGreen : kCyan) : TFT_DARKGREY;
  M5.Display.fillRoundRect(r.x, r.y, r.w, r.h, 8, selected ? kSelected : kPanel);
  M5.Display.drawRoundRect(r.x, r.y, r.w, r.h, 8, border);
  text(label, cx(r), cy(r), enabled ? (selected ? kGreen : TFT_WHITE) : kMuted, size);
}
uint16_t tone_color(sstv_status::Tone tone) {
  switch (tone) {
    case sstv_status::Tone::good: return kGreen;
    case sstv_status::Tone::warn: return kAmber;
    case sstv_status::Tone::info: return kCyan;
    default: return kMuted;
  }
}
const Image* newest_archived(Image* storage) {
  *storage = archived_image(0);
  return storage->pixels ? storage : nullptr;
}
// Short save status for one image, from the background archive.
const char* save_text(const Image& im, uint16_t* color) {
  arc::Result r;
  if (im.session_id && arc::result_for(im.session_id, &r)) {
    switch (r.state) {
      case arc::JobState::saved: *color = kGreen; return "SAVED TO SD";
      case arc::JobState::saving: *color = kCyan; return "SAVING...";
      case arc::JobState::pending: *color = kCyan; return "QUEUED FOR SD";
      case arc::JobState::failed: *color = kAmber; return arc::error_name(r.error);
      case arc::JobState::skipped: *color = kMuted; return "TOO SHORT TO SAVE";
      default: break;
    }
  }
  *color = kMuted;
  return g_ui.settings.autosave ? "WAITING" : "NOT SAVED (AUTO-SAVE OFF)";
}

// ---------------------------------------------------------------- header, tabs, notice
void draw_header() {
  M5.Display.fillRect(0, 0, 1280, 92, TFT_BLACK);
  audio_header::draw_brand("SSTV RECEIVER");
  M5.Display.drawFastVLine(365, 18, 66, kCyan);
  text("SSTV RX", 392, 38, TFT_WHITE, 4, middle_left);
  text("RECEIVE ONLY", 600, 40, kMuted, 1, middle_left);
  const auto m = metrics();
  char label[64];
  if (g_notice[0] && millis() < g_notice_until) {
    // A message from the last action takes the status line for a few seconds; it never covers controls.
    std::snprintf(label, sizeof(label), "%.34s", g_notice);
    text(label, 392, 75, kAmber, 2, middle_left);
  } else {
    std::snprintf(label, sizeof(label), "%0.5f MHz  %s", g_ui.frequency_hz / 1e6,
                  g_ui.streaming ? state_name(m.state) : "RECEIVER IDLE");
    text(label, 392, 75, m.receiving ? kGreen : kCyan, 2, middle_left);
  }
  text(g_ui.receiver_ready ? "RTL OK" : "NO RTL", 776, 30, g_ui.receiver_ready ? kGreen : kAmber, 0, middle_left);
  text(g_ui.sd_ready ? "SD OK" : "NO SD", 776, 60, g_ui.sd_ready ? kGreen : kAmber, 0, middle_left);
  M5.Display.drawFastVLine(865, 18, 66, kCyan);
  if (g_header_hook != nullptr) g_header_hook();
  M5.Display.drawFastHLine(20, 92, 1240, kGreen);
}
void draw_tabs() {
  for (int i = 0; i < kTabCount; ++i)
    button({i * kTabW + 4, kTabsY + 4, kTabW - 8, 82}, kPages[i], static_cast<int>(g_page) == i);
}

// ---------------------------------------------------------------- image painting
// The framebuffer lives in PSRAM. Only newly decoded rows are copied into this one-row viewport
// buffer, never a whole frame on a UI timer; the DSP thread never touches M5GFX.
uint16_t g_preview_line[734]{};
const uint16_t* g_preview_source = nullptr;
uint16_t g_painted_screen_rows = 0, g_painted_source_rows = 0;
uint32_t g_last_revision = 0;
void paint_image_rows(const Image& original, const Rect& area) {
  const Image image = original.pixels ? original : archived_image(0);
  if (!image.pixels || !image.width || !image.height) return;
  const int view_w = area.w - 22, view_h = area.h - 22;
  const float fit = std::min(static_cast<float>(view_w) / image.width, static_cast<float>(view_h) / image.height);
  const int dw = std::max(1, std::min({view_w, static_cast<int>(fit * image.width), static_cast<int>(std::size(g_preview_line))}));
  const int dh = std::max(1, std::min(view_h, static_cast<int>(fit * image.height)));
  const int left = area.x + (area.w - dw) / 2, top = area.y + (area.h - dh) / 2;
  const uint16_t rows = image.partial ? image.decoded_rows : image.height;
  if (g_preview_source != image.pixels || rows < g_painted_source_rows) {
    M5.Display.fillRect(area.x + 3, area.y + 3, area.w - 6, area.h - 6, kImageBg);
    g_preview_source = image.pixels;
    g_painted_screen_rows = 0;
  }
  for (int dy = g_painted_screen_rows; dy < dh; ++dy) {
    const uint16_t sy = static_cast<uint16_t>(static_cast<uint32_t>(dy) * image.height / dh);
    if (sy >= rows) break;
    for (int dx = 0; dx < dw; ++dx) {
      const uint16_t sx = static_cast<uint16_t>(static_cast<uint32_t>(dx) * image.width / dw);
      g_preview_line[dx] = image.pixels[static_cast<size_t>(sy) * image.width + sx];
    }
    // Native RGB565 row: push it as rgb565_t (a uint16_t* image is read as big-endian and shows wrong hues).
    M5.Display.pushImage(left, top + dy, dw, 1, reinterpret_cast<const lgfx::rgb565_t*>(g_preview_line));
    g_painted_screen_rows = dy + 1;
  }
  g_painted_source_rows = rows;
}
void image_area(const Rect& area, const Image& im, const char* empty) {
  M5.Display.fillRoundRect(area.x, area.y, area.w, area.h, 10, kImageBg);
  M5.Display.drawRoundRect(area.x, area.y, area.w, area.h, 10, kGrid);
  if (!im.pixels && !archived_image(0).pixels) {
    text(empty, cx(area), cy(area), kMuted, 0);
    return;
  }
  paint_image_rows(im, area);
}

// ---------------------------------------------------------------- RECEIVE
void draw_receive_stats() {
  BodyClip clip;
  const auto m = metrics();
  Image last;
  const Image* previous = newest_archived(&last);
  frame(kInfoPanel, kGrid);
  const int x = kInfoPanel.x + 20;
  text("RECEPTION", x, 128, kCyan, 1, middle_left);
  M5.Display.drawFastHLine(x, 150, kInfoPanel.w - 40, kGrid);
  const auto head = sstv_status::headline(m, previous);
  text(head.text, x, 184, tone_color(head.tone), 1, middle_left);
  char line[96];
  const ModeSpec* spec = m.receiving ? mode_spec(m.received_mode) : nullptr;
  if (spec) std::snprintf(line, sizeof(line), "MODE  %s   %u x %u", spec->name, spec->width, spec->height);
  else std::snprintf(line, sizeof(line), "MODE  WAITING FOR A TRANSMISSION");
  text(line, x, 224, TFT_WHITE, 0, middle_left);
  if (m.receiving && m.identification == Identification::vis)
    std::snprintf(line, sizeof(line), "HEADER  VIS %u (CONFIRMED)", static_cast<unsigned>(m.vis));
  else if (m.receiving && m.identification == Identification::sync)
    std::snprintf(line, sizeof(line), "NO VIS - SYNC LIKELY (%u)", static_cast<unsigned>(m.identification_score));
  else
    std::snprintf(line, sizeof(line), "VIS HEADERS  OK %lu   BAD %lu", (unsigned long)m.good_vis, (unsigned long)m.bad_vis);
  text(line, x, 252, m.receiving ? TFT_WHITE : kMuted, 0, middle_left);
  if (m.receiving)
    std::snprintf(line, sizeof(line), "IMAGE  IN PROGRESS%s", m.start_unknown ? " - START ROW UNKNOWN" : "");
  else if (previous) {
    sstv_status::ImageChips chips;
    sstv_status::image_chips(*previous, &chips);
    std::snprintf(line, sizeof(line), "LAST IMAGE  %s", chips.completeness);
  } else {
    std::snprintf(line, sizeof(line), "IMAGE  NONE YET");
  }
  text(line, x, 280, m.receiving && m.start_unknown ? kAmber : TFT_WHITE, 0, middle_left);
  const int bar_w = kInfoPanel.w - 40;
  const int meter = m.rows ? bar_w * m.row / m.rows : 0;
  M5.Display.fillRoundRect(x, 302, bar_w, 16, 6, kGrid);
  if (meter) M5.Display.fillRoundRect(x, 302, meter, 16, 6, m.start_unknown ? kAmber : kGreen);
  std::snprintf(line, sizeof(line), "ROWS  %u / %u   (%u%%)", (unsigned)m.row, (unsigned)m.rows,
                m.rows ? (unsigned)(100u * m.row / m.rows) : 0u);
  text(line, x, 340, TFT_WHITE, 0, middle_left);
  std::snprintf(line, sizeof(line), "SYNC  LOCKED %u   MISSED %u   ERR %.1f ms", (unsigned)m.session_sync_locked,
                (unsigned)m.session_sync_missed, (double)m.line_error_ms);
  text(line, x, 368, m.session_sync_missed ? kAmber : kMuted, 0, middle_left);
  std::snprintf(line, sizeof(line), "AUDIO  %.0f Hz   LEVEL %.0f%%   GAPS %lu", (double)m.tone_hz,
                (double)(m.audio_peak * 100.f), (unsigned long)m.audio_gaps);
  text(line, x, 396, m.audio_gaps || m.audio_overruns ? kAmber : kMuted, 0, middle_left);
  std::snprintf(line, sizeof(line), "STATE  %s", state_name(m.state));
  text(line, x, 424, kMuted, 0, middle_left);
  button(kAbort, "ABORT IMAGE");
  button(kCapture, "CAPTURE IMAGE");
}
void draw_receive() {
  image_area(kImagePanel, live_image(), "WAITING FOR FIRST IMAGE / VIS HEADER");
  draw_receive_stats();
}

// ---------------------------------------------------------------- IMAGES
void thumbnail(const Image& im, const Rect& r) {
  if (!im.pixels) { text("EMPTY", cx(r), cy(r), kMuted, 0); return; }
  if (!g_thumb)
    g_thumb = static_cast<uint16_t*>(heap_caps_malloc(100 * 76 * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!g_thumb) { text("NO MEMORY", cx(r), cy(r), kAmber, 0); return; }
  const int tw = 100, th = 76;
  for (int yy = 0; yy < th; ++yy)
    for (int xx = 0; xx < tw; ++xx)
      g_thumb[yy * tw + xx] = im.pixels[(static_cast<size_t>(yy) * im.height / th) * im.width + (xx * im.width / tw)];
  M5.Display.pushImage(r.x + 12, r.y + 14, tw, th, reinterpret_cast<const lgfx::rgb565_t*>(g_thumb));
}
void draw_gallery() {
  frame(kGalleryPanel, kGrid);
  text("RECEIVED IMAGES", 46, 128, kCyan, 1, middle_left);
  char info[50];
  std::snprintf(info, sizeof(info), "%u IN MEMORY", (unsigned)archive_count());
  text(info, 840, 128, kMuted, 0, middle_right);
  if (archive_count() == 0) {
    text("NO IMAGES YET", 446, 350, kMuted, 1);
    text("Complete and interrupted transmissions are added here automatically", 446, 392, kMuted, 0);
  } else {
    for (size_t i = 0; i < archive_count() && i < 4; ++i) {
      const Rect t = tile(static_cast<int>(i));
      const auto im = archived_image(i);
      focus_nav::note(t.x, t.y, t.w, t.h);
      M5.Display.fillRoundRect(t.x, t.y, t.w, t.h, 8, TFT_BLACK);
      M5.Display.drawRoundRect(t.x, t.y, t.w, t.h, 8, i == g_gallery_selected ? kGreen : kGrid);
      thumbnail(im, t);
      sstv_status::ImageChips chips;
      sstv_status::image_chips(im, &chips);
      char line[64];
      const int tx = t.x + 128;
      std::snprintf(line, sizeof(line), "#%lu  %s", (unsigned long)im.serial, mode_name(im.mode));
      text(line, tx, t.y + 30, TFT_WHITE, 0, middle_left);
      text(chips.completeness, tx, t.y + 62, im.partial ? kAmber : kGreen, 0, middle_left);
      std::snprintf(line, sizeof(line), "%s", chips.identification);
      text(line, tx, t.y + 94, kMuted, 0, middle_left);
      if (im.start_unknown) text("JOINED IN PROGRESS", tx, t.y + 126, kAmber, 0, middle_left);
      uint16_t color = kMuted;
      const char* saved = save_text(im, &color);
      text(saved, t.x + 14, t.y + 190, color, 0, middle_left);
    }
  }
  frame(kDetailPanel, kGrid);
  text("SELECTED IMAGE", kDetailPanel.x + 20, 128, kCyan, 1, middle_left);
  const Image im = archived_image(g_gallery_selected);
  const int x = kDetailPanel.x + 20;
  if (im.pixels) {
    sstv_status::ImageChips chips;
    sstv_status::image_chips(im, &chips);
    char line[96];
    std::snprintf(line, sizeof(line), "#%lu   %s", (unsigned long)im.serial, mode_name(im.mode));
    text(line, x, 170, TFT_WHITE, 0, middle_left);
    std::snprintf(line, sizeof(line), "%0.5f MHz", im.frequency_hz / 1e6);
    text(line, x, 200, kCyan, 1, middle_left);
    char when[28] = "TIME NOT VERIFIED";
    if (im.utc_epoch) orcsdr::time_service::format_utc(when, sizeof(when), im.utc_epoch);
    text(when, x, 234, im.utc_epoch ? TFT_WHITE : kMuted, 0, middle_left);
    text(chips.identification, x, 262, TFT_WHITE, 0, middle_left);
    text(chips.completeness, x, 290, im.partial ? kAmber : kGreen, 0, middle_left);
    if (im.start_unknown) {
      text("JOINED IN PROGRESS", x, 318, kAmber, 0, middle_left);
      text("START ROW UNKNOWN", x, 344, kAmber, 0, middle_left);
    } else {
      text(chips.origin, x, 318, kMuted, 0, middle_left);
    }
    std::snprintf(line, sizeof(line), "ENDED  %s", chips.ending[0] ? chips.ending : "-");
    text(line, x, 374, kMuted, 0, middle_left);
    std::snprintf(line, sizeof(line), "SYNC  %u LOCKED / %u MISSED", (unsigned)im.sync_locked, (unsigned)im.sync_missed);
    text(line, x, 402, im.sync_missed ? kAmber : kMuted, 0, middle_left);
    uint16_t color = kMuted;
    const char* saved = save_text(im, &color);
    std::snprintf(line, sizeof(line), "SD  %s", saved);
    text(line, x, 432, color, 0, middle_left);
    arc::Result r;
    if (im.session_id && arc::result_for(im.session_id, &r) && r.filename[0])
      text(r.filename, x, 458, kMuted, 0, middle_left);
  } else {
    text("NOTHING SELECTED", x, 200, kMuted, 0, middle_left);
  }
  button(kSave, "SAVE BMP TO SD", false, true, 2);
  button(kClearGallery, "CLEAR RAM GALLERY", false, true, 2);
}

// ---------------------------------------------------------------- SIGNAL
void draw_analyzer() {
  frame(kMonitorPanel, kGrid);
  text("AUDIO TONE MONITOR", 46, 128, kCyan, 1, middle_left);
  const int px = 48, py = 158, pw = 814, ph = 300;
  M5.Display.fillRect(px, py, pw, ph, TFT_BLACK);
  for (int i = 0; i < 5; ++i) M5.Display.drawFastHLine(px, py + 30 + i * 60, pw, kGrid);
  const Metrics m = metrics();
  const int center = py + ph - static_cast<int>((std::clamp(m.tone_hz, 600.f, 2500.f) - 600.f) * ph / 1900.f);
  M5.Display.drawFastHLine(px + 4, center, pw - 8, m.signal ? kCyan : kMuted);
  char b[96];
  std::snprintf(b, sizeof(b), "%0.0f Hz LIVE   LEVEL %.0f%%", m.tone_hz, m.audio_peak * 100.f);
  text(b, 48, 488, m.signal ? kGreen : kMuted, 1, middle_left);
  std::snprintf(b, sizeof(b), "SYNC PULSE %.1f ms   EVERY %.1f ms   LINE ERROR %.1f ms", (double)m.sync_pulse_ms,
                (double)m.sync_interval_ms, (double)m.line_error_ms);
  text(b, 48, 528, TFT_WHITE, 0, middle_left);
  text("Tone guide: 1200 sync / 1500 black / 1900 middle / 2300 white", 48, 566, kMuted, 0, middle_left);
  frame(kDecoderPanel, kGrid);
  text("DECODER", kDecoderPanel.x + 20, 128, kCyan, 1, middle_left);
  int y = 164;
  const int x = kDecoderPanel.x + 20;
  auto emit = [&](uint16_t color) { text(b, x, y, color, 0, middle_left); y += 29; };
  std::snprintf(b, sizeof(b), "STATE  %s", state_name(m.state));
  emit(TFT_WHITE);
  std::snprintf(b, sizeof(b), "VIS  OK %lu  BAD %lu  LAST %u", (unsigned long)m.good_vis, (unsigned long)m.bad_vis, (unsigned)m.vis);
  emit(kMuted);
  auto_detect::Ranked top[3];
  const size_t shown = auto_detect::ranked(top, 3);
  if (shown) {
    for (size_t i = 0; i < shown; ++i) {
      std::snprintf(b, sizeof(b), "CAND %u  %s  %u", (unsigned)(i + 1), mode_name(top[i].mode), (unsigned)top[i].score);
      emit(i == 0 ? kGreen : kMuted);
    }
  } else {
    std::snprintf(b, sizeof(b), "CANDIDATES  NONE YET");
    emit(kMuted);
  }
  std::snprintf(b, sizeof(b), "MISSED LINES  %u (THIS IMAGE)", (unsigned)m.session_sync_missed);
  emit(m.session_sync_missed ? kAmber : kMuted);
  std::snprintf(b, sizeof(b), "RING OVERRUNS %lu  GAPS %lu", (unsigned long)m.audio_overruns, (unsigned long)m.audio_gaps);
  emit(m.audio_overruns ? kAmber : kMuted);
  std::snprintf(b, sizeof(b), "DECODE  AVG %lu us  MAX %lu us", (unsigned long)m.service_us_avg, (unsigned long)m.service_us_max);
  emit(kMuted);
  std::snprintf(b, sizeof(b), "CPU %u%%   RING MAX %u / 4096", (unsigned)m.service_cpu_pct, (unsigned)m.ring_fill_max);
  emit(kMuted);
  std::snprintf(b, sizeof(b), "IMAGES %lu  PSRAM %lu KB", (unsigned long)m.frames_alive, (unsigned long)(m.frame_bytes / 1024u));
  emit(kMuted);
  const auto s = arc::stats();
  std::snprintf(b, sizeof(b), "SD WRITE  LAST %lu  MAX %lu ms", (unsigned long)s.last_write_ms, (unsigned long)s.max_write_ms);
  emit(kMuted);
  std::snprintf(b, sizeof(b), "SD  %lu SAVED  %lu FAILED", (unsigned long)s.saved, (unsigned long)s.failed);
  emit(s.failed ? kAmber : kMuted);
}

// ---------------------------------------------------------------- TUNE
// Two panes: the RF spectrum and waterfall (tap to tune; the dongle re-centres on the tap), and the audio "sync comb"
// the decoder hears, with guide rails at the SSTV tones. Both are composed in PSRAM and pushed as one image, so a
// refresh never shows a half-drawn trace.
constexpr int kRfW = 1208, kRfSpecH = 60, kRfWfH = 78, kRfH = kRfSpecH + kRfWfH;
constexpr int kAuW = 1208, kAuH = 96;
constexpr Rect kVfo{24, 104, 1232, 88};
constexpr Rect kFreqBox{36, 110, 404, 76};
constexpr Rect kDown{452, 114, 76, 68}, kUp{536, 114, 76, 68};
constexpr Rect kStepChip{624, 114, 150, 68}, kModeChip{782, 114, 130, 68}, kGainChip{920, 114, 160, 68};
constexpr Rect kGainDown{1088, 114, 76, 68}, kGainUp{1172, 114, 76, 68};
constexpr Rect kRfPanel{24, 198, 1232, 172};
constexpr int kRfX = 36, kRfY = 224;
constexpr Rect kAudioPanel{24, 376, 1232, 146};
constexpr int kAuX = 36, kAuY = 400;
Rect preset_chip(int i) { return {24 + i * 248, 528, 240, 40}; }
constexpr Rect kEnter{24, 574, 300, 44}, kRails{336, 574, 200, 44}, kApply{548, 574, 300, 44}, kIfChip{856, 574, 204, 44};
constexpr int kStatusX = 1068;
constexpr uint16_t kRailTones[4] = {1200, 1500, 1900, 2300};
constexpr const char* kRailNames[4] = {"1200 SYNC", "1500 BLACK", "1900 MID", "2300 WHITE"};

uint16_t* g_rf_px = nullptr;        // spectrum on top, waterfall history below
uint16_t* g_au_px = nullptr;        // audio comb
uint16_t g_palette[256]{};
uint8_t g_rf_cols[kRfW]{};
uint8_t g_au_lo[kAuW]{}, g_au_frac[kAuW]{};
float g_rf_floor = 0;
bool g_rf_have = false, g_rf_floor_known = false;
uint32_t g_au_sequence = 0, g_au_ms = 0, g_tune_bar_ms = 0;
bool g_keypad = false;
char g_entry[14]{};
// What each region of the TUNE tab last drew. A region is repainted only when its signature changes, so a quiet
// screen never redraws (repainting panels every half second made the whole tab flicker).
uint32_t g_sig_bar = 0, g_sig_buttons = 0, g_sig_status = 0, g_sig_header = 0, g_sig_header_ms = 0;
uint16_t g_sig_tint = 0;
char g_sig_readout[80]{};
uint32_t mix(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
void reset_signatures() {
  g_sig_bar = g_sig_buttons = g_sig_status = g_sig_header = 0;
  g_sig_tint = 0;
  g_sig_readout[0] = '\0';
}

bool tune_visible() { return g_active && g_page == Page::tune && !g_keypad; }

int rail_x(int index) {
  return (kRailTones[index] - sstv_audio_scope::kLowHz) * kAuW / (sstv_audio_scope::kHighHz - sstv_audio_scope::kLowHz);
}

bool ensure_tune_buffers() {
  if (!g_rf_px)
    g_rf_px = static_cast<uint16_t*>(heap_caps_calloc(static_cast<size_t>(kRfW) * kRfH, sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!g_au_px)
    g_au_px = static_cast<uint16_t*>(heap_caps_calloc(static_cast<size_t>(kAuW) * kAuH, sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (g_palette[255] == 0)
    for (int i = 0; i < 256; ++i) g_palette[i] = sstv_tune::palette565(static_cast<uint8_t>(i));
  // Linear interpolation between audio bins, one lookup per pixel column.
  const float span = static_cast<float>(sstv_audio_scope::kHighHz - sstv_audio_scope::kLowHz);
  for (int x = 0; x < kAuW; ++x) {
    const float f = sstv_audio_scope::kLowHz + (x + 0.5f) * span / kAuW;
    const float position = std::clamp((f - sstv_audio_scope::kLowHz) / (span / sstv_audio_scope::kBins) - 0.5f, 0.f,
                                      static_cast<float>(sstv_audio_scope::kBins - 1) - 0.001f);
    g_au_lo[x] = static_cast<uint8_t>(position);
    g_au_frac[x] = static_cast<uint8_t>((position - std::floor(position)) * 255.f);
  }
  return g_rf_px && g_au_px;
}
void free_tune_buffers() {
  if (g_rf_px) heap_caps_free(g_rf_px);
  if (g_au_px) heap_caps_free(g_au_px);
  g_rf_px = nullptr; g_au_px = nullptr; g_rf_have = false; g_rf_floor_known = false; g_au_sequence = 0;
}

sstv_tune::Tint current_tint() {
  switch (metrics().state) {
    case SessionState::vis_acquisition: return sstv_tune::Tint::header;
    case SessionState::receiving: return sstv_tune::Tint::receiving;
    case SessionState::sync_analysis:
    case SessionState::provisional_lock:
    case SessionState::loss_recovery: return sstv_tune::Tint::recovering;
    default: return sstv_tune::Tint::searching;
  }
}

void draw_vfo_bar() {
  const auto& t = g_ui.tune;
  M5.Display.fillRoundRect(kVfo.x, kVfo.y, kVfo.w, kVfo.h, 10, kPanel);
  M5.Display.drawRoundRect(kVfo.x, kVfo.y, kVfo.w, kVfo.h, 10, kGrid);
  focus_nav::note(kFreqBox.x, kFreqBox.y, kFreqBox.w, kFreqBox.h);
  char mhz[16];
  sstv_tune::format_mhz(mhz, sizeof(mhz), g_ui.frequency_hz);
  text(mhz, kFreqBox.x + 8, cy(kFreqBox) - 6, g_ui.streaming ? TFT_WHITE : kMuted, 6, middle_left);
  text("MHz   TAP TO TYPE", kFreqBox.x + 8, kFreqBox.y + kFreqBox.h - 8, kMuted, 0, middle_left);
  button(kDown, "-", false, true, 4);
  button(kUp, "+", false, true, 4);
  char label[32];
  const uint32_t step = sstv_tune::step_hz(t.step_index);
  if (step >= 1000) std::snprintf(label, sizeof(label), step % 1000 ? "%.1f kHz" : "%.0f kHz", step / 1000.0);
  else std::snprintf(label, sizeof(label), "%lu Hz", static_cast<unsigned long>(step));
  button(kStepChip, "", false, true, 2);
  text("STEP", cx(kStepChip), kStepChip.y + 17, kMuted, 0);
  text(label, cx(kStepChip), kStepChip.y + 46, TFT_WHITE, 1);
  button(kModeChip, "", t.demod_auto, true, 2);
  text(t.demod_auto ? "MODE AUTO" : "MODE", cx(kModeChip), kModeChip.y + 17, kMuted, 0);
  text(sstv_tune::demod_name(static_cast<sstv_tune::Demod>(t.demod)), cx(kModeChip), kModeChip.y + 46, kGreen, 1);
  button(kGainChip, "", false, t.gain_available, 2);
  text(t.gain_auto ? "GAIN" : "GAIN MANUAL", cx(kGainChip), kGainChip.y + 17, kMuted, 0);
  if (!t.gain_available) std::snprintf(label, sizeof(label), "N/A");
  else if (t.gain_auto) std::snprintf(label, sizeof(label), "AUTO");
  else std::snprintf(label, sizeof(label), "%.1f dB", t.gain_tenth_db / 10.0);
  text(label, cx(kGainChip), kGainChip.y + 46, t.gain_available ? kGreen : kMuted, 1);
  button(kGainDown, "G-", false, t.gain_available, 2);
  button(kGainUp, "G+", false, t.gain_available, 2);
}

void draw_rf_frame() {
  frame(kRfPanel, kGrid);
  text("RF SPECTRUM  -  TAP TO TUNE", kRfPanel.x + 14, kRfPanel.y + 13, kCyan, 0, middle_left);
  char label[48];
  const uint32_t rate = g_ui.tune.rate_sps ? g_ui.tune.rate_sps : 240000;
  std::snprintf(label, sizeof(label), "SPAN %lu kHz   %.0f Hz / PIXEL", static_cast<unsigned long>(rate / 1000u),
                static_cast<double>(rate) / kRfW);
  text(label, kRfPanel.x + kRfPanel.w - 14, kRfPanel.y + 13, kMuted, 0, middle_right);
  M5.Display.fillRect(kRfX, kRfY, kRfW, kRfH, TFT_BLACK);
  focus_nav::note(kRfX, kRfY, kRfW, kRfH);
}
// The tuned frequency is the middle of the plot; SSB passbands hang off it on the side of the sideband.
void draw_rf_overlay() {
  const int mid = kRfX + kRfW / 2;
  const auto demod = static_cast<sstv_tune::Demod>(g_ui.tune.demod);
  const int rate = static_cast<int>(g_ui.tune.rate_sps ? g_ui.tune.rate_sps : 240000);
  auto px = [&](int hz) { return static_cast<int>(static_cast<int64_t>(hz) * kRfW / rate); };
  const uint16_t edge_color = kMuted;
  if (demod == sstv_tune::Demod::nfm) {
    M5.Display.drawFastVLine(mid - px(6000), kRfY, kRfH, edge_color);
    M5.Display.drawFastVLine(mid + px(6000), kRfY, kRfH, edge_color);
  } else {
    M5.Display.drawFastVLine(demod == sstv_tune::Demod::usb ? mid + px(2700) : mid - px(2700), kRfY, kRfH, edge_color);
  }
  M5.Display.drawFastVLine(mid, kRfY, kRfH, kAmber);
}
void push_rf() {
  if (!g_rf_px) return;
  M5.Display.pushImage(kRfX, kRfY, kRfW, kRfH, reinterpret_cast<const lgfx::rgb565_t*>(g_rf_px));
  draw_rf_overlay();
}

void draw_audio_frame() {
  const auto tint = sstv_tune::tint565(current_tint());
  frame(kAudioPanel, tint);
  text("AUDIO  -  WHAT THE DECODER HEARS", kAudioPanel.x + 14, kAudioPanel.y + 13, kCyan, 0, middle_left);
}
void draw_audio_labels(bool force) {
  const Metrics m = metrics();
  const auto tint = sstv_tune::tint565(current_tint());
  const int label_y = kAuY + kAuH + 14;
  if (force) {
    M5.Display.fillRect(kAudioPanel.x + 4, label_y - 9, kAudioPanel.w - 8, 18, kPanel);
    for (int i = 0; i < 4; ++i)
      text(kRailNames[i], kAuX + rail_x(i), label_y, g_ui.tune.rails ? tint : kMuted, 0);
  }
  // Readout of the sync tone against 1200 Hz, and which way to move the dial.
  const auto demod = static_cast<sstv_tune::Demod>(g_ui.tune.demod);
  const auto error = sstv_tune::tuning_error(demod, m.sync_tone_hz, m.sync_tone_pulses);
  char line[80];
  uint16_t color = kMuted;
  if (demod == sstv_tune::Demod::nfm) {
    std::snprintf(line, sizeof(line), "NFM: CENTRE THE SIGNAL ON THE SPECTRUM");
  } else if (!error.valid) {
    std::snprintf(line, sizeof(line), "SYNC TONE  WAITING FOR SYNC PULSES");
  } else if (std::abs(error.correction_hz) < 10) {
    std::snprintf(line, sizeof(line), "SYNC %.0f Hz   ON FREQUENCY", static_cast<double>(m.sync_tone_hz));
    color = kGreen;
  } else {
    std::snprintf(line, sizeof(line), "SYNC %.0f Hz   MOVE DIAL %+ld Hz", static_cast<double>(m.sync_tone_hz),
                  static_cast<long>(error.correction_hz));
    color = kAmber;
  }
  if (!force && std::strcmp(line, g_sig_readout) == 0) return;
  std::snprintf(g_sig_readout, sizeof(g_sig_readout), "%s", line);
  M5.Display.fillRect(kAudioPanel.x + 400, kAudioPanel.y + 3, kAudioPanel.w - 410, 20, kPanel);
  text(line, kAudioPanel.x + kAudioPanel.w - 14, kAudioPanel.y + 13, color, 0, middle_right);
}
void compose_audio() {
  if (!g_au_px) return;
  const auto view = sstv_audio_scope::view();
  const uint16_t tint = sstv_tune::tint565(current_tint());
  for (int y = 0; y < kAuH; ++y) {
    uint16_t* row = g_au_px + static_cast<size_t>(y) * kAuW;
    if (!view.rows || static_cast<uint32_t>(y) >= view.sequence) {
      std::fill(row, row + kAuW, static_cast<uint16_t>(0));
      continue;
    }
    const uint8_t* bins = view.rows + static_cast<size_t>((view.sequence - 1 - y) % view.row_count) * view.bins;
    for (int x = 0; x < kAuW; ++x) {
      const int lo = g_au_lo[x], f = g_au_frac[x];
      row[x] = g_palette[(bins[lo] * (255 - f) + bins[lo + 1] * f) / 255];
    }
  }
  if (g_ui.tune.rails)
    for (int i = 0; i < 4; ++i)
      for (int y = 0; y < kAuH; y += 4) g_au_px[static_cast<size_t>(y) * kAuW + rail_x(i)] = tint;
  const Metrics m = metrics();
  if (m.sync_tone_pulses && m.sync_tone_hz > sstv_audio_scope::kLowHz && m.sync_tone_hz < sstv_audio_scope::kHighHz) {
    const int x = static_cast<int>((m.sync_tone_hz - sstv_audio_scope::kLowHz) * kAuW /
                                   (sstv_audio_scope::kHighHz - sstv_audio_scope::kLowHz));
    for (int y = 0; y < 5; ++y)
      for (int d = -2; d <= 2; ++d)
        if (x + d >= 0 && x + d < kAuW) g_au_px[static_cast<size_t>(y) * kAuW + x + d] = TFT_WHITE;
  }
  g_au_sequence = view.sequence;
}
void push_audio() {
  if (!g_au_px) return;
  M5.Display.pushImage(kAuX, kAuY, kAuW, kAuH, reinterpret_cast<const lgfx::rgb565_t*>(g_au_px));
}

void draw_tune_buttons() {
  for (size_t i = 0; i < std::size(kProfiles); ++i) {
    const Rect r = preset_chip(static_cast<int>(i));
    const bool on = kProfiles[i].hz == g_ui.frequency_hz;
    char f[40];
    std::snprintf(f, sizeof(f), "%s %.3f", kProfiles[i].name, kProfiles[i].hz / 1e6);
    button(r, f, on, true, 0);
  }
  button(kEnter, "ENTER FREQUENCY", false, true, 2);
  button(kRails, g_ui.tune.rails ? "GUIDE RAILS ON" : "GUIDE RAILS OFF", g_ui.tune.rails, true, 2);
  const Metrics m = metrics();
  const auto error = sstv_tune::tuning_error(static_cast<sstv_tune::Demod>(g_ui.tune.demod), m.sync_tone_hz, m.sync_tone_pulses);
  char label[48];
  const bool can_apply = error.valid && std::abs(error.correction_hz) >= 5;
  if (can_apply) std::snprintf(label, sizeof(label), "APPLY CORRECTION %+ld Hz", static_cast<long>(error.correction_hz));
  else std::snprintf(label, sizeof(label), "APPLY CORRECTION");
  button(kApply, label, false, can_apply, 2);
  const uint32_t width = g_ui.tune.if_hz;
  std::snprintf(label, sizeof(label), g_ui.tune.if_auto ? "IF %lu kHz AUTO" : "IF %lu kHz",
                static_cast<unsigned long>(width / 1000u));
  button(kIfChip, label, !g_ui.tune.if_auto, true, 2);
}
void draw_tune_status() {
  const Metrics m = metrics();
  M5.Display.fillRect(kStatusX, 574, 188, 44, TFT_BLACK);
  char status[48];
  std::snprintf(status, sizeof(status), "%s", state_name(m.state));
  text(status, kStatusX + 8, 586, m.receiving ? kGreen : kCyan, 0, middle_left);
  std::snprintf(status, sizeof(status), "LEVEL %.0f%%  GAPS %lu", static_cast<double>(m.audio_peak * 100.f),
                static_cast<unsigned long>(m.audio_gaps));
  text(status, kStatusX + 8, 608, kMuted, 0, middle_left);
}
void draw_tune_controls() {
  draw_tune_buttons();
  draw_tune_status();
}

void draw_keypad_screen() {
  M5.Display.startWrite();
  M5.Display.fillRect(0, 93, 1280, 627, TFT_BLACK);
  orcsdr::freq_keypad::draw(93, TFT_BLACK, "ENTER SSTV FREQUENCY", "0.100 - 1700", "MHz", g_entry);
  M5.Display.endWrite();
}

uint32_t bar_signature() {
  const auto& t = g_ui.tune;
  uint32_t h = 2166136261u;
  for (uint32_t v : {g_ui.frequency_hz, static_cast<uint32_t>(g_ui.streaming), static_cast<uint32_t>(t.step_index),
                     static_cast<uint32_t>(t.demod), static_cast<uint32_t>(t.demod_auto), static_cast<uint32_t>(t.gain_available),
                     static_cast<uint32_t>(t.gain_auto), static_cast<uint32_t>(t.gain_tenth_db), t.rate_sps})
    h = mix(h, v);
  return h;
}
uint32_t buttons_signature() {
  const Metrics m = metrics();
  const auto error = sstv_tune::tuning_error(static_cast<sstv_tune::Demod>(g_ui.tune.demod), m.sync_tone_hz, m.sync_tone_pulses);
  uint32_t h = 2166136261u;
  for (uint32_t v : {g_ui.frequency_hz, g_ui.tune.if_hz, static_cast<uint32_t>(g_ui.tune.rails), static_cast<uint32_t>(error.valid),
                     static_cast<uint32_t>(error.valid ? error.correction_hz : 0)})
    h = mix(h, v);
  return h;
}
uint32_t status_signature() {
  const Metrics m = metrics();
  uint32_t h = 2166136261u;
  for (uint32_t v : {static_cast<uint32_t>(m.state), static_cast<uint32_t>(m.audio_peak * 100.f), static_cast<uint32_t>(m.audio_gaps)})
    h = mix(h, v);
  return h;
}

void draw_tune() {
  if (!ensure_tune_buffers()) {
    frame(kBody, kGrid);
    text("NOT ENOUGH MEMORY FOR THE TUNING DISPLAY", cx(kBody), cy(kBody), kAmber, 1);
    return;
  }
  draw_vfo_bar();
  draw_rf_frame();
  if (g_rf_have) push_rf();
  else text("WAITING FOR THE RADIO", kRfX + kRfW / 2, kRfY + kRfH / 2, kMuted, 0);
  draw_audio_frame();
  compose_audio();
  push_audio();
  draw_audio_labels(true);
  draw_tune_controls();
  g_sig_bar = bar_signature();
  g_sig_buttons = buttons_signature();
  g_sig_status = status_signature();
  g_sig_tint = sstv_tune::tint565(current_tint());
  g_au_ms = g_tune_bar_ms = millis();
}

// Called on every screen update while the TUNE tab shows: the audio comb refreshes when new rows exist, the bar and
// readouts a couple of times a second.
void update_tune(uint32_t now) {
  if (!g_au_px) return;
  M5.Display.startWrite();
  const auto view = sstv_audio_scope::view();
  const uint16_t tint = sstv_tune::tint565(current_tint());
  if (tint != g_sig_tint) {   // the receiver changed state: recolour the frame and the rail labels
    g_sig_tint = tint;
    draw_audio_frame();
    compose_audio();
    push_audio();
    draw_audio_labels(true);
    g_au_ms = now;
  } else if (view.sequence != g_au_sequence && now - g_au_ms >= 120u) {
    compose_audio();
    push_audio();
    g_au_ms = now;
  }
  if (now - g_tune_bar_ms >= 500u) {
    g_tune_bar_ms = now;
    draw_audio_labels(false);
    if (const uint32_t sig = bar_signature(); sig != g_sig_bar) { g_sig_bar = sig; draw_vfo_bar(); }
    if (const uint32_t sig = buttons_signature(); sig != g_sig_buttons) { g_sig_buttons = sig; draw_tune_buttons(); }
    if (const uint32_t sig = status_signature(); sig != g_sig_status) { g_sig_status = sig; draw_tune_status(); }
  }
  M5.Display.endWrite();
}

// Touch on the TUNE tab. The keypad owns the screen while it is open.
Action touch_tune(int32_t x, int32_t y) {
  if (hit(x, y, kFreqBox) || hit(x, y, kEnter)) {
    g_entry[0] = '\0';
    g_keypad = true;
    draw_keypad_screen();
    return {};
  }
  if (hit(x, y, kDown)) return {ActionKind::nudge, -1};
  if (hit(x, y, kUp)) return {ActionKind::nudge, 1};
  if (hit(x, y, kStepChip)) return {ActionKind::set_step, static_cast<int32_t>((g_ui.tune.step_index + 1) % sstv_tune::step_count())};
  if (hit(x, y, kModeChip)) return {ActionKind::cycle_demod, 0};
  if (g_ui.tune.gain_available) {
    if (hit(x, y, kGainChip)) return {ActionKind::gain_toggle, 0};
    if (hit(x, y, kGainDown)) return {ActionKind::gain_step, -1};
    if (hit(x, y, kGainUp)) return {ActionKind::gain_step, 1};
  }
  if (hit(x, y, kRails)) return {ActionKind::toggle_rails, 0};
  if (hit(x, y, kIfChip)) return {ActionKind::cycle_if, 0};
  if (hit(x, y, kApply)) {
    const Metrics m = metrics();
    const auto error = sstv_tune::tuning_error(static_cast<sstv_tune::Demod>(g_ui.tune.demod), m.sync_tone_hz, m.sync_tone_pulses);
    if (error.valid && std::abs(error.correction_hz) >= 5) return {ActionKind::apply_correction, error.correction_hz};
    return {};
  }
  if (x >= kRfX && x < kRfX + kRfW && y >= kRfY && y < kRfY + kRfH) {
    const uint32_t rate = g_ui.tune.rate_sps ? g_ui.tune.rate_sps : 240000;
    const uint32_t hz = sstv_tune::from_touch(g_ui.frequency_hz, rate, static_cast<int>(x - kRfX), kRfW,
                                              sstv_tune::step_hz(g_ui.tune.step_index));
    return {ActionKind::tune_hz, static_cast<int32_t>(hz)};
  }
  for (int i = 0; i < static_cast<int>(std::size(kProfiles)); ++i)
    if (hit(x, y, preset_chip(i))) { g_profile = static_cast<uint8_t>(i); return {ActionKind::tune_profile, i}; }
  return {};
}
Action touch_keypad(int32_t x, int32_t y) {
  const auto result = orcsdr::freq_keypad::handle_touch(x, y, g_entry, sizeof(g_entry));
  if (result == orcsdr::freq_keypad::Result::cancelled) {
    g_keypad = false;
    g_entry[0] = '\0';
    draw_all();
  } else if (result == orcsdr::freq_keypad::Result::submitted) {
    uint32_t hz = 0;
    if (sstv_tune::parse_entry(g_entry, &hz)) {
      g_keypad = false;
      g_entry[0] = '\0';
      draw_all();
      return {ActionKind::tune_hz, static_cast<int32_t>(hz)};
    }
    g_entry[0] = '\0';
    draw_keypad_screen();   // refused (out of range or malformed): start over
  }
  return {};
}

// ---------------------------------------------------------------- SETUP
void draw_setup() {
  const auto& s = g_ui.settings;
  frame(kModePanel, kGrid);
  text("MANUAL MODE  (VIS FILTER)", 46, 128, kCyan, 1, middle_left);
  text("Starts an image only when a VIS header names this mode. No VIS, no image.", 46, 158, kMuted, 0, middle_left);
  for (int i = 1; i <= 14; ++i)
    button(mode_button(i), mode_name(static_cast<Mode>(i)),
           s.detection == Detection::manual && static_cast<int>(s.manual_mode) == i);
  frame(kDetectPanel, kGrid);
  text("DETECTION", kDetectPanel.x + 20, 128, kCyan, 1, middle_left);
  button(kAuto, "AUTO: VIS + SYNC RECOVERY (DEFAULT)", s.detection == Detection::automatic, true, 2);
  button(kVisOnly, "VIS ONLY (NO SYNC FALLBACK)", s.detection == Detection::vis_only, true, 2);
  char manual[64];
  std::snprintf(manual, sizeof(manual), "MANUAL: %s", mode_name(s.manual_mode));
  button(kManual, manual, s.detection == Detection::manual, true, 2);
  text("STORAGE", kDetectPanel.x + 20, 350, kCyan, 1, middle_left);
  button(kAutosave, s.autosave ? "AUTO-SAVE TO SD: ON" : "AUTO-SAVE TO SD: OFF", s.autosave, true, 2);
  const auto st = arc::stats();
  char line[96];
  const int x = kDetectPanel.x + 20;
  text(g_ui.sd_ready ? "SD card mounted" : "SD unavailable - images stay in memory", x, 456,
       g_ui.sd_ready ? kGreen : kAmber, 0, middle_left);
  std::snprintf(line, sizeof(line), "Saved %lu   Waiting %lu   Failed %lu", (unsigned long)st.saved,
                (unsigned long)st.pending, (unsigned long)st.failed);
  text(line, x, 484, kMuted, 0, middle_left);
  if (st.last_error != arc::Error::none) {
    std::snprintf(line, sizeof(line), "Last problem: %s", arc::error_name(st.last_error));
    text(line, x, 512, kAmber, 0, middle_left);
  } else {
    text("/orcsdr/sstv/index.csv (schema v2)", x, 512, kMuted, 0, middle_left);
  }
  text("14 modes, full colour (Robot, Martin, Scottie, PD)", x, 540, kMuted, 0, middle_left);
}

void draw_page_body() {
  BodyClip clip;
  switch (g_page) {
    case Page::receive: draw_receive(); break;
    case Page::gallery: draw_gallery(); break;
    case Page::analyzer: draw_analyzer(); break;
    case Page::tune: if (g_keypad) draw_keypad_screen(); else draw_tune(); break;
    case Page::setup: draw_setup(); break;
    default: break;
  }
}
void draw_all() {
  if (!g_active) return;
  DrawTimer timer;
  M5.Display.startWrite();
  g_preview_source = nullptr; g_painted_screen_rows = 0; g_painted_source_rows = 0;
  M5.Display.fillScreen(TFT_BLACK);
  focus_nav::clear();
  reset_signatures();
  draw_header();
  draw_page_body();
  draw_tabs();
  M5.Display.endWrite();
  g_last_render_ms = millis();
  g_last_revision = image_revision();
}
} // namespace

void set_header_hook(void (*draw_controls)()) { g_header_hook = draw_controls; }
void ui_enter(const UiSnapshot& snapshot) {
  g_ui = snapshot; g_active = true; g_page = Page::receive; g_keypad = false; draw_all();
}
void ui_leave() {
  g_active = false; g_keypad = false;
  free_tune_buffers();
}
bool ui_active() { return g_active; }
void ui_update(const UiSnapshot& snapshot) {
  if (!g_active) return;
  DrawTimer timer;
  const bool retuned = g_ui.frequency_hz != snapshot.frequency_hz;
  const bool changed = std::memcmp(&g_ui.settings, &snapshot.settings, sizeof(g_ui.settings)) != 0 ||
                       g_ui.sd_ready != snapshot.sd_ready || g_ui.tune != snapshot.tune;
  g_ui = snapshot;
  if (retuned) {                    // the waterfall history belongs to the old frequency
    g_rf_have = false;
    if (g_rf_px) std::fill(g_rf_px, g_rf_px + static_cast<size_t>(kRfW) * kRfH, static_cast<uint16_t>(0));
  }
  if (g_keypad) return;             // the keypad owns the screen
  if (retuned || changed) {
    if (g_page == Page::tune) {   // repaint in place: no full-screen clear
      M5.Display.startWrite();
      draw_header();
      draw_page_body();
      M5.Display.endWrite();
      g_last_render_ms = millis();
      return;
    }
    draw_all();
    return;
  }
  const uint32_t revision = image_revision();
  const uint32_t now = millis();
  if (g_page == Page::receive) {
    M5.Display.startWrite();
    if (revision != g_last_revision) {
      paint_image_rows(live_image(), kImagePanel);
      g_last_revision = revision;
    }
    if (now - g_last_render_ms >= 1000u) {
      draw_receive_stats();
      draw_header();
      g_last_render_ms = now;
    }
    M5.Display.endWrite();
  } else if (g_page == Page::tune) {
    update_tune(now);
    // The header repaints when what it shows changes, and otherwise every ten seconds (battery, clock).
    uint32_t sig = 2166136261u;
    for (uint32_t v : {g_ui.frequency_hz, static_cast<uint32_t>(metrics().state), static_cast<uint32_t>(g_ui.receiver_ready),
                       static_cast<uint32_t>(g_ui.sd_ready), static_cast<uint32_t>(g_ui.streaming),
                       static_cast<uint32_t>(g_notice[0] && millis() < g_notice_until)})
      sig = mix(sig, v);
    if (sig != g_sig_header || now - g_sig_header_ms >= 10000u) {
      g_sig_header = sig;
      g_sig_header_ms = now;
      M5.Display.startWrite();
      draw_header();
      M5.Display.endWrite();
    }
    g_last_render_ms = now;
  } else if (revision != g_last_revision) {
    draw_all();
  } else if (now - g_last_render_ms >= 1000u) {
    // Live pages refresh their body once a second without repainting the whole screen.
    M5.Display.startWrite();
    draw_page_body();
    draw_header();
    M5.Display.endWrite();
    g_last_render_ms = now;
  }
}
UiStats ui_stats() { return g_ui_stats; }
Page ui_page() { return g_page; }
uint8_t ui_profile() { return g_profile; }
void ui_set_profile(uint8_t index) { g_profile = index; if (g_active) draw_all(); }
void ui_set_tune(const TuneView& tune) {
  if (g_ui.tune == tune) return;
  g_ui.tune = tune;
  if (g_active && g_page == Page::tune && !g_keypad) draw_all();
}
bool ui_wants_spectrum() { return tune_visible() && g_rf_px != nullptr; }
// One row of the RF display: the spectrum trace on top and the waterfall history below it. Called from the UI task
// whenever the application has a fresh spectrum.
void ui_feed_spectrum(const float* levels, size_t bins, uint32_t rate_sps) {
  if (!tune_visible() || !g_rf_px || !levels || bins < 64) return;
  // Column level: the strongest bin under each pixel.
  auto column_level = [&](int x) {
    const size_t b0 = static_cast<size_t>(x) * bins / kRfW;
    const size_t b1 = std::max(b0 + 1, static_cast<size_t>(x + 1) * bins / kRfW);
    float peak = levels[b0];
    for (size_t b = b0 + 1; b < b1 && b < bins; ++b) peak = std::max(peak, levels[b]);
    return peak;
  };
  float sum = 0;
  for (int x = 0; x < kRfW; ++x) sum += column_level(x);
  // The noise floor follows the average level (a few dB under it); the plot shows 42 dB above that.
  const float floor_now = sum / kRfW - 3.f;
  g_rf_floor = g_rf_floor_known ? 0.92f * g_rf_floor + 0.08f * floor_now : floor_now;
  g_rf_floor_known = true;
  for (int x = 0; x < kRfW; ++x)
    g_rf_cols[x] = static_cast<uint8_t>(std::clamp((column_level(x) - g_rf_floor) * (255.f / 42.f), 0.f, 255.f));
  // Waterfall: history moves down one row, the new row goes on top of it.
  uint16_t* wf = g_rf_px + static_cast<size_t>(kRfSpecH) * kRfW;
  std::memmove(wf + kRfW, wf, static_cast<size_t>(kRfW) * (kRfWfH - 1) * sizeof(uint16_t));
  for (int x = 0; x < kRfW; ++x) wf[x] = g_palette[g_rf_cols[x]];
  // Spectrum: a filled trace, brighter at its top.
  std::fill(g_rf_px, g_rf_px + static_cast<size_t>(kRfW) * kRfSpecH, static_cast<uint16_t>(0));
  for (int x = 0; x < kRfW; ++x) {
    const int h = g_rf_cols[x] * (kRfSpecH - 2) / 255;
    for (int k = 0; k < h; ++k) g_rf_px[static_cast<size_t>(kRfSpecH - 1 - k) * kRfW + x] = g_palette[40 + g_rf_cols[x] / 4];
    if (h) g_rf_px[static_cast<size_t>(kRfSpecH - 1 - h) * kRfW + x] = g_palette[g_rf_cols[x]];
  }
  g_rf_have = true;
  (void)rate_sps;
  M5.Display.startWrite();
  push_rf();
  M5.Display.endWrite();
}
void ui_set_settings(const sstv_settings::Settings& settings) { g_ui.settings = settings; draw_all(); }
void ui_notice(const char* msg) {
  if (!msg) msg = "";
  std::snprintf(g_notice, sizeof(g_notice), "%s", msg);
  g_notice_until = millis() + 4500;
  draw_all();
}
Action ui_touch(int32_t x, int32_t y) {
  if (!g_active) return {};
  if (g_keypad) return touch_keypad(x, y);
  if (y >= kTabsY) {
    for (int i = 0; i < kTabCount; ++i)
      if (hit(x, y, {i * kTabW + 4, kTabsY + 4, kTabW - 8, 82})) {
        g_page = static_cast<Page>(i);
        g_keypad = false;
        draw_all();
        return {};
      }
    return {};
  }
  if (g_page == Page::receive) {
    if (hit(x, y, kAbort)) return {ActionKind::abort_rx, 0};
    if (hit(x, y, kCapture)) return {ActionKind::save_image, -1};
  } else if (g_page == Page::gallery) {
    if (hit(x, y, kSave)) return {ActionKind::save_image, static_cast<int32_t>(g_gallery_selected)};
    if (hit(x, y, kClearGallery)) return {ActionKind::clear_gallery, 0};
    for (int i = 0; i < 4; ++i)
      if (hit(x, y, tile(i))) { g_gallery_selected = i; draw_all(); break; }
  } else if (g_page == Page::tune) {
    return touch_tune(x, y);
  } else if (g_page == Page::setup) {
    for (int i = 1; i <= 14; ++i)
      if (hit(x, y, mode_button(i))) return {ActionKind::select_mode, i};
    if (hit(x, y, kAuto)) return {ActionKind::set_detection, static_cast<int32_t>(Detection::automatic)};
    if (hit(x, y, kVisOnly)) return {ActionKind::set_detection, static_cast<int32_t>(Detection::vis_only)};
    if (hit(x, y, kManual)) return {ActionKind::select_mode, static_cast<int32_t>(g_ui.settings.manual_mode)};
    if (hit(x, y, kAutosave)) return {ActionKind::toggle_autosave, 0};
  }
  return {};
}
} // namespace orcsdr::sstv
