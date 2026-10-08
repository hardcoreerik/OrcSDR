#include "ft8_dashboard.hpp"

#include "dashboard_audio_control.hpp"
#include "ft8_decoder_backend.hpp"
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
constexpr Rect kClear{1088, 556, 144, 46};

Snapshot g_snapshot{};
bool g_active = false;
bool g_sprite_ready = false;
void (*g_header_hook)() = nullptr;
uint64_t g_drawn_second = UINT64_MAX;     // UTC second the header clock shows
uint32_t g_drawn_tenth = UINT32_MAX;      // slot-timer tenth the dial shows
M5Canvas g_dial(&M5.Display);             // the slot dial is composed off-screen and pushed in one go
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

// The family name shown in the header and on the tab title.
const char* family_title(DigitalMode mode) {
  if (mode == DigitalMode::ft4) return "FT4 RX";
  return mode_is_js8(mode) ? "JS8 RX" : "FT8 RX";
}
const char* family_brand(DigitalMode mode) {   // longer than 10 characters, so the smaller subtitle fits
  if (mode == DigitalMode::ft4) return "FT4 RECEIVER";
  return mode_is_js8(mode) ? "JS8 RECEIVER" : "FT8 RECEIVER";
}
// Only FT8 has a verified band table so far; the other modes must not show FT8's dial frequencies.
bool mode_has_band_table(DigitalMode mode) { return mode == DigitalMode::ft8; }

// "15", "7.5", "10", "6", "30", "4"
void slot_seconds_text(char* out, size_t size, DigitalMode mode) {
  const uint32_t ms = slot_ms(mode);
  if (ms % 1000u == 0) std::snprintf(out, size, "%u", static_cast<unsigned>(ms / 1000u));
  else std::snprintf(out, size, "%.1f", static_cast<double>(ms) / 1000.0);
}

// A mode can be chosen only when the bound decoder reports it. While no decoder is bound the dashboard stays on its
// default FT8 and every other mode is unavailable; selecting a mode never makes it operational.
bool mode_available(DigitalMode mode) {
  if (g_snapshot.decoder_capabilities == 0) return mode == DigitalMode::ft8;
  return mode_supported(g_snapshot.decoder_capabilities, mode);
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

void draw_utc() {
  char utc[24] = "UTC --:--:--";
  if (g_snapshot.clock_valid && g_snapshot.utc_ms) {
    char clock[16]{};
    format_utc(clock, sizeof(clock), static_cast<uint32_t>(g_snapshot.utc_ms / 1000u));
    std::snprintf(utc, sizeof(utc), "UTC %s", clock);
  }
  M5.Display.fillRect(640, 62, 220, 26, TFT_BLACK);
  text(utc, 640, 75, g_snapshot.clock_valid ? kGreen : kAmber, 2, middle_left);
  g_drawn_second = g_snapshot.utc_ms / 1000u;
}

void draw_header() {
  // Standard dashboard header: brand, divider, title block, then the shared controls on the right. The header
  // repaints only its own band; the screen is never cleared.
  M5.Display.fillRect(0, 0, 1280, 92, TFT_BLACK);
  audio_header::draw_brand(family_brand(g_snapshot.mode));
  M5.Display.drawFastVLine(365, 18, 66, kCyan);
  text(family_title(g_snapshot.mode), 392, 38, TFT_WHITE, 4, middle_left);
  text("RECEIVE ONLY", 600, 40, kMuted, 1, middle_left);

  const BandPreset* preset = band(g_snapshot.selected_band);
  char label[48];
  if (preset && mode_has_band_table(g_snapshot.mode)) {
    std::snprintf(label, sizeof(label), "%s  %.3f MHz", preset->label,
                  static_cast<double>(preset->dial_hz) / 1e6);
  } else {
    std::snprintf(label, sizeof(label), preset ? "BAND TABLE PENDING" : "BAND --");
  }
  text(label, 392, 75, kCyan, 2, middle_left);

  draw_utc();

  M5.Display.drawFastVLine(865, 18, 66, kCyan);
  if (g_header_hook != nullptr) g_header_hook();
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

// Distance and bearing from the receiver to a decode's grid centre. False when the receiver position or the grid is unknown.
bool decode_geometry(const Decode& d, float* km, float* bearing) {
  if (!g_snapshot.station_known || !maidenhead_valid(d.grid)) return false;
  GeoPoint there{};
  if (!maidenhead_center(d.grid, &there)) return false;
  const GeoPoint here{g_snapshot.station_latitude, g_snapshot.station_longitude, 0};
  return distance_bearing(here, there, km, bearing);
}

// The farthest station with a known grid in this session, or false when nothing qualifies.
bool farthest_heard(float* km, char* call, size_t call_size) {
  bool found = false;
  float best = 0.0f;
  for (size_t i = 0; i < g_snapshot.decode_count; ++i) {
    float d_km = 0.0f, bearing = 0.0f;
    if (!decode_geometry(g_snapshot.decodes[i], &d_km, &bearing) || d_km <= best) continue;
    best = d_km;
    found = true;
    std::snprintf(call, call_size, "%s", g_snapshot.decodes[i].callsign);
  }
  *km = best;
  return found;
}

size_t cq_count() {
  size_t total = 0;
  const size_t n = std::min(g_snapshot.decode_count, kDecodeCapacity);
  for (size_t i = 0; i < n; ++i) total += g_snapshot.decodes[i].kind == DecodeKind::cq;
  return total;
}

// The 15 s slot dial. It is composed in a small off-screen canvas and pushed once, so the 10 Hz countdown never
// clears or repaints anything on screen.
void draw_slot_dial() {
  constexpr int kW = 150, kH = 134, kCx = 75, kCy = 67, kRadius = 62, kX = 1091 - 75, kY = 279 - 67;
  const SlotClock slot = slot_clock(g_snapshot.utc_ms, g_snapshot.mode);
  g_drawn_tenth = g_snapshot.clock_valid ? slot.remaining_ms / 100u : UINT32_MAX - 1;
  if (!g_sprite_ready) return;
  g_dial.fillSprite(kPanel);
  g_dial.drawCircle(kCx, kCy, kRadius, kGrid);
  g_dial.drawCircle(kCx, kCy, kRadius - 1, kGrid);
  if (g_snapshot.clock_valid) {
    const int progress = static_cast<int>(360u * slot.elapsed_ms / slot.period_ms);
    g_dial.drawArc(kCx, kCy, kRadius, kRadius - 6, -90, -90 + progress, kGreen);
  }
  char value[16];
  std::snprintf(value, sizeof(value), g_snapshot.clock_valid ? "%.1f" : "--.-",
                g_snapshot.clock_valid ? slot.remaining_ms / 1000.0 : 0.0);
  g_dial.setTextDatum(middle_center);
  g_dial.setTextSize(4);
  g_dial.setTextColor(TFT_WHITE);
  g_dial.drawString(value, kCx, kCy - 4);
  g_dial.setTextSize(1);
  g_dial.setTextColor(kMuted);
  g_dial.drawString("seconds", kCx, kCy + 34);
  g_dial.pushSprite(kX, kY);
  (void)kW; (void)kH;
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
    if (d->flags & decode_flag_snr_unavailable) std::snprintf(value, sizeof(value), "--");
    else std::snprintf(value, sizeof(value), "%+d", d->snr_db);
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
  const bool have_table = preset != nullptr && mode_has_band_table(g_snapshot.mode);
  std::snprintf(value, sizeof(value), have_table ? "%.3f MHz" : "--", have_table ? preset->dial_hz / 1e6 : 0.0);
  chip({24, 104, 212, 58}, "DIAL", value, TFT_WHITE);
  chip({246, 104, 150, 58}, "MODE", mode_name(g_snapshot.mode),
       mode_experimental(g_snapshot.mode) ? kAmber : TFT_WHITE);
  chip({406, 104, 210, 58}, "AUDIO PASS", "200-3000 Hz", TFT_WHITE);
  chip({626, 104, 190, 58}, "CLOCK", g_snapshot.clock_valid ? "LOCKED" : "NEEDED",
       g_snapshot.clock_valid ? kGreen : kAmber);
  chip({826, 104, 200, 58}, "DECODER", decoder_name(), decoder_color());
  std::snprintf(value, sizeof(value), g_snapshot.gain_auto ? "AUTO %.1f dB" : "MAN %.1f dB",
                g_snapshot.gain_tenth_db / 10.0);
  chip({1036, 104, 220, 58}, "GAIN", value, TFT_WHITE);

  const Rect wf{24, 176, 884, 294};
  frame(wf);
  char slot_text[16];
  slot_seconds_text(slot_text, sizeof(slot_text), g_snapshot.mode);
  std::snprintf(value, sizeof(value), "CURRENT %s SECOND SLOT", slot_text);
  text(value, 42, 194, kCyan, 1, middle_left);
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
  std::snprintf(value, sizeof(value), "%s SECOND SLOT", slot_text);
  text(value, cx(timer), 199, kCyan, 1);
  const int center_x = cx(timer);
  draw_slot_dial();
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
  {
    float far_km = 0.0f;
    char far_call[16]{};
    if (farthest_heard(&far_km, far_call, sizeof(far_call))) std::snprintf(value, sizeof(value), "%.0f km", far_km);
    else std::snprintf(value, sizeof(value), "%s", g_snapshot.station_known ? "--" : "SET LOCATION");
    chip({624, 104, 190, 58}, "FARTHEST", value, g_snapshot.station_known ? kGreen : kAmber);
  }
  chip({824, 104, 204, 58}, "CLOCK", g_snapshot.clock_valid ? "LOCKED" : "NEEDED",
       g_snapshot.clock_valid ? kGreen : kAmber);
  chip({1038, 104, 218, 58}, "DECODER", decoder_name(), decoder_color());

  const Rect list{24, 176, 1232, 442};
  frame(list);
  const int x[] = {42, 142, 212, 292, 372, 476, 940, 1030, 1150};
  const char* headers[] = {"UTC", "SNR", "DT", "DF", "TYPE", "MESSAGE", "GRID", "DIST", "BRG"};
  for (size_t i = 0; i < 9; ++i) text(headers[i], x[i], 198, kCyan, 1, middle_left);
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
    if (d->flags & decode_flag_snr_unavailable) std::snprintf(item, sizeof(item), "--");
    else std::snprintf(item, sizeof(item), "%+d", d->snr_db);
    text(item, x[1], y, TFT_WHITE, 1, middle_left);
    std::snprintf(item, sizeof(item), "%+.1f", d->dt_ms / 1000.0);
    text(item, x[2], y, TFT_WHITE, 1, middle_left);
    std::snprintf(item, sizeof(item), "%u", d->audio_hz);
    text(item, x[3], y, TFT_WHITE, 1, middle_left);
    text(kind_name(d->kind), x[4], y, d->kind == DecodeKind::cq ? kGreen : kYellow, 1, middle_left);
    text(d->message, x[5], y, TFT_WHITE, 1, middle_left);
    text(d->grid, x[6], y, maidenhead_valid(d->grid) ? kGreen : kMuted, 1, middle_left);
    float km = 0.0f, bearing = 0.0f;
    if (decode_geometry(*d, &km, &bearing)) {
      std::snprintf(item, sizeof(item), "%.0f km", km);
      text(item, x[7], y, TFT_WHITE, 1, middle_left);
      std::snprintf(item, sizeof(item), "%.0f deg", bearing);
      text(item, x[8], y, TFT_WHITE, 1, middle_left);
    } else {
      text("--", x[7], y, kMuted, 1, middle_left);
      text("--", x[8], y, kMuted, 1, middle_left);
    }
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
    if (d->flags & decode_flag_snr_unavailable)
      std::snprintf(info, sizeof(info), "last %u Hz   %s", d->audio_hz, kind_name(d->kind));
    else
      std::snprintf(info, sizeof(info), "last %+d dB   %u Hz   %s", d->snr_db, d->audio_hz, kind_name(d->kind));
    text(info, 454, y, TFT_WHITE, 1, middle_left);
    ++out;
  }
  if (out == 0) text("No decoded callsigns yet", 640, 350, kMuted, 2);
}

Rect mode_rect(size_t index) { return {42 + static_cast<int>(index) * 172, 140, 164, 60}; }

void draw_mode_button(size_t index) {
  const DigitalMode mode = static_cast<DigitalMode>(index);
  const Rect r = mode_rect(index);
  const bool available = mode_available(mode);
  const bool selected = g_snapshot.mode == mode;
  if (available) focus_nav::note(r.x, r.y, r.w, r.h);
  const uint16_t border = !available ? TFT_DARKGREY : selected ? kGreen : kCyan;
  M5.Display.fillRoundRect(r.x, r.y, r.w, r.h, 8, selected && available ? kSelected : kPanel);
  M5.Display.drawRoundRect(r.x, r.y, r.w, r.h, 8, border);
  text(mode_name(mode), cx(r), r.y + 22, !available ? kMuted : selected ? kGreen : TFT_WHITE, 2);
  const char* sub = !available ? "UNAVAILABLE" : mode_experimental(mode) ? "EXPERIMENTAL" : selected ? "SELECTED" : "";
  text(sub, cx(r), r.y + 46, !available ? kMuted : mode_experimental(mode) ? kAmber : kGreen, 1);
}

void setup_row(int row, const char* label, const char* value, uint16_t color) {
  const int y = 238 + row * 52;
  M5.Display.drawFastHLine(42, y + 40, 1196, kGrid);
  text(label, 54, y + 18, kMuted, 2, middle_left);
  text(value, 1226, y + 18, color, 2, middle_right);
}

void draw_setup() {
  frame(kBody);
  text("RX SETUP   DECODE MODE", 42, 126, kCyan, 1, middle_left);
  for (size_t i = 0; i < kDigitalModeCount; ++i) draw_mode_button(i);
  char slot_text[16], line[96];
  slot_seconds_text(slot_text, sizeof(slot_text), g_snapshot.mode);
  std::snprintf(line, sizeof(line), "%s   %s SECOND SLOT   ONLY MODES THE DECODER REPORTS CAN BE SELECTED",
                mode_name(g_snapshot.mode), slot_text);
  text(line, 42, 214, kMuted, 1, middle_left);
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

namespace {

bool same_content(const Snapshot& a, const Snapshot& b) {
  // Everything the body draws, except the running clock (utc_ms), which has its own partial updates.
  return a.mode == b.mode && a.decoder_capabilities == b.decoder_capabilities && a.clock_valid == b.clock_valid &&
         a.receiver_running == b.receiver_running && a.gain_auto == b.gain_auto &&
         a.gain_tenth_db == b.gain_tenth_db && a.candidate_count == b.candidate_count &&
         a.last_slot_decodes == b.last_slot_decodes && a.selected_band == b.selected_band &&
         a.decoder_state == b.decoder_state && a.decode_count == b.decode_count &&
         std::memcmp(&a.hunter, &b.hunter, sizeof(a.hunter)) == 0 &&
         std::memcmp(a.decodes, b.decodes, a.decode_count * sizeof(Decode)) == 0;
}

void ensure_sprite() {
  if (g_sprite_ready) return;
  g_dial.setPsram(true);
  g_dial.setColorDepth(16);
  g_sprite_ready = g_dial.createSprite(150, 134) != nullptr;
}

}  // namespace

void enter(const Snapshot& snapshot_value) {
  ensure_sprite();
  g_snapshot = snapshot_value;
  g_snapshot.decode_count = std::min(g_snapshot.decode_count, kDecodeCapacity);
  g_snapshot.selected_band = std::min(g_snapshot.selected_band, band_count() - 1);
  g_active = true;
  g_tab = Tab::live;
  g_decode_page = 0;
  draw();
}

// Incremental: nothing is repainted unless what it shows changed. The clock and the slot dial update in their own
// small regions, so the screen never flashes.
void update(const Snapshot& snapshot_value) {
  if (!g_active) return;
  const Snapshot previous = g_snapshot;
  g_snapshot = snapshot_value;
  g_snapshot.decode_count = std::min(g_snapshot.decode_count, kDecodeCapacity);
  g_snapshot.selected_band = std::min(g_snapshot.selected_band, band_count() - 1);
  const bool header_changed = previous.mode != g_snapshot.mode ||
                              previous.selected_band != g_snapshot.selected_band ||
                              previous.clock_valid != g_snapshot.clock_valid ||
                              previous.battery_percent != g_snapshot.battery_percent;
  if (!same_content(previous, g_snapshot) || header_changed) {
    if (header_changed) draw_header();
    if (!same_content(previous, g_snapshot)) draw_body();
    return;
  }
  if (g_snapshot.utc_ms / 1000u != g_drawn_second) draw_utc();
  if (g_tab == Tab::live) {
    const SlotClock slot = slot_clock(g_snapshot.utc_ms, g_snapshot.mode);
    const uint32_t tenth = g_snapshot.clock_valid ? slot.remaining_ms / 100u : UINT32_MAX - 1;
    if (tenth != g_drawn_tenth) draw_slot_dial();
  }
}

void draw() {   // full repaint: entering the screen only
  if (!g_active) return;
  M5.Display.fillScreen(TFT_BLACK);
  draw_header();
  draw_body();
  draw_tabs();
}

Action handle_touch(int32_t x, int32_t y) {
  if (!g_active) return {};
  if (y >= kTabsY) {
    const int index = std::clamp(static_cast<int>(x / kTabW), 0, kTabCount - 1);
    g_tab = static_cast<Tab>(index);
    g_decode_page = 0;
    draw_body();
    draw_tabs();
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
        draw_header();
        draw_body();
        const BandPreset* p = band(i);
        return p ? Action{ActionKind::tune_band, p->dial_hz} : Action{};
      }
    }
  }
  if (g_tab == Tab::setup) {
    for (size_t i = 0; i < kDigitalModeCount; ++i) {
      if (!hit(x, y, mode_rect(i))) continue;
      const DigitalMode mode = static_cast<DigitalMode>(i);
      if (mode_available(mode) && mode != g_snapshot.mode)
        return {ActionKind::select_mode, static_cast<uint32_t>(i)};
      return {};   // an unavailable mode does nothing
    }
  }
  if (g_tab == Tab::decodes && hit(x, y, kClear))
    return {ActionKind::clear_decodes};
  return {};
}

void leave() { g_active = false; }
bool active() { return g_active; }
Tab tab() { return g_tab; }

void set_header_hook(void (*draw_controls)()) { g_header_hook = draw_controls; }

void select_tab(Tab tab) {
  if (!g_active || tab >= Tab::count || tab == g_tab) return;
  g_tab = tab;
  g_decode_page = 0;
  draw_body();
  draw_tabs();
}
const Snapshot& snapshot() { return g_snapshot; }

bool dashboard_self_check() {
  return static_cast<int>(Tab::count) == kTabCount && band_count() >= 10 &&
         kAudioLowHz < kAudioHighHz && orcsdr::ft8::self_check() &&
         hunter_self_check();
}

}  // namespace orcsdr::ft8
