// See ab_controller.hpp.
#include "ab_controller.hpp"

#include <algorithm>
#include <cstring>

#include "airband_runtime.hpp"
#include "orcsdr_storage.hpp"

namespace abweb {
namespace {

using namespace orcsdr;
namespace ab = orcsdr::airband;

ab::LiveState g_live;
bool g_configured = false;
uint32_t g_pending_tune = 0;
bool g_exit = false;
uint32_t g_filter = 0;       // last width the runtime asked for (0 = use the runtime's own)
uint32_t g_span = 480000;
struct GainRequest { int kind, value; };
GainRequest g_gain[8];
unsigned g_gain_n = 0;

bool hook_tune(uint32_t hz) {
  g_live.frequency_hz = hz;
  g_pending_tune = hz;
  return true;
}
void hook_home() { g_exit = true; }
void hook_none() {}
bool hook_gain(const receiver_controls::Action& a) {
  using K = receiver_controls::ActionKind;
  int kind = 0;
  if (a.kind == K::gain_tenth_db) kind = 1;
  else if (a.kind == K::tuner_agc) kind = 2;
  else if (a.kind == K::rtl_agc) kind = 3;
  if (!kind || g_gain_n >= std::size(g_gain)) return false;
  g_gain[g_gain_n++] = {kind, a.value};
  return true;
}
void hook_filter(uint32_t hz) { g_filter = hz; }
void hook_span_step(int dir) {
  static const uint32_t spans[] = {30000, 60000, 120000, 240000};
  size_t i = 0;
  while (i + 1 < std::size(spans) && spans[i] < g_span) ++i;
  i = static_cast<size_t>(std::clamp<int>(static_cast<int>(i) + dir, 0, static_cast<int>(std::size(spans)) - 1));
  g_span = spans[i];
}
void hook_span_set(uint32_t hz) { if (hz) g_span = std::min<uint32_t>(hz, 240000); }

void ensure_configured() {
  if (g_configured) return;
  g_configured = true;
  ab::Hooks hooks;
  hooks.tune = hook_tune;
  hooks.show_home = hook_home;
  hooks.open_radio_settings = hook_none;
  hooks.open_location_settings = hook_none;
  hooks.apply_gain = hook_gain;
  hooks.apply_filter = hook_filter;
  hooks.scope_span_step = hook_span_step;
  hooks.scope_span_set = hook_span_set;
  ab::configure(hooks);
  g_live.filesystem = &storage::filesystem();
}

}  // namespace

void begin(uint32_t now_ms) {
  ensure_configured();
  g_exit = false;
  g_gain_n = 0;
  g_live.now_ms = now_ms;
  if (g_live.frequency_hz == 0) g_live.frequency_hz = ab::default_frequency();
  ab::enter(g_live);
  g_live.frequency_hz = ab::default_frequency();   // the runtime restores the last frequency it saved
  g_pending_tune = g_live.frequency_hz;
}

void leave() { ab::leave(); }

void set_num(const char* key, double v) {
#define F(name, var) if (!strcmp(key, name)) { var = static_cast<decltype(var)>(v); return; }
  F("channel_db", g_live.channel_db) F("running", g_live.receiver_running) F("sound", g_live.sound_enabled)
  F("battery", g_live.battery_percent) F("loc_set", g_live.location_configured) F("lat_e7", g_live.latitude_e7)
  F("lon_e7", g_live.longitude_e7) F("gain_step_count", g_live.gain_step_count)
  F("tuner_agc", g_live.controls.tuner_agc) F("rtl_agc", g_live.controls.rtl_agc) F("gain_tenth_db", g_live.controls.gain_tenth_db)
  F("volume", g_live.controls.volume) F("cap_gain", g_live.controls.capabilities.rf_gain)
  F("cap_tuner_agc", g_live.controls.capabilities.tuner_agc) F("cap_rtl_agc", g_live.controls.capabilities.rtl_agc)
#undef F
}

void set_gain_step(int index, int tenth_db) {
  if (index >= 0 && index < 32) g_live.gain_steps_tenth_db[index] = static_cast<int16_t>(tenth_db);
}

void service(uint32_t now_ms) {
  g_live.now_ms = now_ms;
  ab::service(g_live);
}

void update(uint32_t now_ms) {
  g_live.now_ms = now_ms;
  ab::update(g_live);
}

void touch(int x, int y, uint32_t now_ms) {
  g_live.now_ms = now_ms;
  ab::handle_touch(x, y, g_live);
}

bool scope_edge_hit(int x, int y) { return ab::scope_edge_hit(x, y); }
void scope_drag(int x, uint32_t now_ms) {
  g_live.now_ms = now_ms;
  ab::scope_drag_filter(x, g_live);
}

uint32_t frequency_hz() { return g_live.frequency_hz; }
uint32_t filter_hz() { return g_filter ? g_filter : ab::filter_bandwidth_hz(); }
bool audio_open() { return ab::audio_open(); }
uint32_t scope_span_hz() { return g_span; }
uint32_t take_tune() {
  const uint32_t hz = g_pending_tune;
  g_pending_tune = 0;
  return hz;
}
bool exit_requested() {
  const bool e = g_exit;
  g_exit = false;
  return e;
}
bool take_gain(int* kind, int* value) {
  if (!g_gain_n) return false;
  *kind = g_gain[0].kind;
  *value = g_gain[0].value;
  for (unsigned i = 1; i < g_gain_n; ++i) g_gain[i - 1] = g_gain[i];
  --g_gain_n;
  return true;
}
void reload_catalog(uint32_t now_ms) {
  g_live.now_ms = now_ms;
  (void)ab::serial_action("RELOAD", false, 0, g_live);
}

}  // namespace abweb
