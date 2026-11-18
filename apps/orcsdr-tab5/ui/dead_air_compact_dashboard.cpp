// Compact DEAD AIR dashboard. Everything shown comes from the Snapshot and the event model the application supplies; nothing is
// synthetic. The spectrum and waterfall are drawn straight onto the panel by draw_spectrum_frame().
#include "dead_air_compact_dashboard.hpp"
#include "dashboard_audio_control.hpp"
#include "dead_air_format.hpp"
#include "dead_air_sweep.hpp"

#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

namespace orcsdr::dead_air {
namespace cmp = compact;
namespace {

constexpr uint16_t kPanel = 0x0841, kGrid = 0x2945, kCyan = 0x2e7f, kGreen = 0x6fe8, kAmber = 0xfd20, kMuted = 0x8c71, kRed = 0xf800,
                   kSelected = 0x1264;

struct State {
  cmp::Layout layout;
  const Snapshot* snapshot = nullptr;
  EventModel* events = nullptr;
  Page page = Page::overview;
  size_t event_page = 0;
  bool active = false;
  uint16_t* waterfall = nullptr;   // PSRAM canvas, waterfall.w x waterfall.h RGB565
  float lo = -100.0f, hi = -40.0f;  // smoothed display range of the spectrum
  uint32_t epoch = 0xffffffffu;
  uint32_t drawn_freq = 0;
  uint32_t drawn_revision = 0xffffffffu, drawn_hist_seq = 0xffffffffu, drawn_elapsed = 0xffffffffu, drawn_events = 0xffffffffu;
};
State* g_state = nullptr;   // PSRAM
void (*g_header_controls)() = nullptr;

bool hit(int x, int y, cmp::Rect r) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

void text(const char* value, int x, int y, uint16_t color, int size, textdatum_t datum) {
  M5.Display.setTextDatum(datum);
  M5.Display.setFont(size == 1 ? &fonts::DejaVu24 : &fonts::DejaVu18);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(color);
  M5.Display.drawString(value, x, y);
  M5.Display.setFont(nullptr);
}

void clip(char* value, int width) {
  M5.Display.setFont(&fonts::DejaVu18);
  size_t n = std::strlen(value);
  while (n > 3 && M5.Display.textWidth(value) > width) {
    value[--n] = 0;
    if (n > 2) { value[n - 1] = '.'; value[n - 2] = '.'; }
  }
  M5.Display.setFont(nullptr);
}

void frame(cmp::Rect r) {
  M5.Display.fillRoundRect(r.x, r.y, r.w, r.h, 8, kPanel);
  M5.Display.drawRoundRect(r.x, r.y, r.w, r.h, 8, kGrid);
}

void button(cmp::Rect r, const char* label, bool selected = false, bool enabled = true) {
  const uint16_t border = enabled ? (selected ? kGreen : kCyan) : TFT_DARKGREY;
  M5.Display.fillRoundRect(r.x, r.y, r.w, r.h, 8, selected ? kSelected : kPanel);
  M5.Display.drawRoundRect(r.x, r.y, r.w, r.h, 8, border);
  M5.Display.setFont(&fonts::DejaVu24);
  const bool fits = M5.Display.textWidth(label) <= r.w - 10;
  M5.Display.setFont(nullptr);
  text(label, r.x + r.w / 2, r.y + r.h / 2, enabled ? (selected ? kGreen : TFT_WHITE) : kMuted, fits ? 1 : 0, middle_center);
}

uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

uint16_t heat(float t) {   // 0..1, dark blue -> cyan -> green -> yellow -> red
  t = std::clamp(t, 0.0f, 1.0f);
  float r, g, b;
  if (t < 0.25f) { r = 0; g = t * 4.0f; b = 0.3f + 0.7f * (t * 4.0f); }
  else if (t < 0.5f) { r = 0; g = 1.0f; b = 1.0f - (t - 0.25f) * 4.0f; }
  else if (t < 0.75f) { r = (t - 0.5f) * 4.0f; g = 1.0f; b = 0; }
  else { r = 1.0f; g = 1.0f - (t - 0.75f) * 4.0f; b = 0; }
  return M5.Display.color565(uint8_t(r * 255), uint8_t(g * 255), uint8_t(b * 255));
}

const char* page_name(int i) {
  static const char* const kNames[] = {"STATUS", "SPEC", "EVENTS", "SETUP"};
  return kNames[i];
}
Page tab_page(int i) {
  static const Page kPages[] = {Page::overview, Page::spectrum, Page::events, Page::review};
  return kPages[i];
}
int page_tab(Page p) {
  for (int i = 0; i < cmp::kTabCount; ++i) if (tab_page(i) == p) return i;
  return 0;
}

void draw_tabs(State& st) {
  const int current = page_tab(st.page);
  for (int i = 0; i < cmp::kTabCount; ++i) button(st.layout.tab[i], page_name(i), i == current);
}

void draw_header(State& st) {
  const auto& header = audio_header::layout();
  M5.Display.fillRect(st.layout.viewport.x, st.layout.viewport.y, st.layout.viewport.w, header.content_top - st.layout.viewport.y, TFT_BLACK);
  audio_header::draw_brand("DEAD AIR");
  if (header.status.w >= 100) text("DEAD AIR", header.status.x + 8, header.status.y + header.status.h / 2, TFT_WHITE, header.status.w >= 200 ? 1 : 0, middle_left);
  M5.Display.drawFastHLine(st.layout.viewport.x + 8, header.content_top - 4, st.layout.viewport.w - 16, kGreen);
  if (g_header_controls) g_header_controls();
}

void draw_overview(State& st) {
  const auto& l = st.layout;
  const Snapshot& s = *st.snapshot;
  char line[96];
  frame(l.status);
  std::snprintf(line, sizeof(line), "%.3f MHz  %s", s.frequency_hz / 1e6, s.demod);
  text(line, l.status.x + 10, l.status.y + 24, TFT_WHITE, 1, middle_left);
  const char* state = !s.receiver_ready ? "NO RECEIVER" : s.sweeping ? "SWEEPING" : !s.receiver_settled ? "SETTLING" : "LISTENING";
  text(state, l.status.x + 10, l.status.y + 56, s.receiver_ready ? kGreen : kAmber, 0, middle_left);
  char elapsed[24];
  format_duration(s.session_elapsed_s, elapsed, sizeof(elapsed));
  std::snprintf(line, sizeof(line), "session %s", elapsed);
  text(line, l.status.x + l.status.w - 10, l.status.y + 56, kMuted, 0, middle_right);
  if (l.status.h >= 80) {
    std::snprintf(line, sizeof(line), "%u events", unsigned(st.events ? st.events->total() : 0));
    text(line, l.status.x + 10, l.status.y + 78, kMuted, 0, middle_left);
  }

  frame(l.level);
  text("RF LEVEL (relative dBFS)", l.level.x + 10, l.level.y + 18, kCyan, 0, middle_left);
  if (s.rf_level_dbfs.valid) {
    std::snprintf(line, sizeof(line), "%.1f dBFS", s.rf_level_dbfs.value);
    text(line, l.level.x + 10, l.level.y + 48, TFT_WHITE, 1, middle_left);
    if (s.rf_level_dbfs.baseline_valid) {
      std::snprintf(line, sizeof(line), "baseline %.1f  (%+.1f dB)", s.rf_level_dbfs.baseline, s.rf_level_dbfs.value - s.rf_level_dbfs.baseline);
      text(line, l.level.x + 10, l.level.y + 76, kMuted, 0, middle_left);
    } else {
      text("learning the baseline...", l.level.x + 10, l.level.y + 76, kAmber, 0, middle_left);
    }
    const int bar_x = l.level.x + 10, bar_w = l.level.w - 20, bar_y = l.level.y + l.level.h - 22;
    M5.Display.drawRect(bar_x, bar_y, bar_w, 12, kGrid);
    const float t = std::clamp((s.rf_level_dbfs.value + 100.0f) / 100.0f, 0.0f, 1.0f);
    M5.Display.fillRect(bar_x + 1, bar_y + 1, int((bar_w - 2) * t), 10, kGreen);
  } else {
    text("no measurement", l.level.x + 10, l.level.y + 48, kAmber, 0, middle_left);
  }

  frame(l.history);
  text("RF LEVEL HISTORY", l.history.x + 10, l.history.y + 16, kCyan, 0, middle_left);
  const int top = l.history.y + 30, height = l.history.h - 40, x0 = l.history.x + 8, w = l.history.w - 16;
  if (s.rf_history_count >= 2 && height > 10 && w > 10) {
    const StripScale scale = strip_scale(s.rf_history, s.rf_history_count, false, 0, 0);
    int px = -1, py = 0;
    for (int i = 0; i < s.rf_history_count; ++i) {
      if (s.rf_history[i] == kHistGap) { px = -1; continue; }
      const int x = x0 + i * (w - 1) / std::max(1, int(s.rf_history_count) - 1);
      const int y = strip_y(s.rf_history[i] / 10.0f, scale, top, height);
      if (px >= 0) M5.Display.drawLine(px, py, x, y, kGreen);
      if (s.rf_history_event[i] != uint8_t(HistMark::none)) M5.Display.fillCircle(x, y, 4, s.rf_history_event[i] == uint8_t(HistMark::manual) ? kCyan : kAmber);
      px = x; py = y;
    }
    std::snprintf(line, sizeof(line), "%.0f", scale.hi);
    text(line, l.history.x + l.history.w - 10, l.history.y + 16, kMuted, 0, middle_right);
  } else {
    text("collecting...", l.history.x + 10, l.history.y + 50, kMuted, 0, middle_left);
  }
  button(l.scan_button, s.sweeping ? "STOP SCAN" : "SCAN", s.sweeping, s.receiver_ready);
  button(l.record_button, "MARK EVENT", false, st.events != nullptr);
}

void draw_spectrum_static(State& st) {
  const auto& l = st.layout;
  const Snapshot& s = *st.snapshot;
  frame(l.plot);
  frame(l.waterfall);
  if (st.waterfall) std::memset(st.waterfall, 0, size_t(l.waterfall.w) * l.waterfall.h * 2);
  button(l.tune_down, "-", false, s.receiver_ready && !s.sweeping);
  button(l.tune_up, "+", false, s.receiver_ready && !s.sweeping);
  char line[48];
  std::snprintf(line, sizeof(line), "%.3f MHz", s.frequency_hz / 1e6);
  button(l.frequency, line, false, false);
  st.drawn_freq = s.frequency_hz;
  if (!s.spectrum_valid) text("waiting for the spectrum...", l.plot.x + 10, l.plot.y + l.plot.h / 2, kMuted, 0, middle_left);
}

void draw_events(State& st) {
  const auto& l = st.layout;
  const size_t total = st.events ? st.events->count() : 0;
  const size_t per = l.portrait ? cmp::kEventRows : 3;
  const size_t pages = std::max<size_t>(1, (total + per - 1) / per);
  if (st.event_page >= pages) st.event_page = pages - 1;
  for (size_t i = 0; i < per; ++i) {
    const cmp::Rect r = l.row[i];
    frame(r);
    const size_t idx = st.event_page * per + i;
    if (idx >= total) {
      if (i == 0 && total == 0) text("no events logged yet", r.x + 10, r.y + r.h / 2, kMuted, 0, middle_left);
      continue;
    }
    const Event* e = st.events->newest(idx);
    if (!e) continue;
    char row[128];
    format_event_row(*e, row, sizeof(row));
    clip(row, r.w - 20);
    text(row, r.x + 10, r.y + r.h / 2, TFT_WHITE, 0, middle_left);
  }
  char line[32];
  std::snprintf(line, sizeof(line), "%u/%u", unsigned(st.event_page + 1), unsigned(pages));
  button(l.prev, "PREV", false, st.event_page > 0);
  button(l.label, line, false, false);
  button(l.next, "NEXT", false, st.event_page + 1 < pages);
}

int demod_index(const Snapshot& s) {
  for (int i = 0; i < kModeCount; ++i) if (std::strcmp(s.demod, kModeNames[i]) == 0) return i;
  return 0;
}

void draw_setup(State& st) {
  const auto& l = st.layout;
  const Snapshot& s = *st.snapshot;
  const bool locked = s.sweeping;
  char line[48];
  std::snprintf(line, sizeof(line), "MODE: %s", s.demod);
  button(l.setup_button[0], line, false, !locked && s.receiver_ready);
  std::snprintf(line, sizeof(line), "SWEEP: %s", sweep_preset(s.sweep_preset % kSweepPresetCount).name);
  button(l.setup_button[1], line, false, !locked);
  std::snprintf(line, sizeof(line), "DWELL: %u ms", unsigned(s.sweep_dwell_ms));
  button(l.setup_button[2], line, false, !locked);
  button(l.setup_button[3], s.clips_enabled ? "CLIPS: ON" : "CLIPS: OFF", s.clips_enabled);
  button(l.setup_button[4], s.chime_on ? "CHIME: ON" : "CHIME: OFF", s.chime_on);
  if (locked) text("The sweep owns tuning while it runs.", l.setup_button[0].x + 6, l.content.y + l.content.h - 12, kAmber, 0, middle_left);
}

void draw_body(State& st) {
  const auto& l = st.layout;
  M5.Display.fillRect(l.content.x, l.content.y, l.content.w, l.content.h, TFT_BLACK);
  switch (st.page) {
    case Page::overview: draw_overview(st); break;
    case Page::spectrum: draw_spectrum_static(st); break;
    case Page::events: draw_events(st); break;
    default: draw_setup(st); break;
  }
}

void draw_all(State& st) {
  M5.Display.fillRect(st.layout.viewport.x, st.layout.viewport.y, st.layout.viewport.w, st.layout.viewport.h, TFT_BLACK);
  draw_header(st);
  draw_body(st);
  draw_tabs(st);
}

void remember(State& st) {
  const Snapshot& s = *st.snapshot;
  st.drawn_revision = s.revision;
  st.drawn_hist_seq = s.rf_history_seq;
  st.drawn_elapsed = s.session_elapsed_s;
  st.drawn_events = st.events ? st.events->total() : 0;
}

void set_page(State& st, Page p) {
  if (st.page == p) return;
  st.page = p;
  st.epoch = 0xffffffffu;
  draw_body(st);
  draw_tabs(st);
  remember(st);
}
}  // namespace

bool ui_set_viewport(cmp::Rect viewport) {
  const cmp::Layout next = cmp::make_layout(viewport);
  if (!next.valid || !audio_header::set_viewport(viewport)) return false;
  if (g_state == nullptr) {
    void* memory = heap_caps_calloc(1, sizeof(State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (memory == nullptr) return false;
    g_state = new (memory) State();
  }
  State& st = *g_state;
  st.layout = next;
  if (st.waterfall) { heap_caps_free(st.waterfall); st.waterfall = nullptr; }   // the canvas follows the new size on the next entry
  return true;
}

void ui_set_header_controls(void (*draw)()) { g_header_controls = draw; }

void attach_events(EventModel* model) { if (g_state) g_state->events = model; }

void enter(const Snapshot& snapshot) {
  if (g_state == nullptr || !g_state->layout.valid) return;
  State& st = *g_state;
  st.snapshot = &snapshot;
  st.active = true;
  st.page = Page::overview;
  st.event_page = 0;
  st.epoch = 0xffffffffu;
  st.lo = -100.0f;
  st.hi = -40.0f;
  const size_t bytes = size_t(st.layout.waterfall.w) * st.layout.waterfall.h * 2;
  if (!st.waterfall) st.waterfall = static_cast<uint16_t*>(heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  draw_all(st);
  remember(st);
}

void update(const Snapshot& snapshot) {
  if (g_state == nullptr || !g_state->active) return;
  State& st = *g_state;
  st.snapshot = &snapshot;
  if (st.page == Page::spectrum) {   // the spectrum frames repaint the plot; only the frequency read-out is redrawn here
    if (snapshot.frequency_hz != st.drawn_freq) {
      st.drawn_freq = snapshot.frequency_hz;
      char line[48];
      std::snprintf(line, sizeof(line), "%.3f MHz", snapshot.frequency_hz / 1e6);
      button(st.layout.frequency, line, false, false);
    }
    return;
  }
  const uint32_t events = st.events ? st.events->total() : 0;
  if (snapshot.revision == st.drawn_revision && snapshot.rf_history_seq == st.drawn_hist_seq &&
      snapshot.session_elapsed_s == st.drawn_elapsed && events == st.drawn_events) return;
  draw_body(st);
  remember(st);
}

void leave() {
  if (g_state == nullptr) return;
  g_state->active = false;
  g_state->snapshot = nullptr;
  if (g_state->waterfall) { heap_caps_free(g_state->waterfall); g_state->waterfall = nullptr; }
}

bool active() { return g_state != nullptr && g_state->active; }
PaintStats paint_stats() { return PaintStats{}; }
Page page() { return g_state ? g_state->page : Page::overview; }
Overlay overlay() { return Overlay::none; }
bool self_check() { return true; }

bool draw_spectrum_frame(const float* dbfs, size_t count, uint32_t epoch, uint32_t) {
  if (g_state == nullptr || !g_state->active || g_state->page != Page::spectrum || !g_state->waterfall || dbfs == nullptr || count < 8) return false;
  State& st = *g_state;
  const auto& l = st.layout;
  float lo = dbfs[0], hi = dbfs[0];
  for (size_t i = 1; i < count; ++i) { lo = std::min(lo, dbfs[i]); hi = std::max(hi, dbfs[i]); }
  hi = std::max(hi, lo + 25.0f);
  st.lo += (lo - st.lo) * 0.2f;
  st.hi += (hi - st.hi) * 0.2f;
  const float span = std::max(10.0f, st.hi - st.lo);

  // Trace.
  const cmp::Rect p = l.plot;
  M5.Display.fillRect(p.x + 2, p.y + 2, p.w - 4, p.h - 4, kPanel);
  for (int g = 1; g < 4; ++g) M5.Display.drawFastHLine(p.x + 2, p.y + g * p.h / 4, p.w - 4, kGrid);
  int px = -1, py = 0;
  for (size_t i = 0; i < count; ++i) {
    const int x = p.x + 3 + int(i * (p.w - 7) / (count - 1));
    const float t = std::clamp((dbfs[i] - st.lo) / span, 0.0f, 1.0f);
    const int y = p.y + p.h - 4 - int(t * (p.h - 8));
    if (px >= 0) M5.Display.drawLine(px, py, x, y, kGreen);
    px = x; py = y;
  }

  // Waterfall: scroll the canvas down one row, paint the newest row on top, push the canvas.
  const int w = l.waterfall.w, h = l.waterfall.h;
  std::memmove(st.waterfall + w, st.waterfall, size_t(w) * (h - 1) * 2);
  const bool marker = epoch != st.epoch;
  st.epoch = epoch;
  for (int x = 0; x < w; ++x) {
    if (marker) { st.waterfall[x] = 0xffff; continue; }
    const size_t i = size_t(x) * count / w;
    st.waterfall[x] = heat((dbfs[i] - st.lo) / span);
  }
  M5.Display.pushImage(l.waterfall.x, l.waterfall.y, w, h, reinterpret_cast<const lgfx::rgb565_t*>(st.waterfall));
  return true;
}

bool draw_audio_frame(const AudioView&, uint32_t) { return false; }
void fx_event(const Event&, uint32_t) {}
void fx_idle(bool, float) {}
bool draw_fx(uint32_t) { return false; }

Action handle_touch(int32_t x, int32_t y, uint32_t) {
  if (g_state == nullptr || !g_state->active || g_state->snapshot == nullptr) return {};
  State& st = *g_state;
  const auto& l = st.layout;
  const Snapshot& s = *st.snapshot;
  for (int i = 0; i < cmp::kTabCount; ++i)
    if (hit(x, y, l.tab[i])) { set_page(st, tab_page(i)); return {}; }
  switch (st.page) {
    case Page::overview:
      if (hit(x, y, l.scan_button) && s.receiver_ready) return {s.sweeping ? ActionKind::stop_scan : ActionKind::start_scan, 0};
      if (hit(x, y, l.record_button) && st.events) {
        st.events->mark_manual(now_ms(), s.unix_time, s.frequency_hz);
        draw_body(st);
        remember(st);
      }
      break;
    case Page::spectrum:
      if (s.receiver_ready && !s.sweeping) {
        if (hit(x, y, l.tune_down)) return {ActionKind::tune_down, -1};
        if (hit(x, y, l.tune_up)) return {ActionKind::tune_up, 1};
      }
      break;
    case Page::events: {
      const size_t per = l.portrait ? cmp::kEventRows : 3;
      const size_t total = st.events ? st.events->count() : 0;
      const size_t pages = std::max<size_t>(1, (total + per - 1) / per);
      if (hit(x, y, l.prev) && st.event_page > 0) { --st.event_page; draw_body(st); }
      else if (hit(x, y, l.next) && st.event_page + 1 < pages) { ++st.event_page; draw_body(st); }
      break;
    }
    default:
      if (hit(x, y, l.setup_button[0]) && !s.sweeping && s.receiver_ready) return {ActionKind::set_demod, (demod_index(s) + 1) % kModeCount};
      if (hit(x, y, l.setup_button[1]) && !s.sweeping) return {ActionKind::sweep_preset, 0};
      if (hit(x, y, l.setup_button[2]) && !s.sweeping) return {ActionKind::sweep_dwell, 0};
      if (hit(x, y, l.setup_button[3])) return {ActionKind::toggle_clips, 0};
      if (hit(x, y, l.setup_button[4])) return {ActionKind::toggle_chime, 0};
      break;
  }
  return {};
}
}  // namespace orcsdr::dead_air
