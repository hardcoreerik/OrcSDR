#include "ft8_dashboard.hpp"

#include "dashboard_audio_control.hpp"
#include "focus_nav.hpp"

#include <M5Unified.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace orcsdr::ft8 {
namespace {

constexpr uint16_t kPanel = 0x0841;
constexpr uint16_t kGrid = 0x2945;
constexpr uint16_t kCyan = 0x2e7f;
constexpr uint16_t kGreen = 0x6fe8;
constexpr uint16_t kYellow = 0xff24;
constexpr uint16_t kAmber = 0xfd20;
constexpr uint16_t kMuted = 0x8c71;
constexpr uint16_t kSelected = 0x1264;

struct Rect { int x; int y; int w; int h; };

constexpr int kTabsY = 630;
constexpr int kTabCount = 6;
constexpr int kTabW = 1280 / kTabCount;
constexpr Rect kBody{24, 104, 1232, 514};
constexpr Rect kHome{1178, 14, 74, 60};
constexpr Rect kClear{1088, 556, 144, 46};

Snapshot g_snapshot{};
bool g_active = false;
Tab g_tab = Tab::live;
size_t g_decode_page = 0;

bool hit(int32_t x, int32_t y, const Rect& r) {
  return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}
int cx(const Rect& r) { return r.x + r.w / 2; }
int cy(const Rect& r) { return r.y + r.h / 2; }

void text(const char* value, int x, int y, uint16_t color = TFT_WHITE, int size = 2,
          textdatum_t datum = middle_center) {
  M5.Display.setTextDatum(datum);
  M5.Display.setTextSize(size);
  M5.Display.setTextColor(color);
  M5.Display.drawString(value, x, y);
}

void frame(const Rect& r, uint16_t border = kCyan) {
  M5.Display.fillRoundRect(r.x, r.y, r.w, r.h, 10, kPanel);
  M5.Display.drawRoundRect(r.x, r.y, r.w, r.h, 10, border);
}

void button(const Rect& r, const char* label, bool selected = false, bool enabled = true,
            int size = 2) {
  if (enabled) focus_nav::note(r.x, r.y, r.w, r.h);
  const uint16_t border = enabled ? (selected ? kGreen : kCyan) : TFT_DARKGREY;
  M5.Display.fillRoundRect(r.x, r.y, r.w, r.h, 8, selected ? kSelected : kPanel);
  M5.Display.drawRoundRect(r.x, r.y, r.w, r.h, 8, border);
  text(label, cx(r), cy(r), enabled ? (selected ? kGreen : TFT_WHITE) : kMuted, size);
}

const char* decoder_name() {
  switch (g_snapshot.decoder_state) {
    case DecoderState::unbound: return "UNBOUND";
    case DecoderState::armed: return "ARMED";
    case DecoderState::listening: return "LISTENING";
    case DecoderState::decoding: return "DECODING";
    case DecoderState::ready: return "READY";
    case DecoderState::error: return "ERROR";
  }
  return "UNBOUND";
}

uint16_t decoder_color() {
  switch (g_snapshot.decoder_state) {
    case DecoderState::armed:
    case DecoderState::listening:
    case DecoderState::ready: return kGreen;
    case DecoderState::decoding: return kYellow;
    case DecoderState::error: return TFT_RED;
    case DecoderState::unbound: return kAmber;
  }
  return kMuted;
}

void format_utc(char* out, size_t size, uint32_t epoch) {
  if (out == nullptr || size == 0 || epoch == 0) return;
  const time_t raw = static_cast<time_t>(epoch);
  tm value{};
  if (gmtime_r(&raw, &value) == nullptr) return;
  std::snprintf(out, size, "%02d:%02d:%02d", value.tm_hour, value.tm_min, value.tm_sec);
}

void draw_header() {
  audio_header::draw_brand("FT8 RX");
  M5.Display.drawFastVLine(350, 18, 58, kCyan);
  text("FT8 RX", 390, 42, TFT_WHITE, 4, middle_left);
  text("RECEIVE ONLY", 390, 70, kMuted, 1, middle_left);

  const BandPreset* preset = band(g_snapshot.selected_band);
  char label[48];
  if (preset) {
    std::snprintf(label, sizeof(label), "%s  %.3f MHz", preset->label,
                  static_cast<double>(preset->dial_hz) / 1e6);
  } else {
    std::snprintf(label, sizeof(label), "BAND --");
  }
  text(label, 650, 34, kCyan, 2);

  char utc[24] = "UTC --:--:--";
  if (g_snapshot.clock_valid && g_snapshot.utc_ms) {
    char clock[16]{};
    format_utc(clock, sizeof(clock), static_cast<uint32_t>(g_snapshot.utc_ms / 1000u));
    std::snprintf(utc, sizeof(utc), "UTC %s", clock);
  }
  text(utc, 650, 64, g_snapshot.clock_valid ? kGreen : kAmber, 2);

  audio_header::draw_battery(g_snapshot.battery_percent);
  button(kHome, "HOME");
  M5.Display.drawFastHLine(20, 92, 1240, kGreen);
}

void draw_tabs() {
  static constexpr const char* labels[kTabCount] = {
      "LIVE", "DECODES", "MAP", "HUNTER", "HEARD", "SETUP"};
  for (int i = 0; i < kTabCount; ++i)
    button({i * kTabW + 4, kTabsY + 4, kTabW - 8, 82}, labels[i],
           static_cast<int>(g_tab) == i);
}

void chip(const Rect& r, const char* title, const char* value, uint16_t color = kGreen) {
  frame(r, kGrid);
  text(title, cx(r), r.y + 16, kCyan, 1);
  text(value, cx(r), r.y + 42, color, 2);
}

const Decode* decode_newest(size_t offset) {
  if (offset >= g_snapshot.decode_count || g_snapshot.decode_count > kDecodeCapacity) return nullptr;
  return &g_snapshot.decodes[g_snapshot.decode_count - 1 - offset];
}

size_t unique_calls() {
  size_t total = 0;
  const size_t n = std::min(g_snapshot.decode_count, kDecodeCapacity);
  for (size_t i = 0; i < n; ++i) {
    if (!g_snapshot.decodes[i].callsign[0]) continue;
    bool seen = false;
    for (size_t j = 0; j < i; ++j)
      if (std::strcmp(g_snapshot.decodes[i].callsign, g_snapshot.decodes[j].callsign) == 0) {
        seen = true;
        break;
      }
    if (!seen) ++total;
  }
  return total;
}

size_t grid_count() {
  size_t total = 0;
  const size_t n = std::min(g_snapshot.decode_count, kDecodeCapacity);
  for (size_t i = 0; i < n; ++i) total += maidenhead_valid(g_snapshot.decodes[i].grid);
  return total;
}

size_t cq_count() {
  size_t total = 0;
  const size_t n = std::min(g_snapshot.decode_count, kDecodeCapacity);
  for (size_t i = 0; i < n; ++i) total += g_snapshot.decodes[i].kind == DecodeKind::cq;
  return total;
}

void draw_live_rows() {
  const Rect list{24, 486, 1232, 132};
  frame(list, kGrid);
  text("LATEST DECODES", 42, 504, kCyan, 1, middle_left);
  text("UTC", 42, 530, kMuted, 1, middle_left);
  text("SNR", 146, 530, kMuted, 1, middle_left);
  text("DT", 220, 530, kMuted, 1, middle_left);
  text("DF", 300, 530, kMuted, 1, middle_left);
  text("MESSAGE", 395, 530, kMuted, 1, middle_left);
  for (size_t row = 0; row < 3; ++row) {
    const Decode* d = decode_newest(row);
    if (!d) break;
    const int y = 552 + static_cast<int>(row) * 24;
    char utc[16] = "--:--:--", value[20];
    format_utc(utc, sizeof(utc), d->utc_epoch);
    text(utc, 42, y, TFT_WHITE, 1, middle_left);
    std::snprintf(value, sizeof(value), "%+d", d->snr_db);
    text(value, 146, y, TFT_WHITE, 1, middle_left);
    std::snprintf(value, sizeof(value), "%+.1f", d->dt_ms / 1000.0);
    text(value, 220, y, TFT_WHITE, 1, middle_left);
    std::snprintf(value, sizeof(value), "%u", d->audio_hz);
    text(value, 300, y, TFT_WHITE, 1, middle_left);
    text(d->message, 395, y, d->kind == DecodeKind::cq ? kGreen : TFT_WHITE, 1, middle_left);
  }
}

void draw_live() {
  const BandPreset* preset = band(g_snapshot.selected_band);
  char value[48];
  std::snprintf(value, sizeof(value), preset ? "%.3f MHz" : "--",
                preset ? preset->dial_hz / 1e6 : 0.0);
  chip({24, 104, 212, 58}, "DIAL", value, TFT_WHITE);
  chip({246, 104, 150, 58}, "MODE", "USB", TFT_WHITE);
  chip({406, 104, 210, 58}, "AUDIO PASS", "200-3000 Hz", TFT_WHITE);
  chip({626, 104, 190, 58}, "CLOCK", g_snapshot.clock_valid ? "LOCKED" : "NEEDED",
       g_snapshot.clock_valid ? kGreen : kAmber);
  chip({826, 104, 200, 58}, "DECODER", decoder_name(), decoder_color());
  std::snprintf(value, sizeof(value), g_snapshot.gain_auto ? "AUTO %.1f dB" : "MAN %.1f dB",
                g_snapshot.gain_tenth_db / 10.0);
  chip({1036, 104, 220, 58}, "GAIN", value, TFT_WHITE);

  const Rect wf{24, 176, 884, 294};
  frame(wf);
  text("CURRENT 15 SECOND SLOT", 42, 194, kCyan, 1, middle_left);
  M5.Display.fillRect(42, 214, 848, 224, 0x0021);
  for (int hz = 500; hz <= 3000; hz += 500) {
    const int x = 42 + (hz - kAudioLowHz) * 848 / (kAudioHighHz - kAudioLowHz);
    M5.Display.drawFastVLine(x, 214, 224, kGrid);
    char hz_text[12];
    std::snprintf(hz_text, sizeof(hz_text), "%d", hz);
    text(hz_text, x, 451, kMuted, 1);
  }
  for (int row = 1; row < 5; ++row)
    M5.Display.drawFastHLine(42, 214 + row * 44, 848, 0x10a2);

  if (g_snapshot.decoder_state == DecoderState::unbound) {
    text("DECODER NOT BOUND", 466, 305, kAmber, 3);
    text("UI/model sandbox is live; DSP samples are not being decoded yet.", 466, 340, kMuted, 1);
  } else if (!g_snapshot.clock_valid) {
    text("UTC CLOCK REQUIRED", 466, 305, kAmber, 3);
    text("FT8 receive slots depend on accurate 15 second UTC boundaries.", 466, 340, kMuted, 1);
  } else {
    text("WATERFALL INPUT PENDING DSP BINDING", 466, 320, kMuted, 2);
  }

  const Rect timer{926, 176, 330, 294};
  frame(timer);
  text("15 SECOND SLOT", cx(timer), 199, kCyan, 1);
  const SlotClock slot = slot_clock(g_snapshot.utc_ms);
  const int center_x = cx(timer), center_y = 279, radius = 62;
  M5.Display.drawCircle(center_x, center_y, radius, kGrid);
  M5.Display.drawCircle(center_x, center_y, radius - 1, kGrid);
  if (g_snapshot.clock_valid) {
    const int progress = static_cast<int>(360u * slot.elapsed_ms / kSlotMs);
    M5.Display.drawArc(center_x, center_y, radius, radius - 6, -90, -90 + progress, kGreen);
  }
  std::snprintf(value, sizeof(value), g_snapshot.clock_valid ? "%.1f" : "--.-",
                g_snapshot.clock_valid ? slot.remaining_ms / 1000.0 : 0.0);
  text(value, center_x, center_y - 4, TFT_WHITE, 4);
  text("seconds", center_x, center_y + 34, kMuted, 1);
  text(decoder_name(), center_x, 368, decoder_color(), 2);
  std::snprintf(value, sizeof(value), "%u candidates", g_snapshot.candidate_count);
  text(value, center_x, 397, TFT_WHITE, 1);
  M5.Display.drawFastHLine(946, 420, 290, kGrid);
  text("LAST SLOT", 952, 440, kMuted, 1, middle_left);
  std::snprintf(value, sizeof(value), "%u decoded", g_snapshot.last_slot_decodes);
  text(value, 1228, 440, kGreen, 1, middle_right);
  draw_live_rows();
}

void draw_decodes() {
  char value[32];
  std::snprintf(value, sizeof(value), "%u", static_cast<unsigned>(g_snapshot.decode_count));
  chip({24, 104, 190, 58}, "SESSION", value);
  std::snprintf(value, sizeof(value), "%u", static_cast<unsigned>(unique_calls()));
  chip({224, 104, 190, 58}, "UNIQUE CALLS", value);
  std::snprintf(value, sizeof(value), "%u", static_cast<unsigned>(cq_count()));
  chip({424, 104, 190, 58}, "CQ CALLS", value);
  std::snprintf(value, sizeof(value), "%u", static_cast<unsigned>(grid_count()));
  chip({624, 104, 190, 58}, "GRID LOCATORS", value);
  chip({824, 104, 204, 58}, "CLOCK", g_snapshot.clock_valid ? "LOCKED" : "NEEDED",
       g_snapshot.clock_valid ? kGreen : kAmber);
  chip({1038, 104, 218, 58}, "DECODER", decoder_name(), decoder_color());

  const Rect list{24, 176, 1232, 442};
  frame(list);
  const int x[] = {42, 142, 212, 292, 372, 476, 1080};
  const char* headers[] = {"UTC", "SNR", "DT", "DF", "TYPE", "MESSAGE", "GRID"};
  for (size_t i = 0; i < 7; ++i) text(headers[i], x[i], 198, kCyan, 1, middle_left);
  M5.Display.drawFastHLine(36, 218, 1208, kGrid);

  const size_t page_size = 8;
  const size_t start = g_decode_page * page_size;
  for (size_t row = 0; row < page_size; ++row) {
    const Decode* d = decode_newest(start + row);
    if (!d) break;
    const int y = 246 + static_cast<int>(row) * 42;
    if (row & 1u) M5.Display.fillRect(34, y - 18, 1212, 36, 0x0821);
    char utc[16] = "--:--:--", item[20];
    format_utc(utc, sizeof(utc), d->utc_epoch);
    text(utc, x[0], y, TFT_WHITE, 1, middle_left);
    std::snprintf(item, sizeof(item), "%+d", d->snr_db);
    text(item, x[1], y, TFT_WHITE, 1, middle_left);
    std::snprintf(item, sizeof(item), "%+.1f", d->dt_ms / 1000.0);
    text(item, x[2], y, TFT_WHITE, 1, middle_left);
    std::snprintf(item, sizeof(item), "%u", d->audio_hz);
    text(item, x[3], y, TFT_WHITE, 1, middle_left);
    text(kind_name(d->kind), x[4], y, d->kind == DecodeKind::cq ? kGreen : kYellow, 1, middle_left);
    text(d->message, x[5], y, TFT_WHITE, 1, middle_left);
    text(d->grid, x[6], y, maidenhead_valid(d->grid) ? kGreen : kMuted, 1, middle_left);
  }
  button(kClear, "CLEAR");
}

void map_point(const GeoPoint& p, int* x, int* y) {
  constexpr int left = 50, top = 158, width = 824, height = 420;
  *x = left + static_cast<int>((p.longitude + 180.0f) / 360.0f * width);
  *y = top + static_cast<int>((90.0f - p.latitude) / 180.0f * height);
}

void draw_map() {
  const Rect map{24, 104, 884, 514};
  frame(map);
  text("MAIDENHEAD WORLD GRID", 42, 124, kCyan, 1, middle_left);
  text("Station-reported locators only", 890, 124, kMuted, 1, middle_right);
  constexpr int left = 50, top = 158, width = 824, height = 420;
  M5.Display.fillRect(left, top, width, height, 0x0021);
  M5.Display.drawRect(left, top, width, height, kGrid);
  for (int lon = -150; lon <= 150; lon += 30) {
    const int x = left + (lon + 180) * width / 360;
    M5.Display.drawFastVLine(x, top, height, kGrid);
  }
  for (int lat = -60; lat <= 60; lat += 30) {
    const int y = top + (90 - lat) * height / 180;
    M5.Display.drawFastHLine(left, y, width, kGrid);
  }
  const size_t n = std::min(g_snapshot.decode_count, kDecodeCapacity);
  for (size_t i = 0; i < n; ++i) {
    const Decode& d = g_snapshot.decodes[i];
    if (!maidenhead_valid(d.grid)) continue;
    GeoPoint point{};
    if (!maidenhead_center(d.grid, &point)) continue;
    int x = 0, y = 0;
    map_point(point, &x, &y);
    M5.Display.fillCircle(x, y, 4, d.kind == DecodeKind::cq ? kGreen : kYellow);
  }

  const Rect recent{926, 104, 330, 514};
  frame(recent, kGrid);
  text("RECENT GRIDS", 946, 127, kCyan, 1, middle_left);
  size_t row = 0;
  for (size_t offset = 0; offset < n && row < 7; ++offset) {
    const Decode* d = decode_newest(offset);
    if (!d || !maidenhead_valid(d->grid)) continue;
    const int y = 166 + static_cast<int>(row) * 61;
    M5.Display.drawRoundRect(942, y, 298, 50, 7, kGrid);
    text(d->grid, 956, y + 17, kGreen, 1, middle_left);
    text(d->callsign[0] ? d->callsign : "--", 1030, y + 17, TFT_WHITE, 1, middle_left);
    text(kind_name(d->kind), 956, y + 38, kMuted, 1, middle_left);
    ++row;
  }
  if (row == 0) text("No decoded locators yet", cx(recent), 280, kMuted, 1);
}

constexpr Rect kHunterFast{34, 144, 206, 52};
constexpr Rect kHunterDecode{250, 144, 226, 52};
constexpr Rect kHunterStop{486, 144, 142, 52};
constexpr Rect kHunterBest{638, 144, 286, 52};
constexpr Rect kHunterStatus{934, 144, 304, 52};

bool hunter_active() {
  const HunterPhase phase = g_snapshot.hunter.phase;
  return phase == HunterPhase::tuning ||
         phase == HunterPhase::waiting_slot ||
         phase == HunterPhase::observing ||
         phase == HunterPhase::decoding;
}

uint16_t hunter_evidence_color(HunterEvidence evidence) {
  switch (evidence) {
    case HunterEvidence::quiet: return kMuted;
    case HunterEvidence::energy: return kYellow;
    case HunterEvidence::signature: return kCyan;
    case HunterEvidence::decoded: return kGreen;
  }
  return kMuted;
}

Rect hunter_band_rect(size_t index) {
  const int col = static_cast<int>(index % 4);
  const int row = static_cast<int>(index / 4);
  return {34 + col * 302, 206 + row * 126, 286, 112};
}

void draw_hunter_band(size_t index) {
  const BandPreset* preset = band(index);
  if (!preset) return;
  const HunterBandResult& result = g_snapshot.hunter.results[index];
  const Rect r = hunter_band_rect(index);
  const bool current = g_snapshot.hunter.current_band == index && hunter_active();
  const bool best = g_snapshot.hunter.best_band == index && result.visited;
  const bool selected = g_snapshot.selected_band == index;

  uint16_t border = kGrid;
  if (current) border = kYellow;
  else if (best) border = kGreen;
  else if (selected) border = kCyan;
  else if (result.visited) border = hunter_evidence_color(result.evidence);

  if (!hunter_active()) focus_nav::note(r.x, r.y, r.w, r.h);
  M5.Display.fillRoundRect(r.x, r.y, r.w, r.h, 9, current ? kSelected : kPanel);
  M5.Display.drawRoundRect(r.x, r.y, r.w, r.h, 9, border);

  char value[48];
  text(preset->label, r.x + 16, r.y + 24,
       current || best ? kGreen : TFT_WHITE, 3, middle_left);
  std::snprintf(value, sizeof(value), "%.3f MHz",
                static_cast<double>(preset->dial_hz) / 1e6);
  text(value, r.x + r.w - 14, r.y + 23, kCyan, 1, middle_right);

  if (current) {
    text(hunter_phase_name(g_snapshot.hunter.phase), r.x + 16, r.y + 58,
         kYellow, 2, middle_left);
  } else if (result.visited) {
    text(hunter_evidence_name(result.evidence), r.x + 16, r.y + 58,
         hunter_evidence_color(result.evidence), 2, middle_left);
  } else {
    text(preset->common ? "NOT CHECKED" : "OPTIONAL", r.x + 16, r.y + 58,
         kMuted, 2, middle_left);
  }

  if (result.visited) {
    std::snprintf(value, sizeof(value), "%u slot%s  %u sync",
                  result.slots_observed, result.slots_observed == 1 ? "" : "s",
                  result.sync_candidates);
    text(value, r.x + 16, r.y + 88, kMuted, 1, middle_left);
    std::snprintf(value, sizeof(value), "%u decoded", result.valid_decodes);
    text(value, r.x + r.w - 14, r.y + 88,
         result.valid_decodes ? kGreen : kMuted, 1, middle_right);
  } else {
    text("tap to listen here", r.x + 16, r.y + 88, kMuted, 1, middle_left);
  }

  if (best) text("BEST", r.x + r.w - 14, r.y + 58, kGreen, 1, middle_right);
}

void draw_hunter() {
  frame(kBody);
  text("FT8 HUNTER", 42, 124, kCyan, 1, middle_left);
  text("QUIET -> ENERGY -> FT8 SIGNATURE -> VALID DECODE",
       1238, 124, kMuted, 1, middle_right);

  const bool active = hunter_active();
  button(kHunterFast, "FAST HUNT",
         active && g_snapshot.hunter.mode == HunterMode::fast, !active);
  button(kHunterDecode, "DECODE HUNT",
         active && g_snapshot.hunter.mode == HunterMode::decode, !active);
  button(kHunterStop, "STOP", false, active);
  const bool best_ready = !active && g_snapshot.hunter.best_band < band_count();
  button(kHunterBest, "LISTEN BEST", best_ready, best_ready);

  frame(kHunterStatus, kGrid);
  char status[64];
  if (active && g_snapshot.hunter.current_band < band_count()) {
    const BandPreset* current = band(g_snapshot.hunter.current_band);
    std::snprintf(status, sizeof(status), "%s  %s",
                  current ? current->label : "--",
                  hunter_phase_name(g_snapshot.hunter.phase));
  } else {
    std::snprintf(status, sizeof(status), "%s  %s",
                  hunter_mode_name(g_snapshot.hunter.mode),
                  hunter_phase_name(g_snapshot.hunter.phase));
  }
  text(status, cx(kHunterStatus), cy(kHunterStatus), active ? kYellow : kGreen, 1);

  for (size_t i = 0; i < band_count(); ++i) draw_hunter_band(i);
}

void draw_heard() {
  frame(kBody);
  text("UNIQUE STATIONS HEARD", 42, 126, kCyan, 1, middle_left);
  text("Derived only from decoded callsigns; no Internet lookup", 1238, 126, kMuted, 1, middle_right);
  size_t out = 0;
  const size_t n = std::min(g_snapshot.decode_count, kDecodeCapacity);
  for (size_t offset = 0; offset < n && out < 8; ++offset) {
    const Decode* d = decode_newest(offset);
    if (!d || !d->callsign[0]) continue;
    bool duplicate = false;
    for (size_t newer = 0; newer < offset; ++newer) {
      const Decode* prior = decode_newest(newer);
      if (prior && std::strcmp(prior->callsign, d->callsign) == 0) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) continue;
    const int y = 174 + static_cast<int>(out) * 52;
    if (out & 1u) M5.Display.fillRect(34, y - 19, 1212, 39, 0x0821);
    text(d->callsign, 52, y, kGreen, 2, middle_left);
    text(d->grid[0] ? d->grid : "----", 286, y,
         maidenhead_valid(d->grid) ? kCyan : kMuted, 2, middle_left);
    char info[80];
    std::snprintf(info, sizeof(info), "last %+d dB   %u Hz   %s", d->snr_db, d->audio_hz,
                  kind_name(d->kind));
    text(info, 454, y, TFT_WHITE, 1, middle_left);
    ++out;
  }
  if (out == 0) text("No decoded callsigns yet", 640, 350, kMuted, 2);
}

void setup_row(int row, const char* label, const char* value, uint16_t color) {
  const int y = 150 + row * 68;
  M5.Display.drawFastHLine(42, y + 48, 1196, kGrid);
  text(label, 54, y + 22, kMuted, 2, middle_left);
  text(value, 1226, y + 22, color, 2, middle_right);
}

void draw_setup() {
  frame(kBody);
  text("FT8 RX SETUP", 42, 126, kCyan, 1, middle_left);
  setup_row(0, "OPERATING MODE", "RX ONLY", kGreen);
  setup_row(1, "DECODER BINDING", decoder_name(), decoder_color());
  setup_row(2, "UTC SLOT CLOCK", g_snapshot.clock_valid ? "READY" : "NOT ESTABLISHED",
            g_snapshot.clock_valid ? kGreen : kAmber);
  setup_row(3, "AUDIO PASSBAND", "200 - 3000 Hz", TFT_WHITE);
  setup_row(4, "MAP SOURCE", "OFFLINE MAIDENHEAD GRID", kGreen);
  setup_row(5, "NETWORK REQUIRED", "NO", kGreen);
  text("Baseline intentionally does not claim live FT8 decoding until a DSP backend is bound.",
       54, 584, kMuted, 1, middle_left);
}

void draw_body() {
  M5.Display.fillRect(0, 94, 1280, 532, TFT_BLACK);
  switch (g_tab) {
    case Tab::live: draw_live(); break;
    case Tab::decodes: draw_decodes(); break;
    case Tab::map: draw_map(); break;
    case Tab::hunter: draw_hunter(); break;
    case Tab::heard: draw_heard(); break;
    case Tab::setup: draw_setup(); break;
    case Tab::count: break;
  }
}

}  // namespace

void enter(const Snapshot& snapshot_value) {
  g_snapshot = snapshot_value;
  g_snapshot.decode_count = std::min(g_snapshot.decode_count, kDecodeCapacity);
  g_snapshot.selected_band = std::min(g_snapshot.selected_band, band_count() - 1);
  g_active = true;
  g_tab = Tab::live;
  g_decode_page = 0;
  draw();
}

void update(const Snapshot& snapshot_value) {
  if (!g_active) return;
  const Tab previous_tab = g_tab;
  g_snapshot = snapshot_value;
  g_snapshot.decode_count = std::min(g_snapshot.decode_count, kDecodeCapacity);
  g_snapshot.selected_band = std::min(g_snapshot.selected_band, band_count() - 1);
  g_tab = previous_tab;
  draw();
}

void draw() {
  if (!g_active) return;
  M5.Display.fillScreen(TFT_BLACK);
  draw_header();
  draw_body();
  draw_tabs();
}

Action handle_touch(int32_t x, int32_t y) {
  if (!g_active) return {};
  if (hit(x, y, kHome)) return {ActionKind::home};
  if (y >= kTabsY) {
    const int index = std::clamp(x / kTabW, 0, kTabCount - 1);
    g_tab = static_cast<Tab>(index);
    g_decode_page = 0;
    draw();
    return {};
  }
  if (g_tab == Tab::hunter) {
    if (hit(x, y, kHunterFast) && !hunter_active())
      return {ActionKind::start_hunt_fast};
    if (hit(x, y, kHunterDecode) && !hunter_active())
      return {ActionKind::start_hunt_decode};
    if (hit(x, y, kHunterStop) && hunter_active())
      return {ActionKind::stop_hunt};
    if (hit(x, y, kHunterBest) && !hunter_active() &&
        g_snapshot.hunter.best_band < band_count()) {
      const BandPreset* best = band(g_snapshot.hunter.best_band);
      return best ? Action{ActionKind::lock_hunter_best, best->dial_hz} : Action{};
    }
    if (!hunter_active()) {
      for (size_t i = 0; i < band_count(); ++i) {
        if (!hit(x, y, hunter_band_rect(i))) continue;
        g_snapshot.selected_band = i;
        draw();
        const BandPreset* p = band(i);
        return p ? Action{ActionKind::tune_band, p->dial_hz} : Action{};
      }
    }
  }
  if (g_tab == Tab::decodes && hit(x, y, kClear))
    return {ActionKind::clear_decodes};
  return {};
}

void leave() { g_active = false; }
bool active() { return g_active; }
Tab tab() { return g_tab; }
const Snapshot& snapshot() { return g_snapshot; }

bool self_check() {
  return static_cast<int>(Tab::count) == kTabCount && band_count() >= 10 &&
         kAudioLowHz < kAudioHighHz && orcsdr::ft8::self_check() &&
         hunter_self_check();
}

}  // namespace orcsdr::ft8
