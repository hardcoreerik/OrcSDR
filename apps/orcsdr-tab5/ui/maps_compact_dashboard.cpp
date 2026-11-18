// Compact MAPS screen: the packs the shared map service found (maps::snapshot), five facts per row, paged. Everything shown comes from
// the service's snapshot; nothing is synthetic.
#include "maps_compact_dashboard.hpp"
#include "dashboard_audio_control.hpp"
#include "map_pipeline.hpp"
#include "map_service.hpp"
#include "orcmap/location_picker.hpp"
#include "setup_flow.hpp"

#include <M5Unified.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cmath>
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
  bool picker_mode = false, picker_ready = false, wizard_mode = false, picker_from_wizard = false;
  setup::Flow flow;
  orcmap::LocationPicker picker;
  int32_t start_lat = 0, start_lon = 0;
  uint32_t map_revision = 0xffffffffu;
  maps::FrameInfo info;
  uint32_t shown_revision = 0xffffffffu;
  bool active = false;
};
State* g_state = nullptr;   // PSRAM
void (*g_header_controls)() = nullptr;
WizardHooks g_wizard;

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

void draw_picker(State& st);
void draw_wizard(State& st);

void draw_body(State& st) {
  if (st.picker_mode) { draw_picker(st); return; }
  if (st.wizard_mode) { draw_wizard(st); return; }
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
  std::snprintf(line, sizeof(line), "PAGE %u/%u", unsigned(st.page + 1), unsigned(pages));
  text(line, l.summary.x + l.summary.w - 6, l.summary.y + l.summary.h / 2, kMuted, 0, middle_right);
  button(l.prev, "PREV", st.page > 0);
  button(l.label, "LOCATION", true);
  button(l.next, "NEXT", st.page + 1 < pages);
  button(l.rescan, "RESCAN", true);
}

void draw_wizard(State& st) {
  const auto& l = st.layout;
  M5.Display.fillRect(l.content.x, l.content.y, l.content.w, l.content.h, TFT_BLACK);
  static const char* const kTitles[] = {"1 / 5  WELCOME", "2 / 5  WI-FI", "3 / 5  LOCATION", "4 / 5  MAPS", "5 / 5  READY"};
  const int step = static_cast<int>(st.flow.step);
  text(kTitles[step], l.summary.x + 6, l.summary.y + l.summary.h / 2, kGreen, 1, middle_left);
  cmp::Rect body = l.pick_map;
  body.h = std::min(body.h, 300);
  frame(body);
  char a[128] = "", b[128] = "";
  int y = body.y + 22;
  const auto line = [&](const char* s, uint16_t color, int size = 0) {
    char tmp[160];
    std::snprintf(tmp, sizeof(tmp), "%s", s);
    clip(tmp, body.w - 24);
    text(tmp, body.x + 12, y, color, size, middle_left);
    y += size == 1 ? 34 : 28;
  };
  const char* action = nullptr;
  bool action_enabled = true;
  switch (st.flow.step) {
    case setup::Step::welcome:
      line("Welcome to OrcSDR.", TFT_WHITE, 1);
      line("This setup helps you:", kMuted);
      line("- connect Wi-Fi", kMuted);
      line("- set the receiver location", kMuted);
      line("- check the map packs", kMuted);
      line("SKIP ALL jumps to the end.", kMuted);
      action = "SKIP ALL";
      break;
    case setup::Step::network: {
      const bool ok = g_wizard.wifi_line && g_wizard.wifi_line(a, sizeof(a));
      line(ok ? "Wi-Fi is connected." : "Wi-Fi is not connected.", ok ? kGreen : kAmber, 1);
      if (a[0]) line(a, kMuted);
      line("Wi-Fi sets the clock, downloads", kMuted);
      line("maps and looks up your location.", kMuted);
      line("It is optional.", kMuted);
      action = "WI-FI";
      action_enabled = g_wizard.open_wifi != nullptr;
      break;
    }
    case setup::Step::location: {
      const bool ok = g_wizard.location_line && g_wizard.location_line(a, sizeof(a));
      line(ok ? "Receiver location is set." : "Receiver location is not set.", ok ? kGreen : kAmber, 1);
      if (a[0]) line(a, kMuted);
      line("Used for ADS-B range, the map", kMuted);
      line("and the FT8 grid. PICK MAP lets", kMuted);
      line("you choose it on a map.", kMuted);
      action = "PICK MAP";
      break;
    }
    case setup::Step::maps: {
      unsigned usable = 0;
      for (unsigned i = 0; i < st.snap.count; ++i) usable += st.snap.packs[i].usable ? 1u : 0u;
      std::snprintf(b, sizeof(b), "%u map pack%s, %u usable", unsigned(st.snap.count), st.snap.count == 1 ? "" : "s", usable);
      line(b, usable ? kGreen : kAmber, 1);
      line(st.snap.sd ? "Found on the SD card." : "No SD card: maps need one.", kMuted);
      line("Packs are added in Settings >", kMuted);
      line("Data & Maps.", kMuted);
      action = nullptr;
      break;
    }
    default:
      line("Setup is complete.", kGreen, 1);
      line("Everything can be changed later", kMuted);
      line("in Settings. FINISH saves it.", kMuted);
      break;
  }
  button(l.prev, "BACK", st.flow.step != setup::Step::welcome);
  button(l.label, action ? action : "", action != nullptr && action_enabled);
  button(l.next, st.flow.step == setup::Step::complete ? "FINISH" : "NEXT", true);
  button(l.rescan, "CANCEL", true);
}

void draw_picker_map(State& st) {
  const auto& l = st.layout;
  st.map_revision = maps::revision();
  if (!st.picker_ready) {
    M5.Display.fillRect(l.pick_map.x, l.pick_map.y, l.pick_map.w, l.pick_map.h, kPanel);
    text("Map camera unavailable", l.pick_map.x + 10, l.pick_map.y + 20, kAmber, 0, middle_left);
    return;
  }
  const maps::Frame state = maps::draw(st.picker.View(), M5.Display, l.pick_map.x, l.pick_map.y, &st.info, nullptr, true);
  if (state != maps::Frame::ready) {
    M5.Display.fillRect(l.pick_map.x, l.pick_map.y, l.pick_map.w, l.pick_map.h, kPanel);
    text(state == maps::Frame::pending ? "Loading offline map..." : "No basemap installed. Install a map pack first.",
         l.pick_map.x + 10, l.pick_map.y + 20, kAmber, 0, middle_left);
  }
  const int cx = l.pick_map.x + l.pick_map.w / 2, cy = l.pick_map.y + l.pick_map.h / 2;
  M5.Display.drawLine(cx - 14, cy, cx + 14, cy, kCyan);
  M5.Display.drawLine(cx, cy - 14, cx, cy + 14, kCyan);
  M5.Display.drawRect(l.pick_map.x, l.pick_map.y, l.pick_map.w, l.pick_map.h, kGrid);
}

void draw_picker(State& st) {
  const auto& l = st.layout;
  M5.Display.fillRect(l.content.x, l.content.y, l.content.w, l.content.h, TFT_BLACK);
  char line[64] = "NO POINT";
  if (st.picker_ready) {
    const auto p = st.picker.Selection();
    std::snprintf(line, sizeof(line), "%.4f  %.4f   Z%u", p.lat_deg, p.lon_deg, unsigned(st.picker.Zoom()));
  }
  text(line, l.summary.x + 6, l.summary.y + l.summary.h / 2, kGreen, 1, middle_left);
  draw_picker_map(st);
  button(l.prev, "ZOOM -", st.picker_ready);
  button(l.label, "ZOOM +", st.picker_ready);
  button(l.next, "BACK", true);
  button(l.rescan, "USE", st.picker_ready);
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
  if (st.picker_mode) {   // the view changed size (rotation): rebuild the camera for the new map area at the chosen point
    int32_t lat = st.start_lat, lon = st.start_lon;
    if (st.picker_ready) {
      const auto p = st.picker.Selection();
      lat = static_cast<int32_t>(std::llround(p.lat_deg * 1e7));
      lon = static_cast<int32_t>(std::llround(p.lon_deg * 1e7));
    }
    st.picker_ready = maps::begin_location_picker(st.picker, st.layout.pick_map.w, st.layout.pick_map.h, lat, lon);
    st.map_revision = 0xffffffffu;
  }
  M5.Display.fillRect(st.layout.viewport.x, st.layout.viewport.y, st.layout.viewport.w, st.layout.viewport.h, TFT_BLACK);
  draw_header(st);
  draw_body(st);
}

bool enter() {
  if (g_state == nullptr || !g_state->layout.valid) return false;
  g_state->active = true;
  g_state->page = 0;
  g_state->picker_mode = false;
  g_state->wizard_mode = false;
  g_state->picker_from_wizard = false;
  draw();
  return true;
}

void update() {
  if (g_state == nullptr || !g_state->active) return;
  State& st = *g_state;
  maps::MapSnapshot& s = st.snap;
  const uint32_t rev = maps::revision();
  if (st.picker_mode) {
    if (rev != st.map_revision) draw_picker_map(st);
    return;
  }
  if (rev == st.shown_revision && s.loaded) return;
  refresh(st);
  draw_body(st);
}

void set_start_location(int32_t latitude_e7, int32_t longitude_e7) {
  if (g_state == nullptr) return;
  g_state->start_lat = latitude_e7;
  g_state->start_lon = longitude_e7;
}

bool open_picker() {
  if (g_state == nullptr || !g_state->active) return false;
  State& st = *g_state;
  st.picker_ready = maps::begin_location_picker(st.picker, st.layout.pick_map.w, st.layout.pick_map.h, st.start_lat, st.start_lon);
  st.picker_mode = true;
  st.map_revision = 0xffffffffu;
  draw_body(st);
  return true;
}

void set_wizard_hooks(const WizardHooks& hooks) { g_wizard = hooks; }

bool open_wizard() {
  if (g_state == nullptr || !g_state->active) return false;
  State& st = *g_state;
  st.flow = setup::Flow{};
  st.picker_mode = false;
  st.picker_from_wizard = false;
  st.wizard_mode = true;
  draw();
  return true;
}

bool finish_picker() {
  if (g_state == nullptr || !g_state->picker_from_wizard) return false;
  State& st = *g_state;
  st.picker_from_wizard = false;
  st.picker_mode = false;
  st.wizard_mode = true;
  draw_body(st);
  return true;
}

bool selection(int32_t* latitude_e7, int32_t* longitude_e7) {
  if (g_state == nullptr || !g_state->picker_ready) return false;
  const auto p = g_state->picker.Selection();
  *latitude_e7 = static_cast<int32_t>(std::llround(p.lat_deg * 1e7));
  *longitude_e7 = static_cast<int32_t>(std::llround(p.lon_deg * 1e7));
  return true;
}

void leave() {
  if (g_state) g_state->active = false;
}

Action touch(int x, int y) {
  if (g_state == nullptr || !g_state->active) return Action::none;
  State& st = *g_state;
  const auto& l = st.layout;
  if (st.wizard_mode && !st.picker_mode) {
    const bool cancel = hit(x, y, l.rescan);
    if (cancel) { st.wizard_mode = false; if (g_wizard.finished) g_wizard.finished(); return Action::none; }
    if (hit(x, y, l.next)) {
      if (st.flow.step == setup::Step::complete) {
        st.flow.done = true;
        if (g_wizard.save_record) (void)g_wizard.save_record(st.flow.record());
        st.wizard_mode = false;
        if (g_wizard.finished) g_wizard.finished();
        return Action::none;
      }
      st.flow.next();
      draw_body(st);
    } else if (hit(x, y, l.prev) && st.flow.step != setup::Step::welcome) {
      st.flow.back();
      draw_body(st);
    } else if (hit(x, y, l.label)) {
      if (st.flow.step == setup::Step::welcome) { st.flow.quick_start(); draw_body(st); }
      else if (st.flow.step == setup::Step::network && g_wizard.open_wifi) { st.wizard_mode = false; g_wizard.open_wifi(); }
      else if (st.flow.step == setup::Step::location) { st.picker_from_wizard = true; st.wizard_mode = false; open_picker(); }
    }
    return Action::none;
  }
  if (st.picker_mode) {
    if (hit(x, y, l.next)) { st.picker_mode = false; st.wizard_mode = st.picker_from_wizard; st.picker_from_wizard = false; draw_body(st); }
    else if (hit(x, y, l.rescan) && st.picker_ready) return Action::use_location;
    else if (hit(x, y, l.prev) && st.picker_ready) { if (st.picker.ZoomOut()) draw_picker(st); }
    else if (hit(x, y, l.label) && st.picker_ready) { if (st.picker.ZoomIn()) draw_picker(st); }
    else if (hit(x, y, l.pick_map) && st.picker_ready) {
      if (st.picker.RecenterAtScreen(x - l.pick_map.x, y - l.pick_map.y)) draw_picker(st);
    }
    return Action::none;
  }
  if (hit(x, y, l.label)) { open_picker(); return Action::none; }
  if (hit(x, y, l.rescan)) { maps::rescan(); st.shown_revision = 0xffffffffu; return Action::rescan; }
  const size_t per = size_t(l.rows);
  const size_t pages = std::max<size_t>(1, (st.snap.count + per - 1) / per);
  if (hit(x, y, l.prev) && st.page > 0) { --st.page; draw_body(st); }
  else if (hit(x, y, l.next) && st.page + 1 < pages) { ++st.page; draw_body(st); }
  return Action::none;
}
}  // namespace orcsdr::maps_ui
