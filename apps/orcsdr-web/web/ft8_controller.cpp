// FT8 screen: the part of OrcSDR's main.cpp that owns the FT8/FT4 state (band and dial, decode store, stations-heard table, band
// hunter, expert tuning), driven by decode events from the node (or from the browser's own decoder: both land in the node's
// history). The dashboard is OrcSDR's, unchanged.
#include <M5Unified.h>
#include <emscripten.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "dashboard_audio_control.hpp"
#include "data_screens.hpp"
#include "ft8_dashboard.hpp"
#include "ft8_decoder_backend.hpp"
#include "ft8_heard_db.hpp"
#include "ft8_hunter.hpp"
#include "ft8_tuning.hpp"
#include "clock_sync_status.hpp"

namespace {

using namespace orcsdr;
using namespace dscreens;
namespace ft8 = orcsdr::ft8;

ft8::Snapshot g_snap;
ft8::DecodeStore g_store;
ft8::Hunter g_hunter;
ft8::HeardDb g_heard;
alignas(8) uint8_t g_heard_memory[64 * 1024];
size_t g_band = 5;   // 20 m
ft8::DigitalMode g_mode = ft8::DigitalMode::ft8;
uint32_t g_dial_override = 0;
bool g_expert = false, g_receiving = false, g_entered = false;
double g_utc_ms = 0;            // the page's wall clock at the last update
double g_utc_at = 0;            // emscripten_get_now() when g_utc_ms was set
uint32_t g_synced_utc = 0;      // seconds: when the page last set the node's clock (0 = it has not)
bool g_node_clock = false;
float g_rf_dbfs = -90;
uint32_t g_slots = 0, g_last_slot_decodes = 0, g_last_publish = 0;
size_t g_item = 0;
char g_request[160];
bool g_request_pending = false;

// band hunt (main.cpp's Ft8HuntRun)
struct Hunt {
  bool active = false;
  uint32_t baseline_slots = 0, step_ms = 0;
} g_hunt;

uint32_t dial_hz(size_t band, ft8::DigitalMode mode) { return g_dial_override ? g_dial_override : ft8::mode_dial_hz(band, mode); }

void request_gain(const char* json) {
  snprintf(g_request, sizeof g_request, "%s", json);
  g_request_pending = true;
}

void request_start(uint32_t hz) {
  snprintf(g_request, sizeof g_request, "{\"op\":\"start\",\"hz\":%u,\"options\":\"mode=%s\"}", (unsigned)hz,
           g_mode == ft8::DigitalMode::ft4 ? "ft4" : g_mode == ft8::DigitalMode::js8_normal ? "js8" : "ft8");   // the page starts the JS8 decoder for mode=js8
  g_request_pending = true;
}

bool select_band(size_t index) {
  if (index >= ft8::band_count()) return false;
  const uint32_t hz = dial_hz(index, g_mode);
  if (hz < 100000) return false;
  g_band = index;
  g_slots = 0;
  request_start(hz);
  return true;
}

bool select_band_table(size_t index) {
  const uint32_t previous = g_dial_override;
  g_dial_override = 0;
  if (select_band(index)) return true;
  g_dial_override = previous;
  return false;
}

bool tune_dial(uint32_t hz) {
  if (hz < ft8::tuning::kMinDialHz || hz > ft8::tuning::kMaxDialHz) return false;
  const size_t nearest = ft8::nearest_band(hz);
  const uint32_t previous = g_dial_override;
  g_dial_override = ft8::mode_dial_hz(nearest, g_mode) == hz ? 0u : hz;
  if (!select_band(nearest)) { g_dial_override = previous; return false; }
  return true;
}

bool hunt_start(ft8::HunterMode) {
  // The node has no waterfall for a quick energy scan, so both hunts listen for a decoded slot on each band ("decode" hunt).
  if (g_mode != ft8::DigitalMode::ft8) return false;
  g_hunt = Hunt{};
  if (!g_hunter.start(ft8::HunterMode::decode)) return false;
  g_hunt.active = true;
  return true;
}

void hunt_service(uint32_t now) {
  if (!g_hunt.active) return;
  if (!g_hunter.active()) {
    g_hunt.active = false;
    const auto& snap = g_hunter.snapshot();
    if (snap.phase == ft8::HunterPhase::complete && snap.best_band != SIZE_MAX) select_band_table(snap.best_band);
    return;
  }
  const auto& snap = g_hunter.snapshot();
  if (snap.phase == ft8::HunterPhase::tuning) {
    if (!select_band_table(g_hunter.current_band())) { g_hunter.stop(); return; }
    g_hunter.mark_tuned();
    g_hunt.baseline_slots = g_slots;
    g_hunt.step_ms = now;
    return;
  }
  if (snap.phase != ft8::HunterPhase::waiting_slot) return;
  if (g_slots > g_hunt.baseline_slots) {   // a full slot decoded after the retune
    ft8::HunterObservation obs;
    obs.slot_complete = true;
    obs.peak_dbfs = g_rf_dbfs;
    obs.valid_decodes = (uint16_t)g_last_slot_decodes;
    obs.energy_detected = g_last_slot_decodes > 0;
    g_hunter.begin_slot();
    g_hunter.mark_decoding();
    g_hunter.finish_slot(obs);
    g_hunt.baseline_slots = g_slots;
    g_hunt.step_ms = now;
  } else if (now - g_hunt.step_ms > 90000u) g_hunter.stop();
}

bool heard_lookup(const char* callsign, ft8::HeardInfo* out) {
  const ft8::HeardEntry* e = g_heard.find(callsign);
  if (!e) return false;
  *out = {};
  snprintf(out->callsign, sizeof out->callsign, "%s", e->callsign);
  snprintf(out->grid, sizeof out->grid, "%s", e->grid);
  out->first_utc = e->first_utc;
  out->last_utc = e->last_utc;
  out->count = e->count;
  out->band_mask = e->band_mask;
  out->mode_mask = e->mode_mask;
  out->best_snr = e->best_snr;
  out->last_snr = e->last_snr;
  out->snr_known = (e->flags & ft8::heard_flag_snr_known) != 0;
  out->worked = (e->flags & ft8::heard_flag_worked) != 0;
  return true;
}

void header_controls() {
  static audio_header::Control control;
  audio_header::draw(control, 40, false, -1);
  audio_header::draw_home_button();
  audio_header::draw_settings_button();
  audio_header::draw_mute_button(false);
  audio_header::draw_visualizer_button(true);
}

void fill(uint32_t now) {
  new (&g_snap) ft8::Snapshot();
  const double utc = g_utc_ms + (emscripten_get_now() - g_utc_at);
  g_snap.clock_valid = g_utc_ms > 0;
  g_snap.utc_ms = g_utc_ms > 0 ? (uint64_t)utc : 0;
  // the page's clock comes from the browser, which is network-synced; it vouches for the node's clock once it has set it
  g_snap.clock_sync_level = (uint8_t)(g_node_clock && g_synced_utc ? clock_sync::Level::synced : clock_sync::Level::unsynced);
  g_snap.clock_sync_age_s = g_synced_utc && g_utc_ms > 0 ? (uint32_t)(utc / 1000.0) - g_synced_utc : 0;
  g_snap.receiver_running = g_receiving;
  g_snap.selected_band = g_band;
  g_snap.mode = g_mode;
  g_snap.decoder_capabilities = ft8::decoder_cap_ft8 | ft8::decoder_cap_ft4 | ft8::decoder_cap_js8_normal;
  g_snap.decoder_state = g_receiving ? ft8::DecoderState::listening : ft8::DecoderState::armed;
  g_snap.last_slot_decodes = (uint8_t)std::min<uint32_t>(g_last_slot_decodes, 255u);
  g_snap.expert_tuning = g_expert;
  g_snap.dial_custom = g_dial_override != 0;
  g_snap.dial_hz = dial_hz(g_band, g_mode);
  g_snap.hunter = g_hunter.snapshot();
  g_snap.iq_dbfs = g_rf_dbfs;
  g_snap.battery_percent = -1;
  g_snap.decode_count = std::min(g_store.size(), ft8::kDecodeCapacity);
  for (size_t i = 0; i < g_snap.decode_count; ++i)
    if (const auto* d = g_store.newest(g_snap.decode_count - 1 - i)) g_snap.decodes[i] = *d;   // oldest first, as the dashboard indexes from the end
  g_last_publish = now;
}

void enter(uint32_t now) {
  if (!g_heard.capacity()) g_heard.begin(g_heard_memory, sizeof g_heard_memory);
  ft8::set_header_hook(header_controls);
  ft8::set_heard_lookup(heard_lookup);
  fill(now);
  ft8::enter(g_snap);
  g_entered = true;
  select_band(g_band);
}

void leave() {
  g_entered = false;
  g_hunter.stop();
  g_hunt.active = false;
  ft8::leave();
}

void update(uint32_t now) {
  hunt_service(now);
  fill(now);
  ft8::update(g_snap);
}

Result touch(int x, int y, bool pressed, uint32_t now) {
  Result r;
  if (!pressed) return r;
  if (audio_header::home_hit(x, y)) { r.exit = true; return r; }
  if (audio_header::settings_hit(x, y) || audio_header::mute_hit(x, y) || audio_header::visualizer_hit(x, y)) return r;
  const ft8::Action a = ft8::handle_touch(x, y);
  using K = ft8::ActionKind;
  switch (a.kind) {
    case K::tune_band: select_band_table(ft8::nearest_band(a.value)); break;
    case K::tune_dial: tune_dial(a.value); break;
    case K::tune_auto: select_band_table(g_band); break;
    case K::set_expert:
      g_expert = a.value != 0;
      if (!g_expert) select_band_table(g_band);
      break;
    case K::clear_decodes: g_store.clear(); g_item = 0; break;
    case K::select_mode:
      if (ft8::valid_mode((uint8_t)a.value) && ft8::mode_supported(g_snap.decoder_capabilities, (ft8::DigitalMode)a.value)) {
        g_mode = (ft8::DigitalMode)a.value;
        g_dial_override = 0;
        select_band(g_band);
      }
      break;
    case K::start_hunt_fast: hunt_start(ft8::HunterMode::fast); break;
    case K::start_hunt_decode: hunt_start(ft8::HunterMode::decode); break;
    case K::stop_hunt: g_hunter.stop(); break;
    case K::lock_hunter_best: select_band_table(ft8::nearest_band(a.value)); break;
    case K::gain_auto: request_gain("{\"op\":\"gain\",\"auto\":1}"); break;
    case K::gain_manual: {
      char text[80];
      snprintf(text, sizeof text, "{\"op\":\"gain\",\"auto\":0,\"tenth\":%u}", (unsigned)a.value);
      request_gain(text);
      break;
    }
    case K::rtl_agc: {
      char text[60];
      snprintf(text, sizeof text, "{\"op\":\"agc\",\"on\":%u}", (unsigned)a.value);
      request_gain(text);
      break;
    }
    case K::sync_clock: request_gain("{\"op\":\"sync_clock\"}"); break;
    default: break;   // free_memory: nothing to free in a browser
  }
  if (a.kind != K::none) update(now);
  return r;
}

void event(const char* line, uint32_t) {
  char type[16] = {};
  str(line, "type", type, sizeof type);
  if (!strcmp(type, "slot")) {
    ++g_slots;
    g_last_slot_decodes = (uint32_t)num(line, "decodes");
    return;
  }
  if (strcmp(type, "decode")) return;
  ft8::Decode d;
  const double slot_ms = num(line, "slot_ms");
  d.utc_epoch = (uint32_t)(slot_ms / 1000.0);
  d.snr_db = (int16_t)num(line, "snr_db");
  if (!strstr(line, "\"snr_db\"")) d.flags |= ft8::decode_flag_snr_unavailable;   // JS8: the message carries the sender's report, nothing is measured
  d.dt_ms = (int16_t)num(line, "dt_ms");
  d.audio_hz = (uint16_t)num(line, "hz");
  str(line, "message", d.message, sizeof d.message);
  d.kind = ft8::classify_message(d.message);
  ft8::parse_cq_fields(d.message, d.callsign, sizeof d.callsign, d.grid, sizeof d.grid);
  d.mode = g_mode;
  d.dial_hz = dial_hz(g_band, g_mode);
  d.band_index = (uint8_t)ft8::nearest_band(d.dial_hz);
  if (d.callsign[0]) {
    ft8::HeardObservation obs;
    obs.callsign = d.callsign;
    obs.grid = d.grid;
    obs.utc = d.utc_epoch;
    obs.band = d.band_index < 16 ? d.band_index : 0;
    obs.mode = (uint8_t)d.mode;
    obs.snr = (int8_t)std::max<int>(-127, std::min<int>(127, d.snr_db));
    obs.snr_known = true;
    const ft8::HeardUpdate up = g_heard.observe(obs);
    if (up.stored && up.before == ft8::HeardClass::heard_before) d.flags |= ft8::decode_flag_heard_before;
    if (up.stored && up.before == ft8::HeardClass::worked) d.flags |= ft8::decode_flag_heard_before | ft8::decode_flag_worked;
  }
  g_store.append(d);
}

void set_num(const char* key, double v) {
  if (!strcmp(key, "receiving")) g_receiving = v != 0;
  else if (!strcmp(key, "utc_ms")) { g_utc_ms = v; g_utc_at = emscripten_get_now(); }
  else if (!strcmp(key, "clock_synced_utc")) g_synced_utc = (uint32_t)v;
  else if (!strcmp(key, "node_clock_ok")) g_node_clock = v != 0;
  else if (!strcmp(key, "rf_dbfs")) g_rf_dbfs = (float)v;
}

const char* take_request() {
  if (!g_request_pending) return nullptr;
  g_request_pending = false;
  return g_request;
}

}  // namespace

namespace dscreens {
extern const Screen kFt8 = {dashboards::Id::ft8, "ft8", enter, leave, update, touch, event, set_num, take_request, nullptr, nullptr};
}
