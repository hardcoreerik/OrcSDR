#include "sstv_dashboard.hpp"
#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include "dashboard_audio_control.hpp"
#include "focus_nav.hpp"
#include "sstv_archive.hpp"
#include "sstv_auto_detect.hpp"
#include "sstv_status.hpp"
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
constexpr const char* kPages[kTabCount] = {"RECEIVE", "IMAGES", "SIGNAL", "FREQUENCIES", "SETUP"};

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
// FREQUENCIES
Rect profile_row(int i) { return {42, 160 + i * 112, 1196, 100}; }
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
  const int dw = std::max(1, std::min(view_w, static_cast<int>(fit * image.width)));
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

// ---------------------------------------------------------------- FREQUENCIES
void draw_frequencies() {
  frame(kBody, kGrid);
  text("QUICK TUNE / LOCAL + HF", 46, 128, kCyan, 1, middle_left);
  text("Preset frequencies are starting points, not activity guarantees.", 46, 156, kMuted, 0, middle_left);
  for (size_t i = 0; i < std::size(kProfiles); ++i) {
    const Rect r = profile_row(static_cast<int>(i));
    const bool on = i == g_profile;
    focus_nav::note(r.x, r.y, r.w, r.h);
    M5.Display.fillRoundRect(r.x, r.y, r.w, r.h, 8, on ? kSelected : TFT_BLACK);
    M5.Display.drawRoundRect(r.x, r.y, r.w, r.h, 8, on ? kGreen : kGrid);
    char f[44];
    std::snprintf(f, sizeof(f), "%0.5f MHz", kProfiles[i].hz / 1e6);
    text(kProfiles[i].name, r.x + 20, r.y + 32, TFT_WHITE, 1, middle_left);
    text(f, r.x + 340, r.y + 32, kCyan, 1, middle_left);
    text(kProfiles[i].usb ? "USB" : "NFM", r.x + 640, r.y + 32, kGreen, 0, middle_left);
    text(kProfiles[i].description, r.x + 20, r.y + 70, kMuted, 0, middle_left);
  }
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
  text("14 modes, monochrome preview (colour is not decoded)", x, 540, kMuted, 0, middle_left);
}

void draw_page_body() {
  switch (g_page) {
    case Page::receive: draw_receive(); break;
    case Page::gallery: draw_gallery(); break;
    case Page::analyzer: draw_analyzer(); break;
    case Page::frequencies: draw_frequencies(); break;
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
  g_ui = snapshot; g_active = true; g_page = Page::receive; draw_all();
}
void ui_leave() { g_active = false; }
bool ui_active() { return g_active; }
void ui_update(const UiSnapshot& snapshot) {
  if (!g_active) return;
  DrawTimer timer;
  const bool retuned = g_ui.frequency_hz != snapshot.frequency_hz;
  const bool changed = std::memcmp(&g_ui.settings, &snapshot.settings, sizeof(g_ui.settings)) != 0 ||
                       g_ui.sd_ready != snapshot.sd_ready;
  g_ui = snapshot;
  if (retuned || changed) { draw_all(); return; }
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
  } else if (g_page == Page::frequencies) {
    if (now - g_last_render_ms >= 3000u) {
      M5.Display.startWrite();
      draw_header();
      M5.Display.endWrite();
      g_last_render_ms = now;
    }
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
void ui_set_settings(const sstv_settings::Settings& settings) { g_ui.settings = settings; draw_all(); }
void ui_notice(const char* msg) {
  if (!msg) msg = "";
  std::snprintf(g_notice, sizeof(g_notice), "%s", msg);
  g_notice_until = millis() + 4500;
  draw_all();
}
Action ui_touch(int32_t x, int32_t y) {
  if (!g_active) return {};
  if (y >= kTabsY) {
    for (int i = 0; i < kTabCount; ++i)
      if (hit(x, y, {i * kTabW + 4, kTabsY + 4, kTabW - 8, 82})) {
        g_page = static_cast<Page>(i);
        draw_all();
        return {};
      }
    return {};
  }
  if (g_page == Page::receive) {
    if (hit(x, y, kAbort)) return {ActionKind::abort_rx, 0};
    if (hit(x, y, kCapture)) return {ActionKind::save_image, live_image().decoded_rows > 0 ? -1 : 0};
  } else if (g_page == Page::gallery) {
    if (hit(x, y, kSave)) return {ActionKind::save_image, static_cast<int32_t>(g_gallery_selected)};
    if (hit(x, y, kClearGallery)) return {ActionKind::clear_gallery, 0};
    for (int i = 0; i < 4; ++i)
      if (hit(x, y, tile(i))) { g_gallery_selected = i; draw_all(); break; }
  } else if (g_page == Page::frequencies) {
    for (int i = 0; i < static_cast<int>(std::size(kProfiles)); ++i)
      if (hit(x, y, profile_row(i))) { g_profile = i; draw_all(); return {ActionKind::tune_profile, i}; }
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
