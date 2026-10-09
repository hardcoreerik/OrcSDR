#include "ft8_dashboard.hpp"
#include "ft8_conditions.hpp"
#include "ft8_world_data.hpp"

#include "dashboard_audio_control.hpp"
#include "ft8_decoder_backend.hpp"
#include "ft8_tuning.hpp"
#include "focus_nav.hpp"

#include <M5Unified.h>
#include <esp_heap_caps.h>

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
constexpr Rect kNewer{760, 556, 150, 46};
constexpr Rect kOlder{922, 556, 150, 46};
constexpr size_t kDecodePageSize = 8;

Snapshot g_snapshot{};
bool g_active = false;
bool g_sprite_ready = false;
void (*g_header_hook)() = nullptr;
uint64_t g_drawn_second = UINT64_MAX;     // UTC second the header clock shows
uint32_t g_drawn_tenth = UINT32_MAX;      // slot-timer tenth the dial shows
M5Canvas g_dial(&M5.Display);             // the slot dial is composed off-screen and pushed in one go
Tab g_tab = Tab::live;
size_t g_decode_page = 0;

// Expert tuning panel (opened from the Live DIAL chip while expert tuning is on).
bool g_tune_open = false;
char g_tune_entry[12] = "";
size_t g_tune_step = tuning::kDefaultStepIndex;
uint32_t g_tune_target = 0;      // the dial the panel last asked for; resynced to the receiver's dial when that changes
bool g_tune_invalid = false;     // the last ENTER did not parse or was out of range
constexpr Rect kDialChip{24, 104, 212, 58};
constexpr Rect kTunePanel{240, 104, 800, 512};
constexpr Rect kTuneEntry{260, 168, 300, 46};
constexpr Rect kTuneEnter{260, 462, 300, 52};
constexpr Rect kTuneAuto{260, 524, 300, 46};
constexpr Rect kTuneMinus{590, 276, 216, 86};
constexpr Rect kTunePlus{818, 276, 216, 86};
constexpr Rect kTuneMinus10{590, 372, 216, 56};
constexpr Rect kTunePlus10{818, 372, 216, 56};
constexpr Rect kTuneClose{818, 524, 216, 46};
constexpr Rect kExpertRow{42, 550, 1196, 40};
Rect tune_key_rect(size_t i) { return {260 + static_cast<int>(i % 3) * 104, 224 + static_cast<int>(i / 3) * 58, 96, 52}; }
Rect tune_step_rect(size_t i) { return {590 + static_cast<int>(i % 3) * 148, 168 + static_cast<int>(i / 3) * 50, 140, 42}; }
constexpr char kTuneKeys[12] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '.', '0', '<'};

bool hit(int32_t x, int32_t y, const Rect& r) {
  return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}
int cx(const Rect& r) { return r.x + r.w / 2; }
int cy(const Rect& r) { return r.y + r.h / 2; }

void text(const char* value, int x, int y, uint16_t color = TFT_WHITE, int size = 2,
          textdatum_t datum = middle_center) {
  M5.Display.setTextDatum(datum);
  // The 6x8 built-in font is unreadable on the 1280x720 panel: size 1 draws DejaVu24 and size 0 (dense captions) DejaVu18.
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
bool mode_has_band_table(DigitalMode mode) { return mode == DigitalMode::ft8 || mode == DigitalMode::ft4 || mode == DigitalMode::js8_normal; }

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
  std::printf("ORC_FT8_UI header mode=%u band=%u clock=%d\n", static_cast<unsigned>(g_snapshot.mode), static_cast<unsigned>(g_snapshot.selected_band), g_snapshot.clock_valid ? 1 : 0);
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
    const uint32_t shown = g_snapshot.dial_hz != 0 ? g_snapshot.dial_hz : mode_dial_hz(g_snapshot.selected_band, g_snapshot.mode);
    std::snprintf(label, sizeof(label), "%s  %.4f MHz", g_snapshot.dial_custom ? "CUSTOM" : preset->label, static_cast<double>(shown) / 1e6);
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
  text(title, cx(r), r.y + 16, kCyan, 0);
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
  text("SNR", 190, 530, kMuted, 1, middle_left);
  text("DT", 262, 530, kMuted, 1, middle_left);
  text("DF", 346, 530, kMuted, 1, middle_left);
  text("MESSAGE", 440, 530, kMuted, 1, middle_left);
  for (size_t row = 0; row < 3; ++row) {
    const Decode* d = decode_newest(row);
    if (!d) break;
    const int y = 552 + static_cast<int>(row) * 24;
    char utc[16] = "--:--:--", value[20];
    format_utc(utc, sizeof(utc), d->utc_epoch);
    text(utc, 42, y, TFT_WHITE, 1, middle_left);
    if (d->flags & decode_flag_snr_unavailable) std::snprintf(value, sizeof(value), "--");
    else std::snprintf(value, sizeof(value), "%+d", d->snr_db);
    text(value, 190, y, TFT_WHITE, 1, middle_left);
    std::snprintf(value, sizeof(value), "%+.1f", d->dt_ms / 1000.0);
    text(value, 262, y, TFT_WHITE, 1, middle_left);
    std::snprintf(value, sizeof(value), "%u", d->audio_hz);
    text(value, 346, y, TFT_WHITE, 1, middle_left);
    text(d->message, 440, y, d->kind == DecodeKind::cq ? kGreen : TFT_WHITE, 1, middle_left);
  }
}


// A 256-entry palette (dark blue -> cyan -> green -> yellow -> red) for the waterfall.
uint16_t waterfall_color(uint8_t v) {
  static uint16_t lut[256];
  static bool ready = false;
  if (!ready) {
    for (int i = 0; i < 256; ++i) {
      const float t = static_cast<float>(i) / 255.0f;
      float r, g, b;
      if (t < 0.25f) { r = 0.0f; g = t * 2.0f; b = 0.12f + t * 3.0f; }
      else if (t < 0.5f) { r = 0.0f; g = 0.5f + (t - 0.25f) * 2.0f; b = 0.87f - (t - 0.25f) * 3.4f; }
      else if (t < 0.75f) { r = (t - 0.5f) * 4.0f; g = 1.0f; b = 0.0f; }
      else { r = 1.0f; g = 1.0f - (t - 0.75f) * 4.0f; b = 0.0f; }
      auto clamp01 = [](float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); };
      const uint16_t R = static_cast<uint16_t>(clamp01(r) * 31.0f), G = static_cast<uint16_t>(clamp01(g) * 63.0f),
                     B = static_cast<uint16_t>(clamp01(b) * 31.0f);
      const uint16_t rgb = static_cast<uint16_t>((R << 11) | (G << 5) | B);
      lut[i] = static_cast<uint16_t>((rgb >> 8) | (rgb << 8));   // pushImage takes byte-swapped RGB565 on this panel
    }
    ready = true;
  }
  return lut[v];
}

uint32_t g_drawn_waterfall = UINT32_MAX;
uint32_t g_wf_last_paint_ms = 0;

// Newest row at the top, 2 pixels per row, 848 pixels across the 200-3000 Hz span.
void draw_waterfall() {
  constexpr int kLeft = 42, kTop = 214, kWidth = 848, kRowPx = 2;
  const size_t rows = std::min<size_t>(g_snapshot.wf_rows, 112);
  const size_t bins = g_snapshot.wf_bins;
  if (g_snapshot.waterfall == nullptr || rows == 0 || bins == 0 || g_snapshot.wf_sequence == 0) return;
  static uint16_t line[kWidth * kRowPx];
  const uint32_t seq = g_snapshot.wf_sequence;
  const size_t visible = std::min<size_t>(rows, seq);
  for (size_t r = 0; r < visible; ++r) {
    const uint8_t* src = g_snapshot.waterfall + ((seq - 1 - r) % rows) * bins;
    for (int x = 0; x < kWidth; ++x) {
      const uint16_t color = waterfall_color(src[static_cast<size_t>(x) * bins / kWidth]);
      line[x] = color;
      line[kWidth + x] = color;
    }
    M5.Display.pushImage(kLeft, kTop + static_cast<int>(r) * kRowPx, kWidth, kRowPx, line);
  }
  g_drawn_waterfall = seq;
}

// Shift the picture down by the rows produced since the last paint and draw only those rows at the top.
void scroll_waterfall(size_t max_rows) {
  constexpr int kLeft = 42, kTop = 214, kWidth = 848, kRowPx = 2;
  const uint32_t seq = g_snapshot.wf_sequence;
  const size_t rows = std::min<size_t>(g_snapshot.wf_rows, 112);
  const size_t bins = g_snapshot.wf_bins;
  if (g_snapshot.waterfall == nullptr || rows == 0 || bins == 0 || seq == 0) return;
  if (g_drawn_waterfall == UINT32_MAX || seq < g_drawn_waterfall || seq - g_drawn_waterfall >= 24) {
    draw_waterfall();   // first paint, a restart, or too far behind: repaint everything
    return;
  }
  const size_t fresh = std::min<size_t>(seq - g_drawn_waterfall, max_rows);
  const uint32_t target = g_drawn_waterfall + static_cast<uint32_t>(fresh);   // may stop short of seq: catch up at a steady pace
  {
    static uint32_t window_start = 0, updates = 0, rows_drawn = 0, worst_ms = 0;   // measured paint rate, logged every 5 s
    const uint32_t t0 = millis();
    if (window_start == 0) window_start = t0;
    ++updates;
    rows_drawn += static_cast<uint32_t>(fresh);
    if (t0 - window_start >= 5000u) {
      std::printf("ORC_FT8_WF updates=%u rows=%u last_paint_ms=%u per_5s\n", static_cast<unsigned>(updates), static_cast<unsigned>(rows_drawn), static_cast<unsigned>(g_wf_last_paint_ms));
      window_start = t0;
      updates = rows_drawn = worst_ms = 0;
    }
  }
  const uint32_t paint_start = millis();
  M5.Display.scroll(0, static_cast<int>(fresh) * kRowPx);
  static uint16_t line[kWidth];
  for (size_t k = 0; k < fresh; ++k) {
    const uint8_t* src = g_snapshot.waterfall + ((target - 1 - k) % rows) * bins;
    for (int x = 0; x < kWidth; ++x) line[x] = waterfall_color(src[static_cast<size_t>(x) * bins / kWidth]);
    for (int r = 0; r < kRowPx; ++r)
      M5.Display.pushImage(kLeft, kTop + static_cast<int>(k) * kRowPx + r, kWidth, 1, line);
  }
  g_drawn_waterfall = target;
  g_wf_last_paint_ms = millis() - paint_start;
}

// One small scroll every ~55 ms while rows are waiting (a burst of rows is spread out, not painted at once).
void paced_waterfall_step() {
  static uint32_t last_ms = 0;
  const uint32_t now = millis();
  if (now - last_ms < 55u) return;
  const uint32_t drawn = g_drawn_waterfall == UINT32_MAX ? 0u : g_drawn_waterfall;
  const uint32_t backlog = g_snapshot.wf_sequence - drawn;
  if (g_snapshot.wf_sequence == 0 || (g_drawn_waterfall != UINT32_MAX && backlog == 0)) return;
  last_ms = now;
  scroll_waterfall(backlog > 8 ? 4 : (backlog > 3 ? 2 : 1));
}

void draw_dial_chip(bool have_table) {
  char value[32];
  const uint32_t shown = g_snapshot.dial_hz != 0 ? g_snapshot.dial_hz : (have_table ? mode_dial_hz(g_snapshot.selected_band, g_snapshot.mode) : 0u);
  if (shown != 0) std::snprintf(value, sizeof(value), "%.4f MHz", static_cast<double>(shown) / 1e6);
  else std::snprintf(value, sizeof(value), "--");
  chip(kDialChip, g_snapshot.expert_tuning ? (g_snapshot.dial_custom ? "DIAL  CUSTOM - TAP" : "DIAL  TAP TO TUNE") : "DIAL", value,
       g_snapshot.dial_custom ? kAmber : TFT_WHITE);
  if (g_snapshot.expert_tuning) focus_nav::note(kDialChip.x, kDialChip.y, kDialChip.w, kDialChip.h);
}

void draw_live() {
  const BandPreset* preset = band(g_snapshot.selected_band);
  char value[48];
  const bool have_table = preset != nullptr && mode_has_band_table(g_snapshot.mode);
  draw_dial_chip(have_table);
  chip({246, 104, 150, 58}, "MODE", mode_name(g_snapshot.mode),
       mode_experimental(g_snapshot.mode) ? kAmber : TFT_WHITE);
  chip({406, 104, 210, 58}, "AUDIO PASS", "200-3000 Hz", TFT_WHITE);
  chip({626, 104, 190, 58}, "CLOCK", g_snapshot.clock_valid ? "LOCKED" : "NEEDED",
       g_snapshot.clock_valid ? kGreen : kAmber);
  chip({826, 104, 200, 58}, "DECODER", decoder_name(), decoder_color());
  if (g_snapshot.gain_auto) std::snprintf(value, sizeof(value), "AUTO");   // the readout under AUTO is not the effective gain
  else std::snprintf(value, sizeof(value), "MAN %.1f dB", g_snapshot.gain_tenth_db / 10.0);
  chip({1036, 104, 220, 58}, "GAIN", value, TFT_WHITE);

  const Rect wf{24, 176, 884, 294};
  frame(wf);
  char slot_text[16];
  slot_seconds_text(slot_text, sizeof(slot_text), g_snapshot.mode);
  std::snprintf(value, sizeof(value), "CURRENT %s SECOND SLOT", slot_text);
  text(value, 42, 194, kCyan, 1, middle_left);
  M5.Display.fillRect(42, 214, 848, 224, 0x0021);
  M5.Display.setScrollRect(42, 214, 848, 224, 0x0021);   // incremental waterfall updates scroll this rectangle
  g_drawn_waterfall = UINT32_MAX;
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
    if (g_snapshot.wf_sequence == 0) text("LISTENING...", 466, 320, kMuted, 2);
    else draw_waterfall();
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
  const int x[] = {42, 184, 252, 336, 416, 500, 920, 1018, 1150};
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
    if (!((d->flags & decode_flag_snr_unavailable) && (d->flags & decode_flag_new_station))) text(item, x[1], y, TFT_WHITE, 1, middle_left);   // the NEW badge takes this cell
    std::snprintf(item, sizeof(item), "%+.1f", d->dt_ms / 1000.0);
    text(item, x[2], y, TFT_WHITE, 1, middle_left);
    std::snprintf(item, sizeof(item), "%u", d->audio_hz);
    text(item, x[3], y, TFT_WHITE, 1, middle_left);
    text(kind_name(d->kind), x[4], y, d->kind == DecodeKind::cq ? kGreen : kYellow, 1, middle_left);
    text(d->message, x[5], y, TFT_WHITE, 1, middle_left);
    if (d->flags & decode_flag_new_station) {   // first time this callsign was heard this session
      // The badge sits beside the message in FT8/FT4; JS8 messages are long enough to run under it, so it takes the (unused) SNR cell instead.
      const int badge_x = (d->flags & decode_flag_snr_unavailable) ? 172 : 846;
      M5.Display.drawRoundRect(badge_x, y - 13, 52, 26, 5, kGreen);
      text("NEW", badge_x + 26, y, kGreen, 0);
    }
    text(d->grid, x[6], y, maidenhead_valid(d->grid) ? kGreen : kMuted, 1, middle_left);
    float km = 0.0f, bearing = 0.0f;
    if (decode_geometry(*d, &km, &bearing)) {
      std::snprintf(item, sizeof(item), "%.0f km", km);
      text(item, x[7], y, TFT_WHITE, 1, middle_left);
      std::snprintf(item, sizeof(item), "%.0fÂ°", bearing);
      text(item, x[8], y, TFT_WHITE, 1, middle_left);
    } else {
      text("--", x[7], y, kMuted, 1, middle_left);
      text("--", x[8], y, kMuted, 1, middle_left);
    }
  }
  {
    const size_t pages = (g_snapshot.decode_count + kDecodePageSize - 1) / kDecodePageSize;
    char page_text[32];
    std::snprintf(page_text, sizeof(page_text), "PAGE %u/%u", static_cast<unsigned>(g_decode_page + 1), static_cast<unsigned>(pages ? pages : 1));
    text(page_text, 640, 579, kMuted, 1, middle_center);
    button(kNewer, "NEWER", false, g_decode_page > 0);
    button(kOlder, "OLDER", false, (g_decode_page + 1) * kDecodePageSize < g_snapshot.decode_count);
  }
  button(kClear, "CLEAR");
}

// The map frames the receiver and the stations that were decoded (with a margin), or the whole world when there is nothing to frame.
struct MapView {
  float lon_min = -180.0f, lat_max = 90.0f, lon_span = 360.0f, lat_span = 180.0f;
};
MapView g_map_view{};

void map_point(const GeoPoint& p, int* x, int* y) {
  constexpr int left = 50, top = 158, width = 824, height = 420;
  *x = left + static_cast<int>((p.longitude - g_map_view.lon_min) / g_map_view.lon_span * width);
  *y = top + static_cast<int>((g_map_view.lat_max - p.latitude) / g_map_view.lat_span * height);
}

// The MAP tab always shows the whole world (owner's preference); the view stays a variable so zoom/pan can be added later.
void fit_map_view() { g_map_view = MapView{}; }

// Offline world basemap in the fitted view, styled like the OrcMaps dark theme: slate land on navy water, thin borders.
// Points are 0.01 degree units. Land is filled with an even-odd scanline pass; polygons never cross the antimeridian.
// Draws on `d`, whose origin is the screen point (sub_x, sub_y), so the whole map can be composed off-screen.
void draw_world(lgfx::LovyanGFX& d, int sub_x, int sub_y, int left, int top, int width, int height) {
  constexpr uint16_t kWater = 0x1105, kLand = 0x1082, kCoast = 0x2A2A, kBorder = 0x2124;
  // Projected points for this frame, 16 KB: kept in PSRAM (allocated once) because internal RAM is scarce.
  static int16_t* projected = static_cast<int16_t*>(heap_caps_malloc(2u * 4096u * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (projected == nullptr) return;
  int16_t* const sx = projected;
  int16_t* const sy = projected + 4096;
  const size_t points = std::min<size_t>(kWorldPointCount, 4096);
  for (size_t i = 0; i < points; ++i) {
    int x = 0, y = 0;
    map_point(GeoPoint{kWorldPoints[2 * i + 1] * 0.01f, kWorldPoints[2 * i] * 0.01f, 0}, &x, &y);
    sx[i] = static_cast<int16_t>(std::max(-30000, std::min(30000, x)));
    sy[i] = static_cast<int16_t>(std::max(-30000, std::min(30000, y)));
  }
  d.fillRect(left - sub_x, top - sub_y, width, height, kWater);
  d.setClipRect(left - sub_x, top - sub_y, width, height);
  for (int y = top; y < top + height; ++y) {
    int xs[96];
    int count = 0;
    const int yc = y * 2 + 1;   // sample at the pixel centre in half-pixel units
    for (size_t s = 0; s < kWorldSegmentCount && count < 94; ++s) {
      const WorldSegment& seg = kWorldSegments[s];
      if (seg.kind != 0) continue;
      for (uint16_t i = 0; i + 1 < seg.count && count < 94; ++i) {
        const size_t a = seg.start + i, b = a + 1;
        const int y0 = sy[a] * 2, y1 = sy[b] * 2;
        if ((y0 <= yc) == (y1 <= yc)) continue;
        xs[count++] = sx[a] + static_cast<int>(static_cast<int32_t>(sx[b] - sx[a]) * (yc - y0) / (y1 - y0));
      }
    }
    for (int i = 1; i < count; ++i) {   // insertion sort: few crossings per row
      const int v = xs[i];
      int j = i - 1;
      while (j >= 0 && xs[j] > v) { xs[j + 1] = xs[j]; --j; }
      xs[j + 1] = v;
    }
    for (int i = 0; i + 1 < count; i += 2) {
      const int x0 = std::max(left, xs[i]), x1 = std::min(left + width - 1, xs[i + 1]);
      if (x1 >= x0) d.drawFastHLine(x0 - sub_x, y - sub_y, x1 - x0 + 1, kLand);
    }
  }
  for (size_t s = 0; s < kWorldSegmentCount; ++s) {
    const WorldSegment& seg = kWorldSegments[s];
    for (uint16_t i = 0; i + 1 < seg.count; ++i) {
      const size_t a = seg.start + i, b = a + 1;
      if (a >= points || b >= points) break;
      d.drawLine(sx[a] - sub_x, sy[a] - sub_y, sx[b] - sub_x, sy[b] - sub_y, seg.kind == 0 ? kCoast : kBorder);
    }
  }
  d.clearClipRect();
}

M5Canvas g_map_canvas(&M5.Display);   // the map is composed here and pushed in one go: no blank-then-paint flash on updates
bool g_map_canvas_ready = false;

void draw_map() {
  fit_map_view();
  const Rect map{24, 104, 884, 514};
  frame(map);
  text("MAIDENHEAD GRID", 42, 124, kCyan, 1, middle_left);
  {
    char scale[48];
    std::snprintf(scale, sizeof(scale), "Station-reported locators only   grid %s", g_map_view.lon_span >= 359.0f ? "world" : "fitted");
    text(scale, 890, 124, kMuted, 1, middle_right);
  }
  constexpr int left = 50, top = 158, width = 824, height = 420;
  if (!g_map_canvas_ready) {
    g_map_canvas.setPsram(true);
    g_map_canvas.setColorDepth(16);
    g_map_canvas_ready = g_map_canvas.createSprite(width, height) != nullptr;
  }
  lgfx::LovyanGFX& d = g_map_canvas_ready ? static_cast<lgfx::LovyanGFX&>(g_map_canvas) : static_cast<lgfx::LovyanGFX&>(M5.Display);
  const int sub_x = g_map_canvas_ready ? left : 0, sub_y = g_map_canvas_ready ? top : 0;
  draw_world(d, sub_x, sub_y, left, top, width, height);
  d.drawRect(left - sub_x, top - sub_y, width, height, kGrid);
  const float want = g_map_view.lon_span / 6.0f;   // about six grid columns across the view
  float step = 30.0f;
  for (float candidate : {1.0f, 2.0f, 5.0f, 10.0f, 15.0f, 20.0f, 30.0f}) {
    if (candidate >= want) { step = candidate; break; }
  }
  for (float lon = std::ceil(g_map_view.lon_min / step) * step; lon < g_map_view.lon_min + g_map_view.lon_span; lon += step) {
    int x = 0, y = 0;
    map_point(GeoPoint{0.0f, lon, 0}, &x, &y);
    d.drawFastVLine(x - sub_x, top - sub_y, height, kGrid);
  }
  for (float lat = std::ceil((g_map_view.lat_max - g_map_view.lat_span) / step) * step; lat < g_map_view.lat_max; lat += step) {
    int x = 0, y = 0;
    map_point(GeoPoint{lat, 0.0f, 0}, &x, &y);
    d.drawFastHLine(left - sub_x, y - sub_y, width, kGrid);
  }
  const size_t n = std::min(g_snapshot.decode_count, kDecodeCapacity);
  for (size_t i = 0; i < n; ++i) {
    const Decode& dec = g_snapshot.decodes[i];
    if (!maidenhead_valid(dec.grid)) continue;
    GeoPoint point{};
    if (!maidenhead_center(dec.grid, &point)) continue;
    int x = 0, y = 0;
    map_point(point, &x, &y);
    d.fillCircle(x - sub_x, y - sub_y, 4, dec.kind == DecodeKind::cq ? kGreen : kYellow);
  }
  int you_x = 0, you_y = 0;
  if (g_snapshot.station_known) {   // the receiver itself: a white ring with a cross, labelled
    map_point(GeoPoint{g_snapshot.station_latitude, g_snapshot.station_longitude, 0}, &you_x, &you_y);
    d.drawCircle(you_x - sub_x, you_y - sub_y, 7, TFT_WHITE);
    d.drawFastHLine(you_x - 11 - sub_x, you_y - sub_y, 22, TFT_WHITE);
    d.drawFastVLine(you_x - sub_x, you_y - 11 - sub_y, 22, TFT_WHITE);
  }
  if (g_map_canvas_ready) g_map_canvas.pushSprite(left, top);
  if (g_snapshot.station_known) text("YOU", you_x + 14, you_y - 12, TFT_WHITE, 1, middle_left);

  const Rect recent{926, 104, 330, 514};
  frame(recent, kGrid);
  text("RECENT GRIDS", 946, 127, kCyan, 1, middle_left);
  size_t row = 0;
  for (size_t offset = 0; offset < n && row < 7; ++offset) {
    const Decode* d = decode_newest(offset);
    if (!d || !maidenhead_valid(d->grid)) continue;
    const int y = 166 + static_cast<int>(row) * 61;
    M5.Display.drawRoundRect(942, y, 298, 50, 7, kGrid);
    text(d->grid, 956, y + 15, kGreen, 1, middle_left);
    text(d->callsign[0] ? d->callsign : "--", 1050, y + 15, TFT_WHITE, 1, middle_left);
    text(kind_name(d->kind), 956, y + 37, kMuted, 0, middle_left);
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
    std::snprintf(value, sizeof(value), "%u sync", result.sync_candidates);
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


// Propagation summary from what was decoded: farthest station, median distance and the compass directions the signals come from.
void draw_conditions_panel() {
  const Rect panel{820, 150, 416, 452};
  M5.Display.fillRect(panel.x, panel.y, panel.w, panel.h, TFT_BLACK);
  frame(panel);
  text("CONDITIONS", panel.x + 16, panel.y + 22, kCyan, 1, middle_left);
  if (!g_snapshot.station_known) {
    text("Set your location to see", panel.x + panel.w / 2, panel.y + 190, kAmber, 1);
    text("distance and direction", panel.x + panel.w / 2, panel.y + 212, kAmber, 1);
    return;
  }
  const GeoPoint here{g_snapshot.station_latitude, g_snapshot.station_longitude, 0};
  Conditions c{};
  if (!compute_conditions(g_snapshot.decodes, std::min(g_snapshot.decode_count, kDecodeCapacity), here, &c) || c.stations == 0) {
    text("No stations with a grid yet", panel.x + panel.w / 2, panel.y + 200, kMuted, 1);
    return;
  }
  char line[64];
  std::snprintf(line, sizeof(line), "%u station%s with a grid", static_cast<unsigned>(c.stations), c.stations == 1 ? "" : "s");
  text(line, panel.x + 16, panel.y + 52, TFT_WHITE, 1, middle_left);
  std::snprintf(line, sizeof(line), "FARTHEST  %.0f km  %s", static_cast<double>(c.farthest_km), c.farthest_call);
  text(line, panel.x + 16, panel.y + 80, kGreen, 1, middle_left);
  std::snprintf(line, sizeof(line), "%.0f deg  (%s)", static_cast<double>(c.farthest_bearing_deg), sector_name(bearing_sector(c.farthest_bearing_deg)));
  text(line, panel.x + 16, panel.y + 108, kMuted, 0, middle_left);
  std::snprintf(line, sizeof(line), "MEDIAN  %.0f km", static_cast<double>(c.median_km));
  text(line, panel.x + 16, panel.y + 138, TFT_WHITE, 1, middle_left);

  uint16_t peak = 1;
  for (size_t s = 0; s < kBearingSectors; ++s) peak = std::max(peak, c.sector_counts[s]);
  text("DIRECTION OF SIGNALS", panel.x + 16, panel.y + 174, kCyan, 1, middle_left);
  for (size_t s = 0; s < kBearingSectors; ++s) {
    const int y = panel.y + 198 + static_cast<int>(s) * 31;
    text(sector_name(s), panel.x + 16, y + 10, kMuted, 1, middle_left);
    const int width = static_cast<int>(static_cast<float>(c.sector_counts[s]) / static_cast<float>(peak) * 260.0f);
    if (width > 0) M5.Display.fillRect(panel.x + 56, y, width, 20, kGreen);
    std::snprintf(line, sizeof(line), "%u", static_cast<unsigned>(c.sector_counts[s]));
    text(line, panel.x + 56 + width + 8, y + 10, TFT_WHITE, 1, middle_left);
  }
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
  if (out == 0) text("No decoded callsigns yet", 440, 350, kMuted, 2);
  draw_conditions_panel();
}

// Mode selection is one button per family (FT8, FT4, JS8). JS8 has five submodes; they live in a chip row under the JS8 button,
// shown only while JS8 is the selected family, so the top row stays three large targets.
constexpr size_t kFamilyCount = 3;
constexpr size_t kJs8SubmodeCount = 5;   // Normal, Fast, 40, Slow, 60 (experimental)

size_t family_of(DigitalMode mode) { return mode == DigitalMode::ft8 ? 0 : mode == DigitalMode::ft4 ? 1 : 2; }
DigitalMode family_default(size_t family) { return family == 0 ? DigitalMode::ft8 : family == 1 ? DigitalMode::ft4 : DigitalMode::js8_normal; }
const char* family_name(size_t family) { return family == 0 ? "FT8" : family == 1 ? "FT4" : "JS8CALL"; }
Rect family_rect(size_t index) { return {42 + static_cast<int>(index) * 408, 140, 400, 60}; }
Rect submode_rect(size_t index) { return {42 + static_cast<int>(index) * 240, 206, 232, 30}; }
DigitalMode submode_mode(size_t index) { return static_cast<DigitalMode>(static_cast<size_t>(DigitalMode::js8_normal) + index); }
const char* submode_label(size_t index) {
  static const char* const kNames[kJs8SubmodeCount] = {"NORMAL 15s", "FAST 10s", "40  6s", "SLOW 30s", "60 EXP"};
  return kNames[index];
}

bool family_available(size_t family) {
  if (family != 2) return mode_available(family_default(family));
  for (size_t i = 0; i < kJs8SubmodeCount; ++i)
    if (mode_available(submode_mode(i))) return true;
  return false;
}

void draw_family_button(size_t family) {
  const Rect r = family_rect(family);
  const bool available = family_available(family);
  const bool selected = family_of(g_snapshot.mode) == family;
  if (available) focus_nav::note(r.x, r.y, r.w, r.h);
  const uint16_t border = !available ? TFT_DARKGREY : selected ? kGreen : kCyan;
  M5.Display.fillRoundRect(r.x, r.y, r.w, r.h, 8, selected && available ? kSelected : kPanel);
  M5.Display.drawRoundRect(r.x, r.y, r.w, r.h, 8, border);
  text(family_name(family), cx(r), r.y + 22, !available ? kMuted : selected ? kGreen : TFT_WHITE, 2);
  const char* sub = !available ? "UNAVAILABLE" : selected ? "SELECTED" : "";
  text(sub, cx(r), r.y + 46, !available ? kMuted : kGreen, 0);
}

void draw_submode_chip(size_t index) {
  const DigitalMode mode = submode_mode(index);
  const Rect r = submode_rect(index);
  const bool available = mode_available(mode);
  const bool selected = g_snapshot.mode == mode;
  if (available) focus_nav::note(r.x, r.y, r.w, r.h);
  const uint16_t border = !available ? TFT_DARKGREY : selected ? kGreen : kCyan;
  M5.Display.fillRoundRect(r.x, r.y, r.w, r.h, 6, selected && available ? kSelected : kPanel);
  M5.Display.drawRoundRect(r.x, r.y, r.w, r.h, 6, border);
  text(submode_label(index), cx(r), r.y + 15, !available ? kMuted : selected ? kGreen : mode_experimental(mode) ? kAmber : TFT_WHITE, 1);
}

void setup_row(int row, const char* label, const char* value, uint16_t color) {
  const int y = 238 + row * 52;
  M5.Display.drawFastHLine(42, y + 40, 1196, kGrid);
  text(label, 54, y + 18, kMuted, 2, middle_left);
  text(value, 1226, y + 18, color, 2, middle_right);
}

// The rows whose value follows the decoder or the clock. Repainted alone (row background first) so a status change never repaints the page.
void draw_setup_status_rows() {
  std::printf("ORC_FT8_UI setup_rows\n");
  for (int row = 1; row <= 2; ++row) {
    const int y = 238 + row * 52;
    M5.Display.fillRect(46, y, 1188, 41, kPanel);
  }
  setup_row(1, "DECODER BINDING", decoder_name(), decoder_color());
  setup_row(2, "UTC SLOT CLOCK", g_snapshot.clock_valid ? "READY" : "NOT ESTABLISHED",
            g_snapshot.clock_valid ? kGreen : kAmber);
}

void draw_setup() {
  frame(kBody);
  text("RX SETUP   DECODE MODE", 42, 126, kCyan, 1, middle_left);
  for (size_t i = 0; i < kFamilyCount; ++i) draw_family_button(i);
  if (family_of(g_snapshot.mode) == 2) {
    for (size_t i = 0; i < kJs8SubmodeCount; ++i) draw_submode_chip(i);
  } else {
    char slot_text[16], line[96];
    slot_seconds_text(slot_text, sizeof(slot_text), g_snapshot.mode);
    std::snprintf(line, sizeof(line), "%s   %s SECOND SLOT   ONLY SUPPORTED MODES CAN BE SELECTED",
                  mode_name(g_snapshot.mode), slot_text);
    text(line, 42, 214, kMuted, 1, middle_left);
  }
  setup_row(0, "OPERATING MODE", "RX ONLY", kGreen);
  setup_row(1, "DECODER BINDING", decoder_name(), decoder_color());
  setup_row(2, "UTC SLOT CLOCK", g_snapshot.clock_valid ? "READY" : "NOT ESTABLISHED",
            g_snapshot.clock_valid ? kGreen : kAmber);
  setup_row(3, "AUDIO PASSBAND", "200 - 3000 Hz", TFT_WHITE);
  setup_row(4, "MAP SOURCE", "OFFLINE MAIDENHEAD GRID", kGreen);
  setup_row(5, "NETWORK REQUIRED", "NO", kGreen);
  setup_row(6, "EXPERT TUNING  (DIAL ENTRY + STEP)", g_snapshot.expert_tuning ? "ON" : "OFF", g_snapshot.expert_tuning ? kGreen : kMuted);
  if (g_snapshot.expert_tuning) focus_nav::note(kExpertRow.x, kExpertRow.y, kExpertRow.w, kExpertRow.h);
  text("Receive only. Times need a locked UTC clock. Expert tuning: tap DIAL on Live; OrcDial rotation steps the dial in Hz.", 54, 606, kMuted, 0, middle_left);
}

// Live screen: what a decoder-status, candidate-count or decode change touches. Everything else (dial, mode, waterfall panel) stays as drawn.
int waterfall_state(const Snapshot& v) { return v.decoder_state == DecoderState::unbound ? 0 : !v.clock_valid ? 1 : 2; }

bool live_layout_same(const Snapshot& a, const Snapshot& b) {
  return a.mode == b.mode && a.selected_band == b.selected_band && a.decoder_capabilities == b.decoder_capabilities && a.clock_valid == b.clock_valid &&
         waterfall_state(a) == waterfall_state(b);
}

void draw_live_dynamic(const Snapshot& previous) {
  std::printf("ORC_FT8_UI live_partial\n");
  draw_dial_chip(band(g_snapshot.selected_band) != nullptr && mode_has_band_table(g_snapshot.mode));
  char value[48];
  chip({826, 104, 200, 58}, "DECODER", decoder_name(), decoder_color());
  if (previous.gain_auto != g_snapshot.gain_auto || previous.gain_tenth_db != g_snapshot.gain_tenth_db) {
    if (g_snapshot.gain_auto) std::snprintf(value, sizeof(value), "AUTO");
    else std::snprintf(value, sizeof(value), "MAN %.1f dB", g_snapshot.gain_tenth_db / 10.0);
    chip({1036, 104, 220, 58}, "GAIN", value, TFT_WHITE);
  }
  const Rect timer{926, 176, 330, 294};
  const int center_x = cx(timer);
  M5.Display.fillRect(934, 356, 314, 108, kPanel);   // only the status block under the dial
  text(decoder_name(), center_x, 368, decoder_color(), 2);
  std::snprintf(value, sizeof(value), "%u candidates", g_snapshot.candidate_count);
  text(value, center_x, 397, TFT_WHITE, 1);
  M5.Display.drawFastHLine(946, 420, 290, kGrid);
  text("LAST SLOT", 952, 440, kMuted, 1, middle_left);
  std::snprintf(value, sizeof(value), "%u decoded", g_snapshot.last_slot_decodes);
  text(value, 1228, 440, kGreen, 1, middle_right);
  if (previous.decode_count != g_snapshot.decode_count ||
      std::memcmp(previous.decodes, g_snapshot.decodes, g_snapshot.decode_count * sizeof(Decode)) != 0)
    draw_live_rows();
}

void draw_body(bool repaint_in_place = false);
// ---- Tune panel -------------------------------------------------------------------------------------------------------------------------
void draw_tune_readout() {
  char value[32];
  const uint32_t shown = g_snapshot.dial_hz != 0 ? g_snapshot.dial_hz : g_tune_target;
  tuning::format_mhz(shown, value, sizeof(value));
  M5.Display.fillRect(262, 118, 776, 44, kPanel);
  text(value, 640, 140, g_snapshot.dial_custom ? kAmber : TFT_WHITE, 3);
  const bool bad = g_tune_invalid;
  M5.Display.fillRect(262, 580, 776, 30, kPanel);
  text(bad ? "ENTRY NOT ACCEPTED - TYPE MHz, e.g. 7.078" : g_snapshot.dial_custom ? "CUSTOM DIAL  (AUTO returns to the band table)" : "AUTO: band table dial", 640, 596, bad ? kAmber : kMuted, 0);
}

void draw_tune_entry() {
  M5.Display.fillRoundRect(kTuneEntry.x, kTuneEntry.y, kTuneEntry.w, kTuneEntry.h, 8, TFT_BLACK);
  M5.Display.drawRoundRect(kTuneEntry.x, kTuneEntry.y, kTuneEntry.w, kTuneEntry.h, 8, kCyan);
  char shown[24];
  std::snprintf(shown, sizeof(shown), "%s%s", g_tune_entry[0] ? g_tune_entry : "type MHz", g_tune_entry[0] ? " MHz" : "");
  text(shown, kTuneEntry.x + 14, cy(kTuneEntry), g_tune_entry[0] ? TFT_WHITE : kMuted, 2, middle_left);
}

void draw_tune_steps() {
  for (size_t i = 0; i < tuning::kStepCount; ++i) button(tune_step_rect(i), tuning::step_label(i), i == g_tune_step, true, 1);
}

void draw_tune_panel() {
  frame(kTunePanel, kCyan);
  text("TUNE  (expert)", 262, 126, kCyan, 1, middle_left);
  draw_tune_readout();
  draw_tune_entry();
  for (size_t i = 0; i < 12; ++i) {
    char label[2] = {kTuneKeys[i] == '<' ? 'X' : kTuneKeys[i], '\0'};
    button(tune_key_rect(i), kTuneKeys[i] == '<' ? "DEL" : label, false, true, kTuneKeys[i] == '<' ? 1 : 2);
  }
  button(kTuneEnter, "TUNE", false, true, 2);
  button(kTuneAuto, "AUTO (BAND TABLE)", false, g_snapshot.dial_custom, 1);
  draw_tune_steps();
  button(kTuneMinus, "- STEP", false, true, 2);
  button(kTunePlus, "+ STEP", false, true, 2);
  button(kTuneMinus10, "- 10 STEPS", false, true, 1);
  button(kTunePlus10, "+ 10 STEPS", false, true, 1);
  button(kTuneClose, "CLOSE", false, true, 1);
  text("Typed values are MHz. Steps use the size chosen above.", 590, 488, kMuted, 0, middle_left);
}

void open_tune_panel() {
  g_tune_open = true;
  g_tune_entry[0] = '\0';
  g_tune_invalid = false;
  g_tune_target = g_snapshot.dial_hz != 0 ? g_snapshot.dial_hz : mode_dial_hz(g_snapshot.selected_band, g_snapshot.mode);
  draw_tune_panel();
}

Action tune_step_action(int32_t detents) {
  g_tune_target = tuning::apply_steps(g_tune_target, detents, tuning::kStepsHz[g_tune_step]);
  g_tune_invalid = false;
  return {ActionKind::tune_dial, g_tune_target};
}

// Returns true when the touch was inside the panel (handled), with *out set when it asks the application to retune.
bool handle_tune_touch(int32_t x, int32_t y, Action* out) {
  if (!hit(x, y, kTunePanel)) return false;
  for (size_t i = 0; i < 12; ++i)
    if (hit(x, y, tune_key_rect(i))) {
      if (tuning::entry_key(g_tune_entry, sizeof(g_tune_entry), kTuneKeys[i])) g_tune_invalid = false;
      draw_tune_entry();
      draw_tune_readout();
      return true;
    }
  for (size_t i = 0; i < tuning::kStepCount; ++i)
    if (hit(x, y, tune_step_rect(i))) {
      g_tune_step = i;
      draw_tune_steps();
      return true;
    }
  if (hit(x, y, kTuneEnter)) {
    uint32_t hz = 0;
    if (tuning::parse_mhz(g_tune_entry, &hz)) {
      g_tune_target = hz;
      g_tune_entry[0] = '\0';
      g_tune_invalid = false;
      *out = {ActionKind::tune_dial, hz};
    } else {
      g_tune_invalid = true;
    }
    draw_tune_entry();
    draw_tune_readout();
    return true;
  }
  if (hit(x, y, kTuneAuto)) {
    if (g_snapshot.dial_custom) *out = {ActionKind::tune_auto, 0};
    return true;
  }
  if (hit(x, y, kTuneMinus)) { *out = tune_step_action(-1); return true; }
  if (hit(x, y, kTunePlus)) { *out = tune_step_action(1); return true; }
  if (hit(x, y, kTuneMinus10)) { *out = tune_step_action(-10); return true; }
  if (hit(x, y, kTunePlus10)) { *out = tune_step_action(10); return true; }
  if (hit(x, y, kTuneClose)) {
    g_tune_open = false;
    draw_body(false);
    return true;
  }
  return true;   // a tap on the panel's own background does nothing
}

void draw_body(bool repaint_in_place) {
  std::printf("ORC_FT8_UI body tab=%u in_place=%d\n", static_cast<unsigned>(g_tab), repaint_in_place ? 1 : 0);
  if (!repaint_in_place) M5.Display.fillRect(0, 94, 1280, 532, TFT_BLACK);   // the map repaints opaque panels, so it skips the blank
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
         a.dial_hz == b.dial_hz && a.dial_custom == b.dial_custom && a.expert_tuning == b.expert_tuning &&
         std::memcmp(&a.hunter, &b.hunter, sizeof(a.hunter)) == 0 &&
         std::memcmp(a.decodes, b.decodes, a.decode_count * sizeof(Decode)) == 0;
}

// Does the active tab show anything that changed? The slot counters, gain and candidate count change every slot; only the tabs that
// display them may repaint for them (Live and Setup have their own partial paths).
bool decodes_differ(const Snapshot& a, const Snapshot& b) {
  return a.decode_count != b.decode_count || std::memcmp(a.decodes, b.decodes, b.decode_count * sizeof(Decode)) != 0;
}
bool station_differs(const Snapshot& a, const Snapshot& b) {
  return a.station_known != b.station_known || a.station_latitude != b.station_latitude || a.station_longitude != b.station_longitude;
}
bool tab_content_changed(Tab tab, const Snapshot& a, const Snapshot& b) {
  switch (tab) {
    case Tab::decodes:
      return decodes_differ(a, b) || station_differs(a, b) || a.clock_valid != b.clock_valid || a.decoder_state != b.decoder_state || a.mode != b.mode;
    case Tab::map: return decodes_differ(a, b) || station_differs(a, b) || a.mode != b.mode || a.selected_band != b.selected_band;
    case Tab::hunter:
      return std::memcmp(&a.hunter, &b.hunter, sizeof(a.hunter)) != 0 || a.mode != b.mode || a.selected_band != b.selected_band;
    case Tab::heard: return decodes_differ(a, b) || a.mode != b.mode;
    default: return !same_content(a, b);
  }
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
  g_tune_open = false;
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
  if (tab_content_changed(g_tab, previous, g_snapshot) || header_changed) {
    if (header_changed) draw_header();
    if (g_tune_open) {   // the Tune panel covers the body: only its readout follows the receiver
      if (previous.dial_hz != g_snapshot.dial_hz) g_tune_target = g_snapshot.dial_hz;
      draw_tune_readout();
      return;
    }
    // Live: status, counters and the latest-decodes list repaint alone; the waterfall and dial are not touched.
    if (g_tab == Tab::live && !same_content(previous, g_snapshot) && live_layout_same(previous, g_snapshot) ) {
      draw_live_dynamic(previous);
      return;
    }
    // Setup: a decoder or clock status change repaints only its two rows; a mode change repaints in place without blanking.
    if (g_tab == Tab::setup && !same_content(previous, g_snapshot)) {
      if (previous.mode == g_snapshot.mode && previous.decoder_capabilities == g_snapshot.decoder_capabilities) draw_setup_status_rows();
      else draw_body(true);
      return;
    }
    if (tab_content_changed(g_tab, previous, g_snapshot)) {
      // Opaque-panel tabs repaint in place (no black blank) unless the mode or band changed under them.
      const bool in_place = g_tab != Tab::live && g_tab != Tab::setup && previous.mode == g_snapshot.mode && previous.selected_band == g_snapshot.selected_band;
      const size_t pages = (g_snapshot.decode_count + 7) / 8;
      if (g_decode_page >= (pages ? pages : 1)) g_decode_page = pages ? pages - 1 : 0;
      draw_body(in_place);
    }
    return;
  }
  if (g_snapshot.utc_ms / 1000u != g_drawn_second) draw_utc();
  if (g_tune_open) return;   // never draw the waterfall or dial under the Tune panel
  if (g_tab == Tab::live && g_snapshot.wf_sequence != g_drawn_waterfall && g_snapshot.decoder_state != DecoderState::unbound &&
      g_snapshot.clock_valid) {
    paced_waterfall_step();
  }
  if (g_tab == Tab::live) {
    const SlotClock slot = slot_clock(g_snapshot.utc_ms, g_snapshot.mode);
    const uint32_t tenth = g_snapshot.clock_valid ? slot.remaining_ms / 100u : UINT32_MAX - 1;
    if (tenth != g_drawn_tenth) draw_slot_dial();
  }
}

void draw() {   // full repaint: entering the screen only
  std::printf("ORC_FT8_UI FULL_SCREEN tab=%u\n", static_cast<unsigned>(g_tab));
  if (!g_active) return;
  M5.Display.fillScreen(TFT_BLACK);
  draw_header();
  draw_body();
  draw_tabs();
}

Action handle_touch(int32_t x, int32_t y) {
  if (!g_active) return {};
  if (g_tune_open) {
    if (y < kTabsY) {   // the Tune panel owns the body; taps outside it are ignored
      Action tuned{};
      (void)handle_tune_touch(x, y, &tuned);
      return tuned;
    }
    g_tune_open = false;   // a tab tap closes the panel and switches tabs below
  }
  if (g_tab == Tab::live && g_snapshot.expert_tuning && hit(x, y, kDialChip)) {
    open_tune_panel();
    return {};
  }
  if (g_tab == Tab::setup && hit(x, y, kExpertRow)) return {ActionKind::set_expert, g_snapshot.expert_tuning ? 0u : 1u};
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
    for (size_t i = 0; i < kFamilyCount; ++i) {
      if (!hit(x, y, family_rect(i))) continue;
      if (family_of(g_snapshot.mode) == i) return {};   // already in this family
      // JS8 opens on its first available submode (Normal today).
      DigitalMode target = family_default(i);
      if (i == 2)
        for (size_t k = 0; k < kJs8SubmodeCount; ++k)
          if (mode_available(submode_mode(k))) {
            target = submode_mode(k);
            break;
          }
      if (mode_available(target)) return {ActionKind::select_mode, static_cast<uint32_t>(target)};
      return {};   // an unavailable mode does nothing
    }
    if (family_of(g_snapshot.mode) == 2)
      for (size_t k = 0; k < kJs8SubmodeCount; ++k) {
        if (!hit(x, y, submode_rect(k))) continue;
        const DigitalMode mode = submode_mode(k);
        if (mode_available(mode) && mode != g_snapshot.mode) return {ActionKind::select_mode, static_cast<uint32_t>(mode)};
        return {};
      }
  }
  if (g_tab == Tab::decodes && hit(x, y, kNewer) && g_decode_page > 0) {
    --g_decode_page;
    draw_body(true);
    return {};
  }
  if (g_tab == Tab::decodes && hit(x, y, kOlder) && (g_decode_page + 1) * kDecodePageSize < g_snapshot.decode_count) {
    ++g_decode_page;
    draw_body(true);
    return {};
  }
  if (g_tab == Tab::decodes && hit(x, y, kClear))
    return {ActionKind::clear_decodes};
  return {};
}

void leave() {
  g_active = false;
  g_tune_open = false;
}
uint32_t tune_step_hz() { return tuning::kStepsHz[g_tune_step]; }
bool active() { return g_active; }
Tab tab() { return g_tab; }

void set_header_hook(void (*draw_controls)()) { g_header_hook = draw_controls; }

void select_tab(Tab tab) {
  if (!g_active || tab >= Tab::count || tab == g_tab) return;
  g_tune_open = false;
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

// Called from the main loop between full updates: new waterfall rows scroll in without waiting for the next snapshot.
void pump_waterfall(uint32_t sequence) {
  if (!g_active || g_tune_open || g_tab != Tab::live || g_snapshot.decoder_state == DecoderState::unbound || !g_snapshot.clock_valid) return;
  g_snapshot.wf_sequence = sequence;
  paced_waterfall_step();
}

}  // namespace orcsdr::ft8
