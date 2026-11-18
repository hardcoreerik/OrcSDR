// Compact AIS dashboard: implements the orcsdr::ais_ui contract (ais_dashboard.hpp) for views smaller than 1280 x 720, in place of
// ais_dashboard.cpp (a build links one of the two). Pages: TARGETS, MESSAGES, SIGNAL, SETUP. Not on this view yet: the map page, the
// protocol and events pages. No synthetic data: everything shown comes from the Snapshot the application publishes.
#include "ais_compact_dashboard.hpp"
#include "ais_compact_layout.hpp"
#include "ais_dashboard.hpp"
#include "dashboard_audio_control.hpp"

#include <M5Unified.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>

namespace orcsdr::ais_ui {
namespace cmp = compact;
namespace {

constexpr uint16_t kPanel = 0x0841, kGrid = 0x2945, kCyan = 0x2e7f, kGreen = 0x6fe8, kYellow = 0xff24, kAmber = 0xfd20,
                   kMuted = 0x8c71, kSelected = 0x1264;

enum class Page : uint8_t { targets, messages, signal, setup };

struct State {
  cmp::Layout layout;
  const Snapshot* snapshot = nullptr;   // the application's snapshot; valid from enter() to leave()
  Page page = Page::targets;
  size_t target_page = 0, message_page = 0;
  uint32_t selected = 0;                // identity (MMSI or source id) of the selected target, 0 = none
  bool active = false;
};
State* g_state = nullptr;   // PSRAM, created by set_viewport(): this screen costs no internal RAM when it is not open

bool hit(int32_t x, int32_t y, cmp::Rect r) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

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

void fit_text(const char* value, cmp::Rect r, uint16_t color, textdatum_t datum = middle_center) {
  M5.Display.setFont(&fonts::DejaVu24);
  M5.Display.setTextSize(1);
  const bool fits = M5.Display.textWidth(value) <= r.w - 8;
  M5.Display.setFont(nullptr);
  const int x = datum == middle_left ? r.x + 6 : datum == middle_right ? r.x + r.w - 6 : r.x + r.w / 2;
  text(value, x, r.y + r.h / 2, color, fits ? 1 : 0, datum);
}

void frame(cmp::Rect r, uint16_t border = kGrid) {
  M5.Display.fillRoundRect(r.x, r.y, r.w, r.h, 8, kPanel);
  M5.Display.drawRoundRect(r.x, r.y, r.w, r.h, 8, border);
}

void button(cmp::Rect r, const char* label, bool selected = false, bool enabled = true) {
  const uint16_t border = enabled ? (selected ? kGreen : kCyan) : TFT_DARKGREY;
  M5.Display.fillRoundRect(r.x, r.y, r.w, r.h, 8, selected ? kSelected : kPanel);
  M5.Display.drawRoundRect(r.x, r.y, r.w, r.h, 8, border);
  fit_text(label, r, enabled ? (selected ? kGreen : TFT_WHITE) : kMuted);
}

uint32_t identity(const Target& t) { return t.identity_kind == IdentityKind::source_id28 ? t.source_id28 : t.mmsi; }

// The application marks the stream settled (frames are being decoded) without moving the state off `ready`, so settled means RECEIVING.
const char* receive_text(const Snapshot& snap, uint16_t* color) {
  *color = kAmber;
  switch (snap.receiver) {
    case ReceiveState::disconnected: return "NO RECEIVER";
    case ReceiveState::ready:
      if (snap.receiver_settled) { *color = kGreen; return "RECEIVING"; }
      *color = kCyan;
      return "STARTING";
    case ReceiveState::recording: *color = kGreen; return "RECEIVING";
    case ReceiveState::unavailable: *color = TFT_RED; return "UNAVAILABLE";
  }
  return "?";
}

const char* station_name(Station s) {
  switch (s) {
    case Station::vessel_a: return "VESSEL A";
    case Station::vessel_b: return "VESSEL B";
    case Station::base_station: return "BASE STATION";
    case Station::aid_physical: return "AID TO NAV";
    case Station::aid_virtual: return "VIRTUAL AID";
    case Station::sar_aircraft: return "SAR AIRCRAFT";
    case Station::aid_2026: return "AID (2026)";
    case Station::other: return "OTHER";
  }
  return "OTHER";
}

void age_text(char* out, size_t size, uint32_t seconds) {
  if (seconds < 90) std::snprintf(out, size, "%us", static_cast<unsigned>(seconds));
  else if (seconds < 5400) std::snprintf(out, size, "%um", static_cast<unsigned>(seconds / 60));
  else std::snprintf(out, size, "%uh", static_cast<unsigned>(seconds / 3600));
}

void latlon_text(char* out, size_t size, int32_t lat_e7, int32_t lon_e7) {
  std::snprintf(out, size, "%.4f%c %.4f%c", std::abs(lat_e7) / 1e7, lat_e7 < 0 ? 'S' : 'N', std::abs(lon_e7) / 1e7, lon_e7 < 0 ? 'W' : 'E');
}

const Target* find_selected(const Snapshot& s, State& st) {
  for (size_t i = 0; i < s.target_count && i < kTargets; ++i)
    if (identity(s.targets[i]) == st.selected && st.selected != 0) return &s.targets[i];
  if (s.target_count > 0) {   // nothing (or something that left the list) selected: show the first
    st.selected = identity(s.targets[0]);
    return &s.targets[0];
  }
  st.selected = 0;
  return nullptr;
}

// ---------------------------------------------------------------- TARGETS

void draw_targets(const Snapshot& s, State& st) {
  const cmp::Layout& l = st.layout;
  uint16_t color;
  const char* rx = receive_text(s, &color);
  M5.Display.fillRect(l.context.x, l.context.y, l.context.w, l.context.h, TFT_BLACK);
  char line[96];
  std::snprintf(line, sizeof(line), "%u TARGETS   162.000 MHz   %s%s", static_cast<unsigned>(s.target_count), rx, s.test_replay ? "   REPLAY" : "");
  text(line, l.context.x + 4, l.context.y + l.context.h / 2, s.test_replay ? kAmber : color, 0, middle_left);

  frame(l.target_list);
  const Target* selected = find_selected(s, st);
  const size_t rows = static_cast<size_t>(l.target_rows);
  const size_t pages = (s.target_count + rows - 1) / rows;
  if (st.target_page >= std::max<size_t>(pages, 1)) st.target_page = pages ? pages - 1 : 0;
  if (s.target_count == 0) {
    text(s.receiver == ReceiveState::recording || (s.receiver == ReceiveState::ready && s.receiver_settled) ? "no vessels heard yet" : "waiting for the receiver", l.target_list.x + l.target_list.w / 2,
         l.target_list.y + (l.target_list.h - 48) / 2, kMuted, 1, middle_center);
  }
  for (size_t r = 0; r < rows; ++r) {
    const size_t i = st.target_page * rows + r;
    if (i >= s.target_count || i >= kTargets) break;
    const Target& t = s.targets[i];
    const int y = l.target_list.y + 4 + static_cast<int>(r) * l.target_row_h;
    const bool sel = selected != nullptr && identity(t) == st.selected;
    M5.Display.fillRoundRect(l.target_list.x + 4, y, l.target_list.w - 8, l.target_row_h - 4, 6, sel ? kSelected : kPanel);
    char name[48], info[64], age[12];
    if (t.name[0]) std::snprintf(name, sizeof(name), "%s", t.name);
    else std::snprintf(name, sizeof(name), "%s %u", t.identity_kind == IdentityKind::source_id28 ? "SRC" : "MMSI", static_cast<unsigned>(identity(t)));
    age_text(age, sizeof(age), t.heard_age_s);
    if (t.has_speed) std::snprintf(info, sizeof(info), "%s  %.1f kn%s  %s", station_name(t.station), t.speed_tenths_kn / 10.0, t.has_course ? "" : "", age);
    else std::snprintf(info, sizeof(info), "%s  %s", station_name(t.station), age);
    text(name, l.target_list.x + 12, y + 14, sel ? kGreen : TFT_WHITE, 0, middle_left);
    text(info, l.target_list.x + 12, y + 34, kMuted, 0, middle_left);
  }
  char label[24];
  std::snprintf(label, sizeof(label), "%u/%u", static_cast<unsigned>(st.target_page + 1), static_cast<unsigned>(std::max<size_t>(pages, 1)));
  frame(l.page_label);
  fit_text(label, l.page_label, kMuted);
  button(l.page_prev, "PREV", false, st.target_page > 0);
  button(l.page_next, "NEXT", false, st.target_page + 1 < pages);

  // ---- detail
  frame(l.target_detail);
  if (selected == nullptr) {
    text("no target selected", l.target_detail.x + l.target_detail.w / 2, l.target_detail.y + l.target_detail.h / 2, kMuted, 0, middle_center);
    return;
  }
  const Target& t = *selected;
  const int x = l.target_detail.x + 10;
  int y = l.target_detail.y + 14;
  const int step = 24;
  char value[96], age[12];
  std::snprintf(value, sizeof(value), "%s  %s %u", t.name[0] ? t.name : "(no name)", t.identity_kind == IdentityKind::source_id28 ? "SRC" : "MMSI", static_cast<unsigned>(identity(t)));
  text(value, x, y, kGreen, 0, middle_left);
  y += step;
  std::snprintf(value, sizeof(value), "%s%s%s", station_name(t.station), t.description[0] ? "  " : "", t.description);
  text(value, x, y, TFT_WHITE, 0, middle_left);
  y += step;
  if (t.has_position) {
    latlon_text(value, sizeof(value), t.lat_e7, t.lon_e7);
    age_text(age, sizeof(age), t.position_age_s);
    char full[120];
    std::snprintf(full, sizeof(full), "%s  (%s%s)", value, age, t.position_stale ? ", stale" : "");
    text(full, x, y, t.position_stale ? kAmber : TFT_WHITE, 0, middle_left);
  } else {
    text("position not received", x, y, kMuted, 0, middle_left);
  }
  y += step;
  char sog[24], cog[24], hdg[24];
  if (t.has_speed) std::snprintf(sog, sizeof(sog), "%.1f kn", t.speed_tenths_kn / 10.0); else std::snprintf(sog, sizeof(sog), "--");
  if (t.has_course) std::snprintf(cog, sizeof(cog), "%.1f", t.course_tenths_deg / 10.0); else std::snprintf(cog, sizeof(cog), "--");
  if (t.has_heading) std::snprintf(hdg, sizeof(hdg), "%u", static_cast<unsigned>(t.heading_deg)); else std::snprintf(hdg, sizeof(hdg), "--");
  std::snprintf(value, sizeof(value), "SOG %s   COG %s   HDG %s", sog, cog, hdg);
  text(value, x, y, TFT_WHITE, 0, middle_left);
  y += step;
  std::snprintf(value, sizeof(value), "STATUS %s", t.nav_status[0] ? t.nav_status : "--");
  text(value, x, y, TFT_WHITE, 0, middle_left);
  y += step;
  if (t.destination[0] || t.callsign[0]) {
    std::snprintf(value, sizeof(value), "DEST %s  CALL %s", t.destination[0] ? t.destination : "--", t.callsign[0] ? t.callsign : "--");
    text(value, x, y, TFT_WHITE, 0, middle_left);
    y += step;
  }
  age_text(age, sizeof(age), t.heard_age_s);
  std::snprintf(value, sizeof(value), "last heard %s ago, last type %u, %u fixes", age, static_cast<unsigned>(t.last_type), static_cast<unsigned>(t.history_count));
  if (y + 10 < l.target_detail.y + l.target_detail.h) text(value, x, y, kMuted, 0, middle_left);
}

// ---------------------------------------------------------------- MESSAGES

void draw_messages(const Snapshot& s, State& st) {
  const cmp::Layout& l = st.layout;
  frame(l.message_list);
  char head[64];
  std::snprintf(head, sizeof(head), "RECENT MESSAGES  %u", static_cast<unsigned>(s.message_count));
  text(head, l.message_list.x + 10, l.message_list.y + 15, kCyan, 0, middle_left);
  const size_t rows = static_cast<size_t>(l.message_rows);
  const size_t pages = (s.message_count + rows - 1) / rows;
  if (st.message_page >= std::max<size_t>(pages, 1)) st.message_page = pages ? pages - 1 : 0;
  if (s.message_count == 0) text("no messages decoded yet", l.message_list.x + l.message_list.w / 2, l.message_list.y + l.message_list.h / 2 - 20, kMuted, 1, middle_center);
  for (size_t r = 0; r < rows; ++r) {
    const size_t i = st.message_page * rows + r;
    if (i >= s.message_count || i >= kMessages) break;
    const Message& m = s.messages[i];
    const int y = l.message_list.y + 30 + static_cast<int>(r) * l.message_row_h;
    if (r & 1u) M5.Display.fillRect(l.message_list.x + 4, y, l.message_list.w - 8, l.message_row_h, 0x0821);
    char first[80], age[12];
    age_text(age, sizeof(age), m.age_ms / 1000u);
    std::snprintf(first, sizeof(first), "T%u  %s%u  %s %u  %s%s", static_cast<unsigned>(m.type), m.channel ? "CH" : "", m.channel ? static_cast<unsigned>(m.channel) : 0u,
                  m.identity_kind == IdentityKind::source_id28 ? "SRC" : "MMSI", static_cast<unsigned>(m.identity_kind == IdentityKind::source_id28 ? m.source_id28 : m.mmsi), age,
                  m.crc_ok ? "" : "  CRC?");
    if (l.portrait) {
      text(first, l.message_list.x + 10, y + l.message_row_h / 4 + 2, TFT_WHITE, 0, middle_left);
      text(m.summary, l.message_list.x + 10, y + 3 * l.message_row_h / 4, kMuted, 0, middle_left);
    } else {
      char one[160];
      std::snprintf(one, sizeof(one), "%s   %s", first, m.summary);
      text(one, l.message_list.x + 10, y + l.message_row_h / 2, TFT_WHITE, 0, middle_left);
    }
  }
  char label[24];
  std::snprintf(label, sizeof(label), "%u/%u", static_cast<unsigned>(st.message_page + 1), static_cast<unsigned>(std::max<size_t>(pages, 1)));
  frame(l.message_label);
  fit_text(label, l.message_label, kMuted);
  button(l.message_prev, "PREV", false, st.message_page > 0);
  button(l.message_next, "NEXT", false, st.message_page + 1 < pages);
}

// ---------------------------------------------------------------- SIGNAL

void draw_signal_panel(const Snapshot& s, int ch, cmp::Rect r) {
  frame(r);
  char title[40];
  std::snprintf(title, sizeof(title), "AIS %d   %s MHz", ch + 1, ch == 0 ? "161.975" : "162.025");
  const bool enabled = ch == 0 ? s.ais1_enabled : s.ais2_enabled;
  text(title, r.x + 10, r.y + 15, enabled ? kCyan : kMuted, 0, middle_left);
  char value[64];
  std::snprintf(value, sizeof(value), "%u frames   %u CRC ok   %u bad", static_cast<unsigned>(s.frames[ch]), static_cast<unsigned>(s.crc_ok[ch]), static_cast<unsigned>(s.crc_failed[ch]));
  text(value, r.x + 10, r.y + 40, TFT_WHITE, 0, middle_left);
  const cmp::Rect plot{r.x + 10, r.y + 58, r.w - 20, r.h - 68};
  if (plot.h < 24 || plot.w < 40) return;
  M5.Display.fillRect(plot.x, plot.y, plot.w, plot.h, 0x0021);
  if (!s.spectrum_valid[ch]) {
    text("no spectrum yet", plot.x + plot.w / 2, plot.y + plot.h / 2, kMuted, 0, middle_center);
    return;
  }
  // Bars from the calibrated dBFS tenths: -120 dBFS at the bottom, -20 dBFS at the top.
  const int n = static_cast<int>(kSpectrumBins);
  for (int b = 0; b < n; ++b) {
    const float db = s.spectrum_dbfs[ch][b] / 10.0f;
    const float t = std::clamp((db + 120.0f) / 100.0f, 0.0f, 1.0f);
    const int h = static_cast<int>(t * static_cast<float>(plot.h));
    const int x0 = plot.x + b * plot.w / n, x1 = plot.x + (b + 1) * plot.w / n;
    if (h > 0) M5.Display.fillRect(x0, plot.y + plot.h - h, std::max(1, x1 - x0 - 1), h, kGreen);
  }
}

void draw_signal(const Snapshot& s, State& st) {
  const cmp::Layout& l = st.layout;
  draw_signal_panel(s, 0, l.signal_panel[0]);
  draw_signal_panel(s, 1, l.signal_panel[1]);
  frame(l.signal_status);
  uint16_t color;
  const char* rx = receive_text(s, &color);
  char value[96];
  std::snprintf(value, sizeof(value), "%s   %u sps   IQ drops %u", rx, static_cast<unsigned>(s.sample_rate_sps), static_cast<unsigned>(s.iq_drops));
  text(value, l.signal_status.x + 10, l.signal_status.y + 18, color, 0, middle_left);
  const uint32_t e = s.elapsed_s;
  std::snprintf(value, sizeof(value), "running %02u:%02u:%02u%s", static_cast<unsigned>(e / 3600), static_cast<unsigned>((e / 60) % 60), static_cast<unsigned>(e % 60), s.test_replay ? "   REPLAYED DATA" : "");
  text(value, l.signal_status.x + 10, l.signal_status.y + 42, s.test_replay ? kAmber : kMuted, 0, middle_left);
}

// ---------------------------------------------------------------- SETUP

void setup_row(State& st, int i, const char* label, const char* value, uint16_t color) {
  const cmp::Rect r = st.layout.setup_row[i];
  frame(r);
  text(label, r.x + 10, r.y + r.h / 2, kCyan, 0, middle_left);
  fit_text(value, {r.x + r.w / 2 - 20, r.y, r.w / 2 + 20, r.h}, color, middle_right);
}

void draw_setup(const Snapshot& s, State& st) {
  uint16_t color;
  const char* rx = receive_text(s, &color);
  char value[80];
  setup_row(st, 0, "RECEIVER", rx, color);
  std::snprintf(value, sizeof(value), "%.3f MHz  %u sps", s.center_hz / 1e6, static_cast<unsigned>(s.sample_rate_sps));
  setup_row(st, 1, "STREAM", value, TFT_WHITE);
  setup_row(st, 2, "SOURCE", s.rtl_tcp ? "RTL TCP (remote)" : "USB receiver", TFT_WHITE);
  if (s.coordinates_valid) latlon_text(value, sizeof(value), s.receiver_lat_e7, s.receiver_lon_e7);
  else std::snprintf(value, sizeof(value), "NOT SET");
  setup_row(st, 3, "LOCATION", value, s.coordinates_valid ? kGreen : kAmber);
  setup_row(st, 4, "SD CARD", s.sd_available ? "MOUNTED" : "NOT MOUNTED", s.sd_available ? kGreen : kMuted);
  std::snprintf(value, sizeof(value), "AIS 1 %s   AIS 2 %s", s.ais1_enabled ? "ON" : "OFF", s.ais2_enabled ? "ON" : "OFF");
  setup_row(st, 5, "CHANNELS", value, TFT_WHITE);
  setup_row(st, 6, "EXTRAS", s.long_range_available || s.nmea_available || s.journal_available ? "see Tab5 build" : "long range, NMEA, log: n/a", kMuted);
  setup_row(st, 7, "DATA", s.test_replay ? "REPLAYED, NOT LIVE RF" : "LIVE RF", s.test_replay ? kAmber : kGreen);
}

// ---------------------------------------------------------------- frame

void draw_tabs(State& st) {
  static constexpr const char* labels[cmp::kTabCount] = {"TARGETS", "MESSAGES", "SIGNAL", "SETUP"};
  for (int i = 0; i < cmp::kTabCount; ++i) button(st.layout.tab[i], labels[i], static_cast<int>(st.page) == i);
}

void draw_body(State& st) {
  if (st.snapshot == nullptr) return;
  switch (st.page) {
    case Page::targets: draw_targets(*st.snapshot, st); break;
    case Page::messages: draw_messages(*st.snapshot, st); break;
    case Page::signal: draw_signal(*st.snapshot, st); break;
    case Page::setup: draw_setup(*st.snapshot, st); break;
  }
}

void draw_header(State& st) {
  const auto& header = audio_header::layout();
  M5.Display.fillRect(st.layout.viewport.x, st.layout.viewport.y, st.layout.viewport.w, header.content_top - st.layout.viewport.y, TFT_BLACK);
  audio_header::draw_brand("AIS");
  if (header.status.w >= 100) text("AIS RECEIVE", header.status.x + 8, header.status.y + header.status.h / 2, TFT_WHITE, header.status.w >= 200 ? 1 : 0, middle_left);
  M5.Display.drawFastHLine(st.layout.viewport.x + 8, header.content_top - 4, st.layout.viewport.w - 16, kGreen);
}

}  // namespace

// Validates the view and installs it (and moves the shared header). Allocates the state on first use.
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

bool enter(const Snapshot& snapshot) {
  if (g_state == nullptr || !g_state->layout.valid) return false;
  State& st = *g_state;
  st.snapshot = &snapshot;
  st.active = true;
  st.page = Page::targets;
  st.target_page = st.message_page = 0;
  M5.Display.fillRect(st.layout.viewport.x, st.layout.viewport.y, st.layout.viewport.w, st.layout.viewport.h, TFT_BLACK);
  draw_header(st);
  draw_body(st);
  draw_tabs(st);
  return true;
}

void update(const Snapshot& snapshot) {
  if (g_state == nullptr || !g_state->active) return;
  g_state->snapshot = &snapshot;
  draw_body(*g_state);
}

void leave() {
  if (g_state == nullptr) return;
  g_state->active = false;
  g_state->snapshot = nullptr;
  g_state->~State();
  heap_caps_free(g_state);
  g_state = nullptr;
}

bool active() { return g_state != nullptr && g_state->active; }
bool map_settled() { return true; }   // there is no map page on this view
View view() {
  if (g_state == nullptr) return View::targets;
  switch (g_state->page) {
    case Page::targets: return View::targets;
    case Page::messages: return View::messages;
    case Page::signal: return View::signal;
    case Page::setup: return View::setup;
  }
  return View::targets;
}
uint8_t protocol_type() { return 1; }
void show_preview_view(View, const Snapshot&) {}   // documentation captures are a Tab5 bench feature

Action handle_touch(int32_t x, int32_t y) {
  if (g_state == nullptr || !g_state->active || g_state->snapshot == nullptr) return {};
  State& st = *g_state;
  const Snapshot& s = *st.snapshot;
  const cmp::Layout& l = st.layout;
  for (int i = 0; i < cmp::kTabCount; ++i)
    if (hit(x, y, l.tab[i])) {
      st.page = static_cast<Page>(i);
      M5.Display.fillRect(l.content.x, l.content.y, l.content.w, l.content.h, TFT_BLACK);
      draw_tabs(st);
      draw_body(st);
      return {};
    }
  switch (st.page) {
    case Page::targets: {
      const size_t rows = static_cast<size_t>(l.target_rows);
      const size_t pages = (s.target_count + rows - 1) / rows;
      if (hit(x, y, l.page_prev) && st.target_page > 0) {
        --st.target_page;
        draw_body(st);
      } else if (hit(x, y, l.page_next) && st.target_page + 1 < pages) {
        ++st.target_page;
        draw_body(st);
      } else if (hit(x, y, l.target_list)) {
        const int row = (y - (l.target_list.y + 4)) / l.target_row_h;
        const size_t i = st.target_page * rows + static_cast<size_t>(row);
        if (row >= 0 && row < l.target_rows && i < s.target_count && i < kTargets) {
          st.selected = identity(s.targets[i]);
          draw_body(st);
          Action a;
          a.kind = ActionKind::target_selected;
          a.value = st.selected;
          a.identity_kind = s.targets[i].identity_kind;
          return a;
        }
      }
      break;
    }
    case Page::messages: {
      const size_t rows = static_cast<size_t>(l.message_rows);
      const size_t pages = (s.message_count + rows - 1) / rows;
      if (hit(x, y, l.message_prev) && st.message_page > 0) {
        --st.message_page;
        draw_body(st);
      } else if (hit(x, y, l.message_next) && st.message_page + 1 < pages) {
        ++st.message_page;
        draw_body(st);
      }
      break;
    }
    case Page::signal:
    case Page::setup: break;
  }
  return {};
}

bool self_check() {
  const cmp::Layout l = cmp::make_layout({72, 72, 424, 1088});
  return l.valid;
}

}  // namespace orcsdr::ais_ui
