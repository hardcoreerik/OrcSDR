#include "ft8_compact_dashboard.hpp"

#include "clock_sync_status.hpp"
#include "dashboard_audio_control.hpp"
#include "ft8_decoder_backend.hpp"

#include <M5Unified.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <new>

namespace orcsdr::ft8::compact {
namespace {

constexpr uint16_t kPanel = 0x0841, kGrid = 0x2945, kCyan = 0x2e7f, kGreen = 0x6fe8, kYellow = 0xff24, kAmber = 0xfd20,
                   kMuted = 0x8c71, kSelected = 0x1264, kPlotBg = 0x0021;

// All of this dashboard's static state lives in PSRAM (zero-initialised): internal RAM has almost no slack at boot, and a screen that is
// linked in costs its statics even when it is never opened. Spending about 2 KB of it here made Home's raster buffer fall back to PSRAM.
EXT_RAM_BSS_ATTR Layout g_layout;
Snapshot* g_snapshot = nullptr;       // PSRAM: it holds the decode list
uint16_t* g_line = nullptr;           // one waterfall row (two pixels high), PSRAM
bool g_active = false;
Tab g_tab = Tab::live;
size_t g_page = 0;
void (*g_header_hook)() = nullptr;

// What each region last drew, so an update repaints only what changed.
struct Drawn {
  char chip[6][80]{};                 // dial, clock title, clock value, gain, status, band
  uint32_t wf_sequence = 0;           // set to UINT32_MAX by enter() and the waterfall panel: "nothing painted yet"
  uint32_t tenth = 0;
  uint64_t second = 0;
  size_t decode_count = 0;
  uint32_t last_decode_stamp = 0;
  int16_t battery = -2;
  uint32_t header_key = 0;
  char setup_key[160]{};
};
EXT_RAM_BSS_ATTR Drawn g_drawn;

bool hit(int32_t x, int32_t y, Rect r) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

void text(const char* value, int x, int y, uint16_t color, int size, textdatum_t datum) {
  M5.Display.setTextDatum(datum);
  if (size <= 1) {
    M5.Display.setFont(size == 1 ? &fonts::DejaVu24 : &fonts::DejaVu18);
    M5.Display.setTextSize(1);
  } else {
    M5.Display.setFont(nullptr);
    M5.Display.setTextSize(size);
  }
  M5.Display.setTextColor(color);
  M5.Display.drawString(value, x, y);
  if (size <= 1) M5.Display.setFont(nullptr);
}

// Draws `value` in the 24 px font when it fits the width, otherwise in the 18 px font.
void fit_text(const char* value, Rect r, uint16_t color, textdatum_t datum = middle_center) {
  M5.Display.setFont(&fonts::DejaVu24);
  M5.Display.setTextSize(1);
  const bool fits = M5.Display.textWidth(value) <= r.w - 8;
  M5.Display.setFont(nullptr);
  const int x = datum == middle_left ? r.x + 6 : datum == middle_right ? r.x + r.w - 6 : r.x + r.w / 2;
  text(value, x, r.y + r.h / 2, color, fits ? 1 : 0, datum);
}

void frame(Rect r, uint16_t border = kGrid) {
  M5.Display.fillRoundRect(r.x, r.y, r.w, r.h, 8, kPanel);
  M5.Display.drawRoundRect(r.x, r.y, r.w, r.h, 8, border);
}

void button(Rect r, const char* label, bool selected = false, bool enabled = true) {
  const uint16_t border = enabled ? (selected ? kGreen : kCyan) : TFT_DARKGREY;
  M5.Display.fillRoundRect(r.x, r.y, r.w, r.h, 8, selected ? kSelected : kPanel);
  M5.Display.drawRoundRect(r.x, r.y, r.w, r.h, 8, border);
  fit_text(label, r, enabled ? (selected ? kGreen : TFT_WHITE) : kMuted);
}

// A small status chip: a 18 px title over a value that is as large as the chip allows.
void chip(Rect r, const char* title, const char* value, uint16_t color) {
  frame(r);
  text(title, r.x + r.w / 2, r.y + 12, kCyan, 0, middle_center);
  fit_text(value, {r.x, r.y + 14, r.w, r.h - 14}, color);
}

void format_utc(char* out, size_t size, uint32_t epoch) {
  if (out == nullptr || size == 0) return;
  out[0] = 0;
  if (epoch == 0) return;
  const time_t raw = static_cast<time_t>(epoch);
  tm value{};
  if (gmtime_r(&raw, &value) == nullptr) return;
  std::snprintf(out, size, "%02d:%02d:%02d", value.tm_hour, value.tm_min, value.tm_sec);
}

const char* decoder_name() {
  switch (g_snapshot->decoder_state) {
    case DecoderState::unbound: return "UNBOUND";
    case DecoderState::armed: return "ARMED";
    case DecoderState::listening: return "LISTENING";
    case DecoderState::decoding: return "DECODING";
    case DecoderState::ready: return "READY";
    case DecoderState::error: return "ERROR";
    case DecoderState::no_memory: return "NO MEMORY";
  }
  return "UNBOUND";
}

uint16_t decoder_color() {
  switch (g_snapshot->decoder_state) {
    case DecoderState::armed:
    case DecoderState::listening:
    case DecoderState::ready: return kGreen;
    case DecoderState::decoding: return kYellow;
    case DecoderState::error:
    case DecoderState::no_memory: return TFT_RED;
    case DecoderState::unbound: return kAmber;
  }
  return kMuted;
}

const Decode* newest(size_t offset) {
  const Snapshot& s = *g_snapshot;
  if (offset >= s.decode_count || s.decode_count > kDecodeCapacity) return nullptr;
  return &s.decodes[s.decode_count - 1 - offset];
}

size_t unique_calls() {
  const Snapshot& s = *g_snapshot;
  size_t total = 0;
  const size_t n = std::min(s.decode_count, kDecodeCapacity);
  for (size_t i = 0; i < n; ++i) {
    if (!s.decodes[i].callsign[0]) continue;
    bool seen = false;
    for (size_t j = 0; j < i && !seen; ++j) seen = std::strcmp(s.decodes[i].callsign, s.decodes[j].callsign) == 0;
    if (!seen) ++total;
  }
  return total;
}

size_t cq_count() {
  const Snapshot& s = *g_snapshot;
  size_t total = 0;
  for (size_t i = 0; i < std::min(s.decode_count, kDecodeCapacity); ++i) total += s.decodes[i].kind == DecodeKind::cq ? 1u : 0u;
  return total;
}

uint16_t waterfall_color(uint8_t v) {
  EXT_RAM_BSS_ATTR static uint16_t lut[256];
  EXT_RAM_BSS_ATTR static bool ready;
  if (!ready) {
    for (int i = 0; i < 256; ++i) {
      const float t = static_cast<float>(i) / 255.0f;
      float r, g, b;
      if (t < 0.25f) { r = 0.0f; g = t * 2.0f; b = 0.12f + t * 3.0f; }
      else if (t < 0.5f) { r = 0.0f; g = 0.5f + (t - 0.25f) * 2.0f; b = 0.87f - (t - 0.25f) * 3.4f; }
      else if (t < 0.75f) { r = (t - 0.5f) * 4.0f; g = 1.0f; b = 0.0f; }
      else { r = 1.0f; g = 1.0f - (t - 0.75f) * 4.0f; b = 0.0f; }
      auto c01 = [](float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); };
      const uint16_t rgb = static_cast<uint16_t>((static_cast<uint16_t>(c01(r) * 31.0f) << 11) |
                                                 (static_cast<uint16_t>(c01(g) * 63.0f) << 5) |
                                                 static_cast<uint16_t>(c01(b) * 31.0f));
      lut[i] = static_cast<uint16_t>((rgb >> 8) | (rgb << 8));   // pushImage takes byte-swapped RGB565 on this panel
    }
    ready = true;
  }
  return lut[v];
}

// ---------------------------------------------------------------- header and tabs

void draw_header() {
  const Layout& l = g_layout;
  const auto& header = audio_header::layout();
  const Rect brand_area{l.viewport.x, l.viewport.y, l.viewport.w, header.content_top - l.viewport.y};
  M5.Display.fillRect(brand_area.x, brand_area.y, brand_area.w, brand_area.h, TFT_BLACK);
  audio_header::draw_brand(mode_name(g_snapshot->mode));
  if (header.status.w >= 100) {
    char title[24];
    std::snprintf(title, sizeof(title), "%s RECEIVE", mode_name(g_snapshot->mode));
    text(title, header.status.x + 8, header.status.y + header.status.h / 2, TFT_WHITE, header.status.w >= 200 ? 1 : 0, middle_left);
  }
  M5.Display.drawFastHLine(l.viewport.x + 8, header.content_top - 4, l.viewport.w - 16, kGreen);
  if (g_header_hook != nullptr) g_header_hook();
  g_drawn.header_key = static_cast<uint32_t>(g_snapshot->mode) | (static_cast<uint32_t>(g_snapshot->battery_percent & 0xff) << 8);
}

void draw_tabs() {
  static constexpr const char* labels[kTabCount] = {"LIVE", "DECODES", "SETUP"};
  for (int i = 0; i < kTabCount; ++i) button(g_layout.tab[i], labels[i], static_cast<int>(g_tab) == i);
}

// ---------------------------------------------------------------- LIVE

void band_text(char* out, size_t size) {
  const BandPreset* preset = band(g_snapshot->selected_band);
  const uint32_t dial = g_snapshot->dial_hz != 0 ? g_snapshot->dial_hz : mode_dial_hz(g_snapshot->selected_band, g_snapshot->mode);
  if (preset != nullptr && dial != 0)
    std::snprintf(out, size, "%s  %.4f", g_snapshot->dial_custom ? "CUSTOM" : preset->label, static_cast<double>(dial) / 1e6);
  else
    std::snprintf(out, size, "--");
}

void clock_text(char* title, size_t title_size, char* value, size_t value_size, uint16_t* color) {
  const auto level = static_cast<clock_sync::Level>(g_snapshot->clock_sync_level);
  clock_sync::chip_text(level, g_snapshot->clock_sync_age_s, title, title_size, value, value_size);
  *color = (level == clock_sync::Level::synced || level == clock_sync::Level::signal) ? kGreen
         : level == clock_sync::Level::syncing ? kCyan : kAmber;
}

bool differs(char (&slot)[80], const char* value) {
  if (std::strncmp(slot, value, sizeof(slot) - 1) == 0) return false;
  std::snprintf(slot, sizeof(slot), "%s", value);
  return true;
}

void draw_live_chips(bool force) {
  const Snapshot& s = *g_snapshot;
  char value[48], title[40], clock_value[24];
  uint16_t color = kGreen;
  band_text(value, sizeof(value));
  if (force || differs(g_drawn.chip[0], value)) chip(g_layout.dial, s.dial_custom ? "DIAL (CUSTOM)" : "BAND  MHz", value, s.dial_custom ? kAmber : TFT_WHITE);
  clock_text(title, sizeof(title), clock_value, sizeof(clock_value), &color);
  char combined[72];
  std::snprintf(combined, sizeof(combined), "%s|%s", title, clock_value);
  if (force || differs(g_drawn.chip[1], combined)) chip(g_layout.clock, title, clock_value, color);
  if (s.gain_auto) std::snprintf(value, sizeof(value), "AUTO");
  else std::snprintf(value, sizeof(value), "MAN %.1f dB", s.gain_tenth_db / 10.0);
  if (force || differs(g_drawn.chip[2], value)) chip(g_layout.gain, s.gain_auto ? "GAIN" : "GAIN - TAP: AUTO", value, TFT_WHITE);
  std::snprintf(value, sizeof(value), "%s", decoder_name());
  if (force || differs(g_drawn.chip[3], value)) chip(g_layout.status, "DECODER", value, decoder_color());
  if (force) {
    button(g_layout.band_prev, "<");
    button(g_layout.band_next, ">");
  }
}

void draw_waterfall_axis() {
  const Rect p = g_layout.wf_plot;
  for (int hz = 500; hz <= 3000; hz += 500) {
    const int x = p.x + (hz - kAudioLowHz) * p.w / (kAudioHighHz - kAudioLowHz);
    M5.Display.drawFastVLine(x, p.y, p.h, kGrid);
  }
}

// Paints waterfall rows. A first paint, a restart or a long gap repaints the whole plot; otherwise the plot is scrolled and only the
// newest rows (at most four per call, so a burst is spread over a few updates) are pushed, as the legacy dashboard does. Repainting every
// row on every update kept the UI task at 100% of its core.
void draw_waterfall_rows() {
  const Snapshot& s = *g_snapshot;
  const Rect p = g_layout.wf_plot;
  if (s.waterfall == nullptr || s.wf_rows == 0 || s.wf_bins == 0 || s.wf_sequence == 0 || g_line == nullptr) return;
  const size_t px_rows = static_cast<size_t>(p.h) / 2;
  const size_t rows = std::min<size_t>(s.wf_rows, 112);
  const uint32_t seq = s.wf_sequence;
  auto push_row = [&](uint32_t sequence_number_of_row, int y) {
    const uint8_t* src = s.waterfall + (sequence_number_of_row % rows) * s.wf_bins;
    for (int x = 0; x < p.w; ++x) {
      const uint16_t color = waterfall_color(src[static_cast<size_t>(x) * s.wf_bins / static_cast<size_t>(p.w)]);
      g_line[x] = color;
      g_line[p.w + x] = color;
    }
    M5.Display.pushImage(p.x, y, p.w, 2, g_line);
  };
  const bool full = g_drawn.wf_sequence == UINT32_MAX || seq < g_drawn.wf_sequence || seq - g_drawn.wf_sequence >= 24;
  if (full) {
    const size_t visible = std::min<size_t>(std::min<size_t>(rows, seq), px_rows);
    for (size_t r = 0; r < visible; ++r) push_row(static_cast<uint32_t>(seq - 1 - r), p.y + static_cast<int>(r) * 2);
    g_drawn.wf_sequence = seq;
  } else {
    const uint32_t fresh = std::min<uint32_t>(seq - g_drawn.wf_sequence, 4);
    if (fresh == 0) return;
    const uint32_t target = g_drawn.wf_sequence + fresh;
    M5.Display.setScrollRect(p.x, p.y, p.w, p.h, kPlotBg);
    M5.Display.scroll(0, static_cast<int>(fresh) * 2);
    for (uint32_t k = 0; k < fresh; ++k) push_row(target - 1 - k, p.y + static_cast<int>(k) * 2);
    g_drawn.wf_sequence = target;
  }
  draw_waterfall_axis();
}

// Returns the message for the plot area when there is nothing to draw (null = the waterfall can run).
const char* plot_message(uint16_t* color) {
  const Snapshot& s = *g_snapshot;
  *color = kAmber;
  if (s.decoder_state == DecoderState::no_memory) { *color = TFT_RED; return "NOT ENOUGH MEMORY"; }
  if (s.decoder_state == DecoderState::unbound) return "DECODER NOT RUNNING";
  if (!s.receiver_running) return "NO RECEIVER STREAMING";
  if (!s.clock_valid) return "UTC CLOCK REQUIRED";
  if (s.wf_sequence == 0) { *color = kMuted; return "LISTENING..."; }
  return nullptr;
}

void draw_waterfall_panel() {
  const Layout& l = g_layout;
  frame(l.wf_frame);
  char slot[48];
  std::snprintf(slot, sizeof(slot), "AUDIO 200-3000 Hz  %.0f s SLOT", slot_ms(g_snapshot->mode) / 1000.0);
  text(slot, l.wf_frame.x + 8, l.wf_frame.y + 14, kCyan, 0, middle_left);
  M5.Display.fillRect(l.wf_plot.x, l.wf_plot.y, l.wf_plot.w, l.wf_plot.h, kPlotBg);
  for (int hz = 500; hz <= 3000; hz += 500) {
    const int x = l.wf_plot.x + (hz - kAudioLowHz) * l.wf_plot.w / (kAudioHighHz - kAudioLowHz);
    char label[8];
    std::snprintf(label, sizeof(label), "%d", hz);
    // The last label would run past the right edge of the view: right-align it to the plot edge.
    const bool last = hz == 3000;
    text(label, last ? l.wf_plot.x + l.wf_plot.w : x, l.wf_plot.y + l.wf_plot.h + 11, kMuted, 0, last ? middle_right : middle_center);
  }
  g_drawn.wf_sequence = UINT32_MAX;
  uint16_t color;
  if (const char* message = plot_message(&color)) {
    text(message, l.wf_plot.x + l.wf_plot.w / 2, l.wf_plot.y + l.wf_plot.h / 2, color, 1, middle_center);
  } else {
    draw_waterfall_rows();
  }
}

void draw_timer() {
  const Snapshot& s = *g_snapshot;
  const Rect t = g_layout.timer;
  frame(t);
  const SlotClock slot = slot_clock(s.utc_ms, s.mode);
  g_drawn.tenth = s.clock_valid ? slot.remaining_ms / 100u : UINT32_MAX - 1;
  g_drawn.second = s.utc_ms / 1000u;
  const int half = t.w / 2;
  char value[40];
  if (s.clock_valid) {
    std::snprintf(value, sizeof(value), "%.1f", slot.remaining_ms / 1000.0);
    const int bar_w = static_cast<int>(static_cast<uint64_t>(half - 24) * slot.elapsed_ms / slot.period_ms);
    M5.Display.fillRect(t.x + 12, t.y + t.h - 18, half - 24, 8, kGrid);
    if (bar_w > 0) M5.Display.fillRect(t.x + 12, t.y + t.h - 18, bar_w, 8, kGreen);
  } else {
    std::snprintf(value, sizeof(value), "--.-");
  }
  text(value, t.x + half / 2, t.y + t.h / 2 - 8, TFT_WHITE, 4, middle_center);
  text("seconds", t.x + half / 2, t.y + t.h / 2 + 22, kMuted, 0, middle_center);
  char clock[16]{};
  if (s.clock_valid) format_utc(clock, sizeof(clock), static_cast<uint32_t>(s.utc_ms / 1000u));
  std::snprintf(value, sizeof(value), "UTC %s", clock[0] ? clock : "--:--:--");
  const int lx = t.x + half + 4;
  const int line = std::max(22, t.h / 4);
  text(value, lx, t.y + line / 2 + 4, s.clock_valid ? kGreen : kAmber, 0, middle_left);
  text(decoder_name(), lx, t.y + line + line / 2 + 4, decoder_color(), 0, middle_left);
  std::snprintf(value, sizeof(value), "%u candidates", s.candidate_count);
  text(value, lx, t.y + 2 * line + line / 2 + 4, TFT_WHITE, 0, middle_left);
  std::snprintf(value, sizeof(value), "last slot: %u", s.last_slot_decodes);
  text(value, lx, t.y + 3 * line + line / 2 + 4, kGreen, 0, middle_left);
}

void draw_live_list() {
  const Layout& l = g_layout;
  frame(l.live_list);
  text("LATEST DECODES", l.live_list.x + 10, l.live_list.y + 14, kCyan, 0, middle_left);
  for (int row = 0; row < l.live_rows; ++row) {
    const Decode* d = newest(static_cast<size_t>(row));
    const int y = l.live_list.y + 28 + row * 26 + 13;
    if (d == nullptr) {
      if (row == 0) text("none yet", l.live_list.x + 10, y, kMuted, 0, middle_left);
      break;
    }
    char utc[16] = "--:--:--", snr[12];
    format_utc(utc, sizeof(utc), d->utc_epoch);
    if (d->flags & decode_flag_snr_unavailable) std::snprintf(snr, sizeof(snr), "--");
    else std::snprintf(snr, sizeof(snr), "%+d", d->snr_db);
    const int x0 = l.live_list.x + 10;
    text(utc, x0, y, TFT_WHITE, 0, middle_left);
    text(snr, x0 + 92, y, TFT_WHITE, 0, middle_left);
    int mx = x0 + 140;
    if (!l.portrait) {
      char dt[16], df[12];
      std::snprintf(dt, sizeof(dt), "%+.1f", d->dt_ms / 1000.0);
      std::snprintf(df, sizeof(df), "%u", d->audio_hz);
      text(dt, x0 + 140, y, TFT_WHITE, 0, middle_left);
      text(df, x0 + 210, y, TFT_WHITE, 0, middle_left);
      mx = x0 + 290;
    }
    text(d->message, mx, y, d->kind == DecodeKind::cq ? kGreen : TFT_WHITE, 0, middle_left);
  }
}

void draw_live() {
  draw_live_chips(true);
  draw_waterfall_panel();
  draw_timer();
  draw_live_list();
}

// ---------------------------------------------------------------- DECODES

void draw_decodes() {
  const Layout& l = g_layout;
  const Snapshot& s = *g_snapshot;
  char value[32];
  std::snprintf(value, sizeof(value), "%u", static_cast<unsigned>(s.decode_count));
  chip(l.summary[0], "SESSION", value, kGreen);
  std::snprintf(value, sizeof(value), "%u", static_cast<unsigned>(unique_calls()));
  chip(l.summary[1], "UNIQUE CALLS", value, kGreen);
  std::snprintf(value, sizeof(value), "%u", static_cast<unsigned>(cq_count()));
  chip(l.summary[2], "CQ CALLS", value, kGreen);

  frame(l.list);
  const bool two = l.two_line_rows;
  const char* head = two ? "UTC       SNR   DT    DF    GRID" : "UTC       SNR   DT    DF    MESSAGE                      GRID";
  text(head, l.list.x + 10, l.list.y + 16, kCyan, 0, middle_left);
  M5.Display.drawFastHLine(l.list.x + 6, l.list.y + 30, l.list.w - 12, kGrid);
  const size_t pages = (s.decode_count + static_cast<size_t>(l.list_rows) - 1) / static_cast<size_t>(l.list_rows);
  if (g_page >= std::max<size_t>(pages, 1)) g_page = pages ? pages - 1 : 0;
  const size_t start = g_page * static_cast<size_t>(l.list_rows);
  for (int row = 0; row < l.list_rows; ++row) {
    const Decode* d = newest(start + static_cast<size_t>(row));
    if (d == nullptr) break;
    const int y = l.list.y + 32 + row * l.row_h;
    if (row & 1) M5.Display.fillRect(l.list.x + 4, y, l.list.w - 8, l.row_h, 0x0821);
    char utc[16] = "--:--:--", snr[12], dt[12], df[12];
    format_utc(utc, sizeof(utc), d->utc_epoch);
    if (d->flags & decode_flag_snr_unavailable) std::snprintf(snr, sizeof(snr), "--");
    else std::snprintf(snr, sizeof(snr), "%+d", d->snr_db);
    std::snprintf(dt, sizeof(dt), "%+.1f", d->dt_ms / 1000.0);
    std::snprintf(df, sizeof(df), "%u", d->audio_hz);
    const int x0 = l.list.x + 10;
    const int y1 = two ? y + l.row_h / 4 + 2 : y + l.row_h / 2;
    text(utc, x0, y1, TFT_WHITE, 0, middle_left);
    text(snr, x0 + 90, y1, TFT_WHITE, 0, middle_left);
    text(dt, x0 + 140, y1, TFT_WHITE, 0, middle_left);
    text(df, x0 + 200, y1, TFT_WHITE, 0, middle_left);
    const uint16_t message_color = d->kind == DecodeKind::cq ? kGreen : TFT_WHITE;
    if (two) {
      text(d->grid, x0 + 270, y1, maidenhead_valid(d->grid) ? kGreen : kMuted, 0, middle_left);
      text(d->message, x0, y + 3 * l.row_h / 4, message_color, 0, middle_left);
    } else {
      text(d->message, x0 + 270, y1, message_color, 0, middle_left);
      text(d->grid, l.list.x + l.list.w - 90, y1, maidenhead_valid(d->grid) ? kGreen : kMuted, 0, middle_left);
    }
  }
  if (s.decode_count == 0) text("no decodes yet", l.list.x + l.list.w / 2, l.list.y + l.list.h / 2, kMuted, 1, middle_center);
  char page[24];
  std::snprintf(page, sizeof(page), "%u/%u", static_cast<unsigned>(g_page + 1), static_cast<unsigned>(std::max<size_t>(pages, 1)));
  frame(l.page);
  fit_text(page, l.page, kMuted);
  button(l.newer, "NEWER", false, g_page > 0);
  button(l.older, "OLDER", false, (g_page + 1) * static_cast<size_t>(l.list_rows) < s.decode_count);
  button(l.clear, "CLEAR", false, s.decode_count > 0);
}

// ---------------------------------------------------------------- SETUP

void setup_row(int i, const char* label, const char* value, uint16_t color) {
  const Rect r = g_layout.info[i];
  frame(r);
  text(label, r.x + 10, r.y + r.h / 2, kCyan, 0, middle_left);
  fit_text(value, {r.x + r.w / 2 - 20, r.y, r.w / 2 + 20, r.h}, color, middle_right);
}

void draw_setup() {
  const Snapshot& s = *g_snapshot;
  const bool ft8_ok = (s.decoder_capabilities & decoder_cap_ft8) != 0;
  const bool ft4_ok = (s.decoder_capabilities & decoder_cap_ft4) != 0;
  button(g_layout.mode_ft8, "FT8", s.mode == DigitalMode::ft8, ft8_ok);
  button(g_layout.mode_ft4, "FT4", s.mode == DigitalMode::ft4, ft4_ok);
  char value[48], title[40], clock_value[24];
  uint16_t color;
  std::snprintf(value, sizeof(value), "%s, %.1f s slots", mode_name(s.mode), slot_ms(s.mode) / 1000.0);
  setup_row(0, "STANDARD", value, TFT_WHITE);
  setup_row(1, "DECODER", decoder_name(), decoder_color());
  clock_text(title, sizeof(title), clock_value, sizeof(clock_value), &color);
  setup_row(2, title, clock_value, color);
  band_text(value, sizeof(value));
  setup_row(3, "BAND / DIAL MHz", value, TFT_WHITE);
  std::snprintf(value, sizeof(value), s.receiver_running ? "STREAMING" : "NOT STREAMING");
  setup_row(4, "RECEIVER", value, s.receiver_running ? kGreen : kAmber);
  if (s.iq_dbfs > -89.0f) std::snprintf(value, sizeof(value), "%.0f dBFS%s", s.iq_dbfs, s.iq_clip_pct > 0.5f ? "  CLIPPING" : "");
  else std::snprintf(value, sizeof(value), "--");
  setup_row(5, "INPUT LEVEL", value, s.iq_clip_pct > 0.5f ? TFT_RED : TFT_WHITE);
  button(g_layout.sync_clock, "SYNC CLOCK");
  button(g_layout.open_clock, "SET CLOCK");
}

// ---------------------------------------------------------------- frame

void draw_body() {
  M5.Display.fillRect(g_layout.content.x, g_layout.content.y, g_layout.content.w, g_layout.content.h, TFT_BLACK);
  std::memset(g_drawn.chip, 0, sizeof(g_drawn.chip));
  switch (g_tab) {
    case Tab::live: draw_live(); break;
    case Tab::decodes: draw_decodes(); break;
    case Tab::setup: draw_setup(); break;
  }
}

bool alloc_buffers() {
  if (g_snapshot == nullptr) {
    void* memory = heap_caps_calloc(1, sizeof(Snapshot), MALLOC_CAP_SPIRAM);
    if (memory == nullptr) return false;
    g_snapshot = new (memory) Snapshot();
  }
  if (g_line == nullptr) {
    g_line = static_cast<uint16_t*>(heap_caps_malloc(2u * 1280u * sizeof(uint16_t), MALLOC_CAP_SPIRAM));
    if (g_line == nullptr) return false;
  }
  return true;
}

void take(const Snapshot& s) {
  *g_snapshot = s;
  g_snapshot->decode_count = std::min(g_snapshot->decode_count, kDecodeCapacity);
  g_snapshot->selected_band = std::min(g_snapshot->selected_band, band_count() - 1);
}

}  // namespace

bool set_viewport(Rect viewport) {
  const Layout next = make_layout(viewport);
  if (!next.valid || !audio_header::set_viewport(viewport)) return false;
  g_layout = next;
  return true;
}

void set_header_hook(void (*draw_controls)()) { g_header_hook = draw_controls; }
bool active() { return g_active; }
Tab tab() { return g_tab; }
size_t decode_page() { return g_page; }

void draw() {
  if (!g_active || !g_layout.valid) return;
  M5.Display.fillRect(g_layout.viewport.x, g_layout.viewport.y, g_layout.viewport.w, g_layout.viewport.h, TFT_BLACK);
  draw_header();
  draw_body();
  draw_tabs();
}

void enter(const Snapshot& snapshot) {
  if (!g_layout.valid || !alloc_buffers()) return;
  take(snapshot);
  g_active = true;
  g_tab = Tab::live;
  g_page = 0;
  g_drawn = Drawn{};
  g_drawn.wf_sequence = UINT32_MAX;
  draw();
}

void select_tab(Tab tab) {
  if (!g_active || tab == g_tab) return;
  g_tab = tab;
  g_page = 0;
  draw_tabs();
  draw_body();
}

void update(const Snapshot& snapshot) {
  if (!g_active) return;
  const size_t old_count = g_snapshot->decode_count;
  const uint32_t old_stamp = old_count ? g_snapshot->decodes[old_count - 1].utc_epoch : 0;
  const DecoderState old_state = g_snapshot->decoder_state;
  const bool old_running = g_snapshot->receiver_running, old_clock = g_snapshot->clock_valid;
  const int16_t old_battery = g_snapshot->battery_percent;
  take(snapshot);
  Snapshot& s = *g_snapshot;
  // The battery gauge is noisy and can read -1: ignore invalid readings and changes under 2 percent.
  if (s.battery_percent < 0 || s.battery_percent > 100) s.battery_percent = old_battery;
  else if (old_battery >= 0 && std::abs(s.battery_percent - old_battery) < 2) s.battery_percent = old_battery;
  const uint32_t header_key = static_cast<uint32_t>(s.mode) | (static_cast<uint32_t>(s.battery_percent & 0xff) << 8);
  if (header_key != g_drawn.header_key) draw_header();
  const uint32_t new_stamp = s.decode_count ? s.decodes[s.decode_count - 1].utc_epoch : 0;
  const bool list_changed = s.decode_count != old_count || new_stamp != old_stamp;
  const bool plot_state_changed = old_state != s.decoder_state || old_running != s.receiver_running || old_clock != s.clock_valid;

  switch (g_tab) {
    case Tab::live: {
      draw_live_chips(false);
      if (plot_state_changed) draw_waterfall_panel();
      uint16_t color;
      if (plot_message(&color) == nullptr && s.wf_sequence != g_drawn.wf_sequence) draw_waterfall_rows();
      const SlotClock slot = slot_clock(s.utc_ms, s.mode);
      const uint32_t tenth = s.clock_valid ? slot.remaining_ms / 100u : UINT32_MAX - 1;
      if (tenth != g_drawn.tenth || plot_state_changed || list_changed) draw_timer();
      if (list_changed) draw_live_list();
      break;
    }
    case Tab::decodes:
      if (list_changed) draw_decodes();
      break;
    case Tab::setup: {
      char key[160];
      char title[40], clock_value[24];
      uint16_t color;
      clock_text(title, sizeof(title), clock_value, sizeof(clock_value), &color);
      std::snprintf(key, sizeof(key), "%d|%d|%s|%s|%d|%d|%d|%u", static_cast<int>(s.mode), static_cast<int>(s.decoder_state), title,
                    clock_value, s.receiver_running ? 1 : 0, static_cast<int>(s.iq_dbfs), s.iq_clip_pct > 0.5f ? 1 : 0, static_cast<unsigned>(s.dial_hz));
      if (std::strncmp(g_drawn.setup_key, key, sizeof(g_drawn.setup_key) - 1) != 0) {
        std::snprintf(g_drawn.setup_key, sizeof(g_drawn.setup_key), "%s", key);
        draw_setup();
      }
      break;
    }
  }
}

Action handle_touch(int32_t x, int32_t y) {
  if (!g_active) return {};
  for (int i = 0; i < kTabCount; ++i)
    if (hit(x, y, g_layout.tab[i])) {
      select_tab(static_cast<Tab>(i));
      return {};
    }
  const Snapshot& s = *g_snapshot;
  switch (g_tab) {
    case Tab::live: {
      const size_t count = band_count();
      if (hit(x, y, g_layout.band_prev)) return {ActionKind::tune_band, static_cast<uint32_t>((s.selected_band + count - 1) % count)};
      if (hit(x, y, g_layout.band_next)) return {ActionKind::tune_band, static_cast<uint32_t>((s.selected_band + 1) % count)};
      if (hit(x, y, g_layout.dial) && s.dial_custom) return {ActionKind::tune_auto, 0};
      if (hit(x, y, g_layout.gain) && !s.gain_auto) return {ActionKind::gain_auto, 0};
      if (hit(x, y, g_layout.clock)) return {ActionKind::open_clock, 0};
      break;
    }
    case Tab::decodes:
      if (hit(x, y, g_layout.newer) && g_page > 0) {
        --g_page;
        draw_decodes();
      } else if (hit(x, y, g_layout.older) && (g_page + 1) * static_cast<size_t>(g_layout.list_rows) < s.decode_count) {
        ++g_page;
        draw_decodes();
      } else if (hit(x, y, g_layout.clear) && s.decode_count > 0) {
        g_page = 0;
        return {ActionKind::clear_decodes, 0};
      }
      break;
    case Tab::setup:
      if (hit(x, y, g_layout.mode_ft8) && (s.decoder_capabilities & decoder_cap_ft8)) return {ActionKind::select_mode, static_cast<uint32_t>(DigitalMode::ft8)};
      if (hit(x, y, g_layout.mode_ft4) && (s.decoder_capabilities & decoder_cap_ft4)) return {ActionKind::select_mode, static_cast<uint32_t>(DigitalMode::ft4)};
      if (hit(x, y, g_layout.sync_clock)) return {ActionKind::sync_clock, 0};
      if (hit(x, y, g_layout.open_clock)) return {ActionKind::open_clock, 0};
      break;
  }
  return {};
}

void leave() {
  g_active = false;
  if (g_line != nullptr) {
    heap_caps_free(g_line);
    g_line = nullptr;
  }
  if (g_snapshot != nullptr) {
    g_snapshot->~Snapshot();
    heap_caps_free(g_snapshot);
    g_snapshot = nullptr;
  }
}

}  // namespace orcsdr::ft8::compact
