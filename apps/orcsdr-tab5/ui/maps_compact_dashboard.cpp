// Compact MAPS screen: the packs the shared map service found (maps::snapshot), five facts per row, paged. Everything shown comes from
// the service's snapshot; nothing is synthetic.
#include "maps_compact_dashboard.hpp"
#include "dashboard_audio_control.hpp"
#include "map_service.hpp"

#include <M5Unified.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>

namespace orcsdr::maps_ui {
namespace cmp = compact;
namespace {
constexpr uint16_t kPanel = 0x0841, kGrid = 0x2945, kCyan = 0x2e7f, kGreen = 0x6fe8, kAmber = 0xfd20, kMuted = 0x8c71;

struct State {
  cmp::Layout layout;
  maps::MapSnapshot snap;
  size_t page = 0;
  uint32_t shown_revision = 0xffffffffu;
  bool active = false;
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

// Trims `value` (in the small font) until it fits `width` pixels, ending in "..".
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

void button(cmp::Rect r, const char* label, bool enabled) {
  M5.Display.fillRoundRect(r.x, r.y, r.w, r.h, 8, kPanel);
  M5.Display.drawRoundRect(r.x, r.y, r.w, r.h, 8, enabled ? kCyan : TFT_DARKGREY);
  M5.Display.setFont(&fonts::DejaVu24);
  const bool fits = M5.Display.textWidth(label) <= r.w - 10;
  M5.Display.setFont(nullptr);
  text(label, r.x + r.w / 2, r.y + r.h / 2, enabled ? TFT_WHITE : kMuted, fits ? 1 : 0, middle_center);
}

void format_size(uint64_t bytes, char* out, size_t cap) {
  if (bytes >= 1024ull * 1024 * 1024) std::snprintf(out, cap, "%.1f GB", double(bytes) / (1024.0 * 1024 * 1024));
  else if (bytes >= 1024ull * 1024) std::snprintf(out, cap, "%.0f MB", double(bytes) / (1024.0 * 1024));
  else if (bytes) std::snprintf(out, cap, "%.0f KB", double(bytes) / 1024.0);
  else std::snprintf(out, cap, "-");
}

const char* kind_name(maps::Kind k) {
  switch (k) {
    case maps::Kind::firmware: return "built in";
    case maps::Kind::pmtiles: return "PMTiles";
    case maps::Kind::legacy: return "legacy";
    default: return "rejected";
  }
}

void draw_header(State& st) {
  const auto& header = audio_header::layout();
  M5.Display.fillRect(st.layout.viewport.x, st.layout.viewport.y, st.layout.viewport.w, header.content_top - st.layout.viewport.y, TFT_BLACK);
  audio_header::draw_brand("MAPS");
  if (header.status.w >= 100) text("LOCAL MAPS", header.status.x + 8, header.status.y + header.status.h / 2, TFT_WHITE, header.status.w >= 200 ? 1 : 0, middle_left);
  M5.Display.drawFastHLine(st.layout.viewport.x + 8, header.content_top - 4, st.layout.viewport.w - 16, kGreen);
  if (g_header_controls) g_header_controls();
}

void draw_body(State& st) {
  const auto& l = st.layout;
  const auto& s = st.snap;
  M5.Display.fillRect(l.content.x, l.content.y, l.content.w, l.content.h, TFT_BLACK);
  char line[96];
  const char* where = !s.sd ? "NO SD CARD" : !s.loaded ? "SCANNING" : "";
  if (*where) std::snprintf(line, sizeof(line), "%s", where);
  else std::snprintf(line, sizeof(line), "%u PACK%s ON THE CARD%s", unsigned(s.count), s.count == 1 ? "" : "S", s.overflow ? " (LIST FULL)" : "");
  text(line, l.summary.x + 6, l.summary.y + l.summary.h / 2, s.sd && s.loaded ? kGreen : kAmber, 1, middle_left);
  const size_t per = size_t(l.rows);
  const size_t pages = std::max<size_t>(1, (s.count + per - 1) / per);
  if (st.page >= pages) st.page = pages - 1;
  for (size_t i = 0; i < per; ++i) {
    const size_t idx = st.page * per + i;
    const cmp::Rect r = l.row[i];
    frame(r);
    if (idx >= s.count) continue;
    const maps::PackInfo& p = s.packs[idx];
    char size[24];
    format_size(p.bytes, size, sizeof(size));
    char title[80];
    std::snprintf(title, sizeof(title), "%s", p.title[0] ? p.title : p.identity);
    clip(title, r.w - 20);
    text(title, r.x + 10, r.y + r.h / 3, p.usable ? TFT_WHITE : kMuted, 0, middle_left);
    text(size, r.x + r.w - 10, r.y + r.h * 3 / 4, kCyan, 0, middle_right);
    char info[120];
    std::snprintf(info, sizeof(info), "%s  z%u-%u  %s", kind_name(p.kind), unsigned(p.min_zoom), unsigned(p.max_zoom), p.usable ? "usable" : p.status);
    clip(info, r.w - 20 - 90);
    text(info, r.x + 10, r.y + r.h * 3 / 4, p.usable ? kGreen : kAmber, 0, middle_left);
  }
  std::snprintf(line, sizeof(line), "%u/%u", unsigned(st.page + 1), unsigned(pages));
  button(l.prev, "PREV", st.page > 0);
  button(l.label, line, false);
  button(l.next, "NEXT", st.page + 1 < pages);
  button(l.rescan, "RESCAN", true);
}

void refresh(State& st) {
  maps::snapshot(&st.snap);
  st.shown_revision = st.snap.revision;
}
}  // namespace

void set_header_controls(void (*draw)()) { g_header_controls = draw; }

bool set_viewport(cmp::Rect viewport) {
  const cmp::Layout next = cmp::make_layout(viewport);
  if (!next.valid || !audio_header::set_viewport(viewport)) return false;
  if (g_state == nullptr) {
    void* memory = heap_caps_calloc(1, sizeof(State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (memory == nullptr) return false;
    g_state = new (memory) State();
  }
  g_state->layout = next;
  return true;
}

void draw() {
  if (g_state == nullptr || !g_state->layout.valid) return;
  State& st = *g_state;
  refresh(st);
  M5.Display.fillRect(st.layout.viewport.x, st.layout.viewport.y, st.layout.viewport.w, st.layout.viewport.h, TFT_BLACK);
  draw_header(st);
  draw_body(st);
}

bool enter() {
  if (g_state == nullptr || !g_state->layout.valid) return false;
  g_state->active = true;
  g_state->page = 0;
  draw();
  return true;
}

void update() {
  if (g_state == nullptr || !g_state->active) return;
  State& st = *g_state;
  maps::MapSnapshot& s = st.snap;
  const uint32_t rev = maps::revision();
  if (rev == st.shown_revision && s.loaded) return;
  refresh(st);
  draw_body(st);
}

void leave() {
  if (g_state) g_state->active = false;
}

Action touch(int x, int y) {
  if (g_state == nullptr || !g_state->active) return Action::none;
  State& st = *g_state;
  const auto& l = st.layout;
  if (hit(x, y, l.rescan)) { maps::rescan(); st.shown_revision = 0xffffffffu; return Action::rescan; }
  const size_t per = size_t(l.rows);
  const size_t pages = std::max<size_t>(1, (st.snap.count + per - 1) / per);
  if (hit(x, y, l.prev) && st.page > 0) { --st.page; draw_body(st); }
  else if (hit(x, y, l.next) && st.page + 1 < pages) { ++st.page; draw_body(st); }
  return Action::none;
}
}  // namespace orcsdr::maps_ui
