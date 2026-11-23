// Web entry point for the shared OrcSDR UI (core/ui). State goes in through m5_set_*()/m5_commit(),
// actions come out of m5_touch()/m5_key(). The page owns all I/O (REST calls, mock data); this file
// only renders into a RAM sprite that JavaScript blits to a canvas, and plays the part of OrcSDR's
// main.cpp for screen navigation (Home <-> dashboards).
#include <M5Unified.h>
#include <emscripten.h>
#include <math.h>
#include <string.h>
#include <string>
#include <vector>

#include "ab_controller.hpp"
#include "airband_audio_filter.hpp"
#include "airband_runtime.hpp"
#include "am_dashboard.hpp"
#include "cb_controller.hpp"
#include "cb_dashboard.hpp"
#include "data_screens.hpp"
#include "dashboard_audio_control.hpp"
#include "fm_dashboard.hpp"
#include "freq_keypad.hpp"
#include "home_dashboard.hpp"
#include "keyboard_input.hpp"
#include "nvs_store.hpp"
#include "shortwave_audio_dsp.hpp"
#include "shortwave_dashboard.hpp"
#include "sw_controller.hpp"
#include "text_editor.hpp"
#include "web_storage_api.hpp"

M5Class M5;
using namespace orcsdr;

namespace {

enum Screen : int { kHome = 0, kFm = 1, kAm = 2, kCb = 3, kSw = 4, kAb = 5, kData = 6 };

int g_screen = kHome;
const dscreens::Screen* g_ds = nullptr;   // the active decoder dashboard when g_screen == kData
const dscreens::Screen* g_ds_left = nullptr;   // the one just left: requests it queued on the way out (settings restored) still go to the page
home::Snapshot g_snap, g_committed;
fm::Snapshot g_fm_snap, g_fm_committed;
am::Snapshot g_am_snap;
NvsStore g_nvs;
bool g_am_drag_used = false;
bool g_ab_scope_drag = false;
float g_cb_bins[512];
float g_sw_hunt_bins[512];
uint32_t g_cb_sps = 1800000, g_cb_center = 27535000;
const char* g_action_name = "none";
unsigned g_action_value = 0;
int g_action_dashboard = (int)dashboards::Id::count;
float g_levels[512];
bool g_started = false;

template <typename T>
void set_int(T& field, double v) { field = static_cast<T>(v); }

const char* home_action_name(home::ActionKind k) {
  using K = home::ActionKind;
  switch (k) {
    case K::none: return "none";
    case K::open_dashboard: return "open_dashboard";
    case K::open_browser: return "open_browser";
    case K::close_browser: return "close_browser";
    case K::browser_previous: return "browser_previous";
    case K::browser_next: return "browser_next";
    case K::tune_frequency: return "tune_frequency";
    case K::span_down: return "span_down";
    case K::span_up: return "span_up";
    case K::step_down: return "step_down";
    case K::step_up: return "step_up";
    case K::step_size_down: return "step_size_down";
    case K::step_size_up: return "step_size_up";
    case K::sound_toggle: return "sound_toggle";
    case K::volume_down: return "volume_down";
    case K::volume_up: return "volume_up";
    case K::open_device_settings: return "open_device_settings";
    case K::waterfall_contrast_down: return "waterfall_contrast_down";
    case K::waterfall_contrast_up: return "waterfall_contrast_up";
    case K::waterfall_palette_next: return "waterfall_palette_next";
    case K::waterfall_speed_next: return "waterfall_speed_next";
    case K::gain_open: return "gain_open";
    case K::gain_close: return "gain_close";
    case K::filter_open: return "filter_open";
    case K::filter_close: return "filter_close";
    case K::filter_set: return "filter_set";
    case K::filter_standard: return "filter_standard";
    case K::filter_edges: return "filter_edges";
    case K::gain_auto: return "gain_auto";
    case K::gain_tenth_db: return "gain_tenth_db";
    case K::rtl_agc: return "rtl_agc";
    case K::mode_open: return "mode_open";
    case K::mode_close: return "mode_close";
    case K::mode_set: return "mode_set";
    case K::keypad_open: return "keypad_open";
  }
  return "none";
}

const char* fm_action_name(fm::ActionKind k) {
  using K = fm::ActionKind;
  switch (k) {
    case K::none: return "none";
    case K::tune_hz: return "tune_frequency";
    case K::step_down: return "step_down";
    case K::step_up: return "step_up";
    case K::seek_down: return "seek_down";
    case K::seek_up: return "seek_up";
    case K::save_preset: return "save_preset";
    case K::step_cycle: return "step_cycle";
    case K::filter_down: return "filter_down";
    case K::filter_up: return "filter_up";
    case K::span_down: return "span_down";
    case K::span_up: return "span_up";
    case K::sound_toggle: return "sound_toggle";
    case K::volume_down: return "volume_down";
    case K::volume_up: return "volume_up";
    case K::graphics_toggle: return "graphics_toggle";
    case K::recording_toggle: return "recording_toggle";
    case K::gain_auto: return "gain_auto";
    case K::gain_tenth_db: return "gain_tenth_db";
    case K::scan_presets: return "scan_presets";
    case K::open_device_settings: return "open_device_settings";
    case K::exit_to_browse: return "exit_to_browse";
    case K::step_cycle_previous: return "step_cycle_previous";
  }
  return "none";
}

const char* am_action_name(am::ActionKind k) {
  using K = am::ActionKind;
  switch (k) {
    case K::none: return "none";
    case K::tune_hz: return "tune_frequency";
    case K::step_down: return "step_down";
    case K::step_up: return "step_up";
    case K::step_cycle: return "step_cycle";
    case K::scan_spacing_toggle: return "scan_spacing_toggle";
    case K::filter_bandwidth_hz: return "filter_set";
    case K::span_down: return "span_down";
    case K::span_up: return "span_up";
    case K::preset_recall: return "preset_recall";
    case K::preset_save: return "preset_save";
    case K::preset_replace: return "preset_replace";
    case K::preset_delete: return "preset_delete";
    case K::sound_toggle: return "sound_toggle";
    case K::volume_down: return "volume_down";
    case K::volume_up: return "volume_up";
    case K::graphics_toggle: return "graphics_toggle";
    case K::recording_toggle: return "recording_toggle";
    case K::gain_auto: return "gain_auto";
    case K::gain_tenth_db: return "gain_tenth_db";
    case K::rtl_agc: return "rtl_agc";
    case K::scan_toggle: return "scan_toggle";
    case K::open_device_settings: return "open_device_settings";
    case K::exit_home: return "exit_home";
  }
  return "none";
}

void clear_action() {
  g_action_name = "none";
  g_action_value = 0;
  g_action_dashboard = (int)dashboards::Id::count;
}

void show_home() {
  if (g_screen == kData && g_ds) {
    g_ds->leave();
    g_ds_left = g_ds;
    g_ds = nullptr;
  }
  if (g_screen == kAb) abweb::leave();
  fm::leave();
  am::leave();
  cb::leave();
  shortwave::leave();
  g_screen = kHome;
  M5.Display.fillScreen(0);
  home::enter(g_snap);
  home::draw();
  g_committed = g_snap;
}

void show_data(const dscreens::Screen* ds) {
  home::leave();
  fm::leave();
  am::leave();
  cb::leave();
  shortwave::leave();
  g_screen = kData;
  g_ds = ds;
  M5.Display.fillScreen(0);
  ds->enter((uint32_t)emscripten_get_now());
}

// The header's VIS and gear buttons open the RF visualizer and Settings from any screen (main.cpp's open_visualizer() and
// open_global_settings()); closing either goes back to the screen it was opened from.
int g_viz_from = kHome;
const dscreens::Screen* g_viz_from_ds = nullptr;
void show_ab();
void show_sw();
void show_cb();
void show_am();
void show_fm();

void open_overlay(const dscreens::Screen* target) {
  g_viz_from = g_screen;
  g_viz_from_ds = g_screen == kData ? g_ds : nullptr;
  if (g_screen == kData && g_ds) {
    g_ds->leave();
    g_ds_left = g_ds;
    g_ds = nullptr;
  }
  show_data(target);
}

void open_viz() { open_overlay(&dscreens::kViz); }

void close_overlay() {
  if (g_ds) {
    g_ds->leave();
    g_ds_left = g_ds;
    g_ds = nullptr;
  }
  switch (g_viz_from) {
    case kFm: show_fm(); break;
    case kAm: show_am(); break;
    case kCb: show_cb(); break;
    case kSw: show_sw(); break;
    case kAb: show_ab(); break;
    case kData: if (g_viz_from_ds) { show_data(g_viz_from_ds); break; } show_home(); break;
    default: show_home(); break;
  }
}

bool is_overlay(const dscreens::Screen* ds) { return ds == &dscreens::kViz || ds == &dscreens::kSettings; }

bool viz_button_applies() {
  if (g_screen != kData) return true;
  return g_ds && !is_overlay(g_ds) && g_ds != &dscreens::kRfLab;   // RF Lab has no header to press; the visualizer is the target
}

void show_ab() {
  home::leave();
  fm::leave();
  am::leave();
  cb::leave();
  shortwave::leave();
  g_screen = kAb;
  M5.Display.fillScreen(0);
  abweb::begin((uint32_t)emscripten_get_now());
}

void show_sw() {
  if (g_screen == kAb) abweb::leave();
  home::leave();
  fm::leave();
  am::leave();
  cb::leave();
  g_screen = kSw;
  M5.Display.fillScreen(0);
  swweb::begin((uint32_t)emscripten_get_now());
  shortwave::enter(swweb::snapshot());
}

void show_cb() {
  if (g_screen == kAb) abweb::leave();
  home::leave();
  fm::leave();
  am::leave();
  shortwave::leave();
  g_screen = kCb;
  M5.Display.fillScreen(0);
  const uint32_t now = (uint32_t)emscripten_get_now();
  cbweb::begin(now);
  cb::enter(cbweb::snapshot(now));
}

void show_am() {
  if (g_screen == kAb) abweb::leave();
  home::leave();
  fm::leave();
  cb::leave();
  shortwave::leave();
  g_screen = kAm;
  M5.Display.fillScreen(0);
  am::populate_presets(g_am_snap);
  am::enter(g_am_snap);
}

void show_fm() {
  if (g_screen == kAb) abweb::leave();
  home::leave();
  am::leave();
  cb::leave();
  shortwave::leave();
  g_screen = kFm;
  M5.Display.fillScreen(0);
  fm::enter(g_fm_snap);
  g_fm_committed = g_fm_snap;
}

// Home's "last used" list, kept in the page's storage under the key the device's NVS uses.
void load_recents() {
  uint8_t ids[dashboards::kRecentCapacity] = {};
  g_nvs.begin("orcsdr");
  const size_t n = g_nvs.get_bytes("recent", ids, sizeof ids);
  dashboards::load_recent(ids, n);
}

void note_open(dashboards::Id id) {
  if (!dashboards::record_open(id)) return;
  uint8_t ids[dashboards::kRecentCapacity] = {};
  const size_t n = dashboards::copy_recent(ids, sizeof ids);
  g_nvs.begin("orcsdr");
  g_nvs.put_bytes("recent", ids, n);
}

// Records an action for JS, or handles it here when it is only navigation between screens.
// Returns true when JS has something to do.
bool take_home(const home::Action& a) {
  if (a.kind == home::ActionKind::none) return false;
  if (a.kind == home::ActionKind::open_dashboard) {
    if (const dscreens::Screen* ds = dscreens::find(a.dashboard)) {
      note_open(a.dashboard);
      show_data(ds);
      return false;
    }
  }
  if (a.kind == home::ActionKind::open_dashboard && a.dashboard == dashboards::Id::fm) {
    note_open(a.dashboard);   // feeds Home's "last used" list, as main.cpp does on the device
    show_fm();
    return false;
  }
  if (a.kind == home::ActionKind::open_dashboard && a.dashboard == dashboards::Id::airband) {
    note_open(a.dashboard);
    show_ab();
    return false;
  }
  if (a.kind == home::ActionKind::open_dashboard && a.dashboard == dashboards::Id::shortwave) {
    note_open(a.dashboard);
    show_sw();
    return false;
  }
  if (a.kind == home::ActionKind::open_dashboard && a.dashboard == dashboards::Id::cb) {
    note_open(a.dashboard);
    show_cb();
    return false;
  }
  if (a.kind == home::ActionKind::open_dashboard && a.dashboard == dashboards::Id::am) {
    note_open(a.dashboard);
    show_am();
    return false;
  }
  if (a.kind == home::ActionKind::open_dashboard &&
      (a.dashboard == dashboards::Id::weather || a.dashboard == dashboards::Id::marine || a.dashboard == dashboards::Id::satellite)) {
    // main.cpp's draw_sdr_screen(): these bands have no screen of their own, so Home comes back tuned to the band (the page does the tuning)
    note_open(a.dashboard);
    show_home();
  }
  g_action_name = home_action_name(a.kind);
  g_action_value = a.value;
  g_action_dashboard = (int)a.dashboard;
  return true;
}

// CB keeps its state (mode, scanner, monitor, settings) in cbweb, as main.cpp does on the device; only
// navigation and the header mute reach the page as actions.
// Shortwave: its state lives in swweb. Gain/volume/hunt/export and the header buttons reach the page by name.
bool take_sw(const shortwave::Action& a, uint32_t now) {
  using K = shortwave::ActionKind;
  if (a.kind == K::none) return false;
  if (a.kind == K::exit_home) { show_home(); return false; }
  const bool forward = swweb::act(a, now);
  if (!forward) return false;
  g_action_value = static_cast<unsigned>(a.value);
  switch (a.kind) {
    case K::sound_toggle: g_action_name = "sound_toggle"; break;
    case K::volume_down: g_action_name = "volume_down"; break;
    case K::volume_up: g_action_name = "volume_up"; break;
    case K::gain_auto: g_action_name = "gain_auto"; break;
    case K::gain_tenth_db: g_action_name = "gain_tenth_db"; break;
    case K::rtl_agc: g_action_name = "rtl_agc"; break;
    case K::export_log: g_action_name = "export_log"; break;
    case K::open_settings: g_action_name = "open_device_settings"; break;
    default: return false;
  }
  return true;
}

bool take_cb(const cb::Action& a) {
  if (a.kind == cb::ActionKind::none) return false;
  if (a.kind == cb::ActionKind::exit_home) { show_home(); return false; }
  if (a.kind == cb::ActionKind::open_settings) { g_action_name = "open_device_settings"; return true; }
  cbweb::act(a, (uint32_t)emscripten_get_now());
  return false;
}

bool take_am(const am::Action& a) {
  if (a.kind == am::ActionKind::none) return false;
  if (a.kind == am::ActionKind::exit_home) {
    show_home();
    return false;
  }
  g_action_name = am_action_name(a.kind);
  g_action_value = a.value;
  return true;
}

bool take_fm(const fm::Action& a) {
  if (a.kind == fm::ActionKind::none) return false;
  if (a.kind == fm::ActionKind::exit_to_browse) {
    show_home();
    return false;
  }
  g_action_name = fm_action_name(a.kind);
  g_action_value = a.value;
  return true;
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE int m5_init() {
  if (g_started) return 1;
  M5.Display.setColorDepth(16);
  M5.Display.setPsram(false);
  if (!M5.Display.createSprite(1280, 720)) return 0;
  g_snap.battery_percent = -1;
  g_fm_snap.battery_percent = -1;
  strcpy(g_snap.receiver, "RTL-SDR");
  for (float& l : g_levels) l = -90.0f;
  g_started = true;
  load_recents();
  show_home();
  return 1;
}

EMSCRIPTEN_KEEPALIVE int m5_screen() { return g_screen; }

// ---- Home snapshot ----
EMSCRIPTEN_KEEPALIVE void m5_set_num(const char* key, double v) {
  auto& s = g_snap;
#define N(name) if (!strcmp(key, #name)) { set_int(s.name, v); return; }
  N(frequency_hz) N(tune_min_hz) N(tune_max_hz) N(requested_frequency_hz) N(span_hz) N(step_hz)
  N(step_adjustable) N(filter_bandwidth_hz) N(effective_sps) N(battery_percent) N(vbus_mv) N(volume)
  N(relative_dbfs) N(wifi_connected) N(usb_connected) N(driver_ready) N(receiving) N(sound_enabled)
  N(gain_available) N(gain_auto_available) N(gain_smart) N(gain_auto) N(rtl_agc_available) N(rtl_agc)
  N(bias_available) N(bias_on) N(gain_tenth_db) N(gain_step_count) N(mode_choice) N(mode_active)
  N(mode_suggested) N(mode_selectable) N(edges_hint)
#undef N
  if (!strcmp(key, "filter_kind")) s.filter_kind = static_cast<filter_standards::Kind>((int)v);
}

EMSCRIPTEN_KEEPALIVE void m5_set_str(const char* key, const char* v) {
  auto& s = g_snap;
#define S(name) if (!strcmp(key, #name)) { strncpy(s.name, v, sizeof(s.name) - 1); s.name[sizeof(s.name) - 1] = 0; return; }
  S(wifi_ip) S(receiver) S(mode) S(band) S(clock) S(date)
#undef S
}

EMSCRIPTEN_KEEPALIVE void m5_set_gain_step(int index, int tenth_db) {
  if (index >= 0 && index < 32) {
    g_snap.gain_steps_tenth_db[index] = (int16_t)tenth_db;
    g_fm_snap.gain_steps_tenth_db[index] = tenth_db;
  }
}

// ---- FM snapshot ----
EMSCRIPTEN_KEEPALIVE void m5_fm_set_num(const char* key, double v) {
  auto& s = g_fm_snap;
#define N(name) if (!strcmp(key, #name)) { set_int(s.name, v); return; }
  N(frequency_hz) N(step_hz) N(filter_bandwidth_hz) N(span_hz) N(effective_sps) N(target_sps)
  N(usb_overruns) N(consumer_drops) N(audio_underruns) N(dsp_percent) N(audio_ring_pressure_percent)
  N(battery_percent) N(relative_dbfs) N(left_dbfs) N(right_dbfs) N(running) N(driver_ready) N(stereo)
  N(rds_carrier) N(rds_locked) N(wifi_connected) N(sound_enabled) N(graphics_enabled) N(recording)
  N(preset_scanning) N(gain_auto) N(gain_auto_selecting) N(clipping_percent) N(gain_tenth_db)
  N(gain_step_count) N(volume) N(preset_index) N(preset_count)
#undef N
}

EMSCRIPTEN_KEEPALIVE void m5_fm_set_str(const char* key, const char* v) {
  auto& s = g_fm_snap;
#define S(name) if (!strcmp(key, #name)) { strncpy(s.name, v, sizeof(s.name) - 1); s.name[sizeof(s.name) - 1] = 0; return; }
  S(program_service) S(radio_text) S(pi_code) S(program_type) S(last_error)
#undef S
}

// ---- AM snapshot ----
EMSCRIPTEN_KEEPALIVE void m5_am_set_num(const char* key, double v) {
  auto& s = g_am_snap;
#define N(name) if (!strcmp(key, #name)) { set_int(s.name, v); return; }
  N(frequency_hz) N(step_hz) N(filter_bandwidth_hz) N(span_hz) N(relative_dbfs) N(running) N(driver_ready)
  N(sound_enabled) N(graphics_enabled) N(recording) N(volume) N(gain_available) N(rtl_agc_available)
  N(rtl_agc) N(gain_auto) N(gain_auto_selecting) N(clipping_percent) N(gain_tenth_db) N(gain_step_count)
  N(scan_active) N(scan_step) N(scan_total) N(scan_found) N(scan_frequency_hz) N(selected_preset)
  N(battery_percent)
#undef N
}

// The page keeps what the node's NVS would: saved frequency, step, presets. It hands them over as
// "key:hex" lines before the first AM screen and reads them back whenever the revision moves.
static const char* hex_digits = "0123456789abcdef";
EMSCRIPTEN_KEEPALIVE void m5_nvs_import(const char* text) {
  const char* p = text;
  while (*p) {
    const char* colon = strchr(p, ':');
    const char* end = strchr(p, '\n');
    if (!colon || (end && colon > end)) break;
    std::string key(p, colon - p);
    std::vector<uint8_t> bytes;
    const char* q = colon + 1;
    while (*q && *q != '\n' && q[1] && q[1] != '\n') {
      auto nib = [](char c) { return c >= 'a' ? c - 'a' + 10 : c - '0'; };
      bytes.push_back(static_cast<uint8_t>(nib(q[0]) * 16 + nib(q[1])));
      q += 2;
    }
    g_nvs.set_raw(key, bytes);
    if (!end) break;
    p = end + 1;
  }
  am::load(g_nvs);
  load_recents();
}
EMSCRIPTEN_KEEPALIVE const char* m5_nvs_export() {
  static std::string out;
  out.clear();
  for (const auto& kv : g_nvs.entries()) {
    out += kv.first;
    out += ':';
    for (uint8_t b : kv.second) { out += hex_digits[b >> 4]; out += hex_digits[b & 15]; }
    out += '\n';
  }
  return out.c_str();
}
EMSCRIPTEN_KEEPALIVE unsigned m5_nvs_revision() { return g_nvs.revision(); }
EMSCRIPTEN_KEEPALIVE unsigned m5_am_saved_frequency() { return am::saved_frequency(); }
EMSCRIPTEN_KEEPALIVE unsigned m5_am_tune_step() { return am::tune_step(); }
EMSCRIPTEN_KEEPALIVE unsigned m5_am_cycle_step() { return am::cycle_tune_step(); }
EMSCRIPTEN_KEEPALIVE unsigned m5_am_toggle_spacing() { return am::toggle_scan_spacing(); }
EMSCRIPTEN_KEEPALIVE unsigned m5_am_preset(int i) { return am::preset((size_t)i); }
EMSCRIPTEN_KEEPALIVE void m5_am_note_tuned(unsigned hz) { am::note_tuned(hz); }
EMSCRIPTEN_KEEPALIVE void m5_am_save_preset() { am::save_current_preset(); }
EMSCRIPTEN_KEEPALIVE int m5_am_replace_preset(int i) { return am::replace_preset((size_t)i) ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE int m5_am_delete_preset(int i) { return am::delete_preset((size_t)i) ? 1 : 0; }

// ---- Airband: the runtime keeps its own state; the page feeds it inputs and applies what it asks for ----
EMSCRIPTEN_KEEPALIVE void m5_ab_set_num(const char* key, double v) { abweb::set_num(key, v); }
EMSCRIPTEN_KEEPALIVE void m5_ab_set_gain_step(int i, int tenth) { abweb::set_gain_step(i, tenth); }
EMSCRIPTEN_KEEPALIVE unsigned m5_ab_freq() { return abweb::frequency_hz(); }
EMSCRIPTEN_KEEPALIVE unsigned m5_ab_filter() { return abweb::filter_hz(); }
EMSCRIPTEN_KEEPALIVE int m5_ab_audio_open() { return abweb::audio_open() ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE unsigned m5_ab_span() { return abweb::scope_span_hz(); }
EMSCRIPTEN_KEEPALIVE unsigned m5_ab_take_tune() { return abweb::take_tune(); }
static int g_ab_gain_value = 0;
EMSCRIPTEN_KEEPALIVE int m5_ab_take_gain_kind() { int kind = 0; if (!abweb::take_gain(&kind, &g_ab_gain_value)) return 0; return kind; }
EMSCRIPTEN_KEEPALIVE int m5_ab_gain_value() { return g_ab_gain_value; }
EMSCRIPTEN_KEEPALIVE void m5_ab_reload() { abweb::reload_catalog((uint32_t)emscripten_get_now()); }
// Voice band-pass (300 Hz - 3.2 kHz) on 48 kHz floats in place.
EMSCRIPTEN_KEEPALIVE void m5_ab_audio(float* audio, int n) {
  static int16_t block[4096];
  for (int off = 0; off < n; off += 4096) {
    const int m = n - off < 4096 ? n - off : 4096;
    for (int i = 0; i < m; ++i) block[i] = (int16_t)(audio[off + i] * 30000.0f);
    airband_audio::process(block, (size_t)m);
    for (int i = 0; i < m; ++i) audio[off + i] = block[i] / 30000.0f;
  }
}
EMSCRIPTEN_KEEPALIVE void m5_ab_audio_reset() { airband_audio::reset(); }
// Writes a file into the in-memory store (used to hand the aviation catalog to the runtime).
EMSCRIPTEN_KEEPALIVE int m5_fs_write(const char* path, const char* data, int n) {
  File f = storage::filesystem().open(path, FILE_WRITE, true);
  if (!f) return 0;
  f.write(reinterpret_cast<const uint8_t*>(data), (size_t)n);
  f.close();
  return 1;
}

// ---- Shortwave ----
EMSCRIPTEN_KEEPALIVE void m5_sw_set_num(const char* key, double v) { swweb::set_num(key, v); }
EMSCRIPTEN_KEEPALIVE void m5_sw_set_str(const char* key, const char* v) { swweb::set_str(key, v); }
EMSCRIPTEN_KEEPALIVE void m5_sw_set_gain_step(int i, int tenth) { swweb::set_gain_step(i, tenth); }
EMSCRIPTEN_KEEPALIVE unsigned m5_sw_freq() { return swweb::frequency_hz(); }
EMSCRIPTEN_KEEPALIVE int m5_sw_mode() { return swweb::mode(); }
EMSCRIPTEN_KEEPALIVE int m5_sw_clarifier() { return swweb::clarifier_hz(); }
EMSCRIPTEN_KEEPALIVE unsigned m5_sw_filter() { return swweb::filter_hz(); }
EMSCRIPTEN_KEEPALIVE unsigned m5_sw_span() { return swweb::span_hz(); }
EMSCRIPTEN_KEEPALIVE int m5_sw_boost() { return swweb::audio_boost() ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE unsigned m5_sw_take_tune() { return swweb::take_tune(); }
EMSCRIPTEN_KEEPALIVE int m5_sw_hunt_requested() { return swweb::hunt_requested_band(); }
EMSCRIPTEN_KEEPALIVE unsigned m5_sw_band_min(int i) { unsigned lo = 0, hi = 0; return swweb::hunt_band((size_t)i, &lo, &hi) ? lo : 0; }
EMSCRIPTEN_KEEPALIVE unsigned m5_sw_band_max(int i) { unsigned lo = 0, hi = 0; return swweb::hunt_band((size_t)i, &lo, &hi) ? hi : 0; }
EMSCRIPTEN_KEEPALIVE float* m5_sw_hunt_bins() { return g_sw_hunt_bins; }
EMSCRIPTEN_KEEPALIVE void m5_sw_hunt_finish(unsigned sps, unsigned center_hz) { swweb::hunt_finish(g_sw_hunt_bins, 512, sps, center_hz); }
// Audio DSP (noise reduction, notch, squelch) on 48 kHz floats in place.
EMSCRIPTEN_KEEPALIVE void m5_sw_dsp(float* audio, int n, float rf_dbfs) {
  static int16_t block[4096];
  for (int off = 0; off < n; off += 4096) {
    const int m = n - off < 4096 ? n - off : 4096;
    for (int i = 0; i < m; ++i) block[i] = (int16_t)(audio[off + i] * 30000.0f);
    shortwave::audio_dsp::process(block, (size_t)m, rf_dbfs);
    for (int i = 0; i < m; ++i) audio[off + i] = block[i] / 30000.0f;
  }
}
EMSCRIPTEN_KEEPALIVE int m5_sw_squelch_open() { return shortwave::audio_dsp::metrics().squelch_open ? 1 : 0; }
// The in-memory file store behind the library (memories, logbook, exports): the page persists it.
EMSCRIPTEN_KEEPALIVE void m5_fs_import(const char* text) { storage::import_all(text); }
EMSCRIPTEN_KEEPALIVE const char* m5_fs_export() { static std::string out; out = storage::export_all(); return out.c_str(); }
EMSCRIPTEN_KEEPALIVE unsigned m5_fs_revision() { return storage::revision(); }
EMSCRIPTEN_KEEPALIVE const char* m5_fs_list(const char* prefix) { static std::string out; out = storage::list_with_prefix(prefix); return out.c_str(); }
EMSCRIPTEN_KEEPALIVE const char* m5_fs_read(const char* path) { static std::string out; out = storage::read_file(path); return out.c_str(); }
EMSCRIPTEN_KEEPALIVE void m5_sw_load() { swweb::load(g_nvs); }

// ---- CB: the page hands over the receiver window's spectrum and the channel signal; the dashboard state lives here ----
EMSCRIPTEN_KEEPALIVE float* m5_cb_bins() { return g_cb_bins; }
EMSCRIPTEN_KEEPALIVE void m5_cb_signal(float dbfs, int squelch_open, int running, int sound) {
  cbweb::set_signal(dbfs, squelch_open != 0, running != 0, sound != 0);
}
EMSCRIPTEN_KEEPALIVE void m5_cb_observe(unsigned sps, unsigned center_hz) {
  g_cb_sps = sps;
  g_cb_center = center_hz;
  cbweb::observe((uint32_t)emscripten_get_now(), g_cb_bins, 512, sps, center_hz);
}
EMSCRIPTEN_KEEPALIVE unsigned m5_cb_freq() { return cbweb::frequency_hz(); }
EMSCRIPTEN_KEEPALIVE int m5_cb_mode() { return cbweb::mode(); }
EMSCRIPTEN_KEEPALIVE int m5_cb_clarifier() { return cbweb::clarifier_hz(); }
EMSCRIPTEN_KEEPALIVE int m5_cb_squelch() { return cbweb::squelch_dbfs(); }
EMSCRIPTEN_KEEPALIVE unsigned m5_cb_take_tune() { return cbweb::take_tune(); }
EMSCRIPTEN_KEEPALIVE int m5_cb_scanning() { return cbweb::scanning() ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE void m5_cb_load() { cbweb::load(g_nvs); }

// Home only redraws the parts whose revision counter moved, so derive the counters from the diff.
EMSCRIPTEN_KEEPALIVE void m5_commit() {
  if (!g_started) return;
  if (g_screen == kData && g_ds) {
    g_ds->update((uint32_t)emscripten_get_now());
    return;
  }
  if (g_screen == kAb) {
    const uint32_t now = (uint32_t)emscripten_get_now();
    abweb::service(now);
    abweb::update(now);
    if (abweb::exit_requested()) show_home();
    return;
  }
  if (g_screen == kSw) {
    shortwave::update(swweb::snapshot());
    return;
  }
  if (g_screen == kCb) {
    cb::update(cbweb::snapshot((uint32_t)emscripten_get_now()));
    return;
  }
  if (g_screen == kAm) {
    am::populate_presets(g_am_snap);
    am::update(g_am_snap);
    return;
  }
  if (g_screen == kFm) {
    fm::update(g_fm_snap);
    g_fm_committed = g_fm_snap;
    return;
  }
  home::Snapshot& n = g_snap;
  const home::Snapshot& o = g_committed;
  n.revision = o.revision;
  n.tuner_revision = o.tuner_revision;
  n.audio_revision = o.audio_revision;
  n.status_revision = o.status_revision;
  if (n.frequency_hz != o.frequency_hz || strcmp(n.mode, o.mode) || strcmp(n.band, o.band) ||
      n.requested_frequency_hz != o.requested_frequency_hz || n.mode_choice != o.mode_choice ||
      n.mode_active != o.mode_active)
    ++n.tuner_revision;
  if (n.volume != o.volume || n.sound_enabled != o.sound_enabled || n.step_hz != o.step_hz)
    ++n.audio_revision;
  if (n.wifi_connected != o.wifi_connected || n.usb_connected != o.usb_connected ||
      n.battery_percent != o.battery_percent || strcmp(n.clock, o.clock) || strcmp(n.date, o.date) ||
      strcmp(n.wifi_ip, o.wifi_ip) || n.sound_enabled != o.sound_enabled)
    ++n.status_revision;
  home::Snapshot probe = n;
  probe.revision = o.revision;
  if (memcmp(&probe, &o, sizeof(probe)) == 0) return;
  ++n.revision;
  home::update(n);
  g_committed = n;
}

EMSCRIPTEN_KEEPALIVE float* m5_levels() { return g_levels; }

EMSCRIPTEN_KEEPALIVE void m5_draw_spectrum(float floor_db, int stressed) {
  if (!g_started) return;
  if (g_screen == kData) {
    if (g_ds && g_ds->spectrum) g_ds->spectrum(g_levels, 512, floor_db);
    return;
  }
  if (g_screen == kAb) {
    if (airband::spectrum_active()) airband::draw_spectrum(g_levels, 0, 512, abweb::scope_span_hz());
    return;
  }
  if (g_screen == kSw) {
    if (shortwave::spectrum_active()) shortwave::draw_spectrum(g_levels, 0, 512, floor_db);
    return;
  }
  if (g_screen == kCb) {
    if (cb::spectrum_active()) cb::draw_spectrum(g_cb_bins, 512, g_cb_sps, g_cb_center);
    return;
  }
  if (g_screen == kAm) {
    if (am::spectrum_active()) am::draw_spectrum(g_levels, 0, 512, floor_db);
    return;
  }
  if (g_screen == kFm) {
    if (fm::spectrum_active()) fm::draw_spectrum(g_levels, 0, 512, floor_db);
    return;
  }
  home::draw_spectrum(g_levels, 0, 512, floor_db, stressed != 0);
}

// Touch: pressed=1 on pointer down/drag, 0 on release. Returns 1 when JS has an action to perform.
EMSCRIPTEN_KEEPALIVE int m5_touch(int x, int y, int pressed) {
  if (!g_started) return 0;
  clear_action();
  if (!pressed && viz_button_applies() && audio_header::visualizer_hit(x, y)) { open_viz(); return 0; }
  if (!pressed && viz_button_applies() && audio_header::settings_hit(x, y)) { open_overlay(&dscreens::kSettings); return 0; }
  if (g_screen == kData && g_ds) {
    const dscreens::Result r = g_ds->touch(x, y, pressed != 0, (uint32_t)emscripten_get_now());
    if (r.exit) { if (is_overlay(g_ds)) close_overlay(); else show_home(); return 0; }
    if (r.action) { g_action_name = r.action; return 1; }
    return 0;
  }
  if (g_screen == kAb) {
    const uint32_t now = (uint32_t)emscripten_get_now();
    if (pressed) { g_ab_scope_drag = abweb::scope_edge_hit(x, y); if (g_ab_scope_drag) abweb::scope_drag(x, now); return 0; }
    if (g_ab_scope_drag) { g_ab_scope_drag = false; return 0; }
    if (audio_header::home_hit(x, y)) { show_home(); return 0; }
    if (audio_header::mute_hit(x, y)) { g_action_name = "sound_toggle"; return 1; }
    abweb::touch(x, y, now);
    return 0;
  }
  if (g_screen == kSw) {
    const uint32_t now = (uint32_t)emscripten_get_now();
    // Same order as OrcSDR main.cpp: filter and gain sliders first, a plain tap on release.
    if (take_sw(shortwave::handle_filter_drag(x, y, pressed != 0), now)) return 1;
    if (pressed) {
      g_am_drag_used = false;
      const auto ga = shortwave::handle_gain_drag(x, y);
      if (ga.kind != shortwave::ActionKind::none) { g_am_drag_used = true; return take_sw(ga, now) ? 1 : 0; }
      return 0;
    }
    if (g_am_drag_used) { g_am_drag_used = false; return 0; }
    if (!text_editor::active() && audio_header::home_hit(x, y)) { show_home(); return 0; }
    if (!text_editor::active() && audio_header::mute_hit(x, y)) { g_action_name = "sound_toggle"; return 1; }
    return take_sw(shortwave::handle_touch(x, y), now) ? 1 : 0;
  }
  if (g_screen == kCb) {
    if (!pressed) return 0;   // taps act on press
    if (audio_header::home_hit(x, y)) { show_home(); return 0; }
    if (audio_header::mute_hit(x, y)) { g_action_name = "sound_toggle"; return 1; }
    return take_cb(cb::handle_touch(x, y)) ? 1 : 0;
  }
  if (g_screen == kAm) {
    // Same order as OrcSDR's main.cpp: presets (long press), bandwidth and gain sliders on press; a plain tap on release.
    const uint32_t now = (uint32_t)emscripten_get_now();
    const auto preset = am::handle_preset_touch(x, y, pressed != 0, now);
    if (preset.consumed) return take_am(preset.action) ? 1 : 0;
    if (pressed) {
      g_am_drag_used = false;
      if (take_am(am::handle_bandwidth_drag(x, y)) || take_am(am::handle_gain_drag(x, y))) { g_am_drag_used = true; return 1; }
      return 0;
    }
    if (g_am_drag_used) { g_am_drag_used = false; return 0; }
    if (audio_header::home_hit(x, y)) { show_home(); return 0; }
    if (audio_header::mute_hit(x, y)) { g_action_name = "sound_toggle"; return 1; }
    return take_am(am::handle_touch(x, y)) ? 1 : 0;
  }
  if (g_screen == kFm) {
    if (!pressed) return 0;   // the FM screens act on press
    // The shared header buttons belong to main.cpp on the device, so they are handled here.
    if (audio_header::home_hit(x, y)) { show_home(); return 0; }
    if (audio_header::mute_hit(x, y)) {
      g_action_name = "sound_toggle";
      return 1;
    }
    return take_fm(fm::handle_touch(x, y)) ? 1 : 0;
  }
  return take_home(home::handle_touch(x, y, pressed != 0)) ? 1 : 0;
}

// Drag while the pointer is down. Only the FM gain slider cares.
EMSCRIPTEN_KEEPALIVE int m5_drag(int x, int y) {
  if (!g_started || g_screen == kHome || g_screen == kCb || g_screen == kData) return 0;
  clear_action();
  if (g_screen == kAb) { if (g_ab_scope_drag) abweb::scope_drag(x, (uint32_t)emscripten_get_now()); return 0; }
  if (g_screen == kSw) {
    const uint32_t now = (uint32_t)emscripten_get_now();
    if (take_sw(shortwave::handle_filter_drag(x, y, true), now)) { g_am_drag_used = true; return 1; }
    const auto ga = shortwave::handle_gain_drag(x, y);
    if (ga.kind != shortwave::ActionKind::none) { g_am_drag_used = true; return take_sw(ga, now) ? 1 : 0; }
    return 0;
  }
  if (g_screen == kAm) {
    if (take_am(am::handle_bandwidth_drag(x, y)) || take_am(am::handle_gain_drag(x, y))) { g_am_drag_used = true; return 1; }
    return 0;
  }
  return take_fm(fm::handle_gain_drag(x, y)) ? 1 : 0;
}

// Physical keyboard: when the numpad is up, keys press the on-screen keys (same path as a touch).
EMSCRIPTEN_KEEPALIVE int m5_key(int ch) {
  if (!g_started) return 0;
  clear_action();
  if (g_screen == kSw && text_editor::active()) {   // memory label / notes / antenna text entry
    keyboard_input::Key key;
    if (ch == 13) key.special = keyboard_input::Special::enter;
    else if (ch == 27) key.special = keyboard_input::Special::escape;
    else if (ch == 8) key.special = keyboard_input::Special::backspace;
    else if (ch >= 32 && ch < 127) key.ch = (char)ch;
    else return 0;
    const auto act = text_editor::handle_key(key);
    int x = 0, y = 0;
    if (act == keyboard_input::EditAction::accept) { text_editor::accept_point(&x, &y); take_sw(shortwave::handle_touch(x, y), (uint32_t)emscripten_get_now()); }
    else if (act == keyboard_input::EditAction::cancel) { text_editor::cancel_point(&x, &y); take_sw(shortwave::handle_touch(x, y), (uint32_t)emscripten_get_now()); }
    return 0;
  }
  const bool pad = g_screen == kSw ? shortwave::keypad_open() : g_screen == kAm ? am::keypad_open() : g_screen == kFm ? fm::keypad_open() : home::keypad_open();
  int x = 0, y = 0;
  bool got = false;
  if (pad) {
    bool ok = false;
    if (ch == 13) { freq_keypad::tune_point(&x, &y); ok = true; }
    else if (ch == 27) { freq_keypad::cancel_point(&x, &y); ok = true; }
    else if (ch == 8) ok = freq_keypad::key_point('\b', &x, &y);
    else if ((ch >= '0' && ch <= '9') || ch == '.') ok = freq_keypad::key_point((char)ch, &x, &y);
    if (ok) {
      if (g_screen == kSw) got = take_sw(shortwave::handle_touch(x, y), (uint32_t)emscripten_get_now());
      else if (g_screen == kAm) got = take_am(am::handle_touch(x, y));
      else if (g_screen == kFm) got = take_fm(fm::handle_touch(x, y));
      else {
        got = take_home(home::handle_touch(x, y, true));
        got = take_home(home::handle_touch(x, y, false)) || got;
      }
    }
  } else if (ch == 27 && g_screen == kHome && home::popup_open()) {
    home::close_popup();
  } else if (ch == 27 && g_screen != kHome) {
    show_home();
  }
  return got ? 1 : 0;
}

// ---- decoder dashboards (data screens) ----
EMSCRIPTEN_KEEPALIVE const char* m5_ds_decoder_left() { return g_ds_left ? g_ds_left->decoder : ""; }
EMSCRIPTEN_KEEPALIVE const char* m5_ds_decoder() { return g_screen == kData && g_ds ? g_ds->decoder : ""; }
EMSCRIPTEN_KEEPALIVE void m5_ds_event(const char* line) { if (g_screen == kData && g_ds) g_ds->event(line, (uint32_t)emscripten_get_now()); }
EMSCRIPTEN_KEEPALIVE void m5_ds_set_num(const char* key, double v) { if (g_screen == kData && g_ds) g_ds->set_num(key, v); }
// Settings a screen should start with, handed over before it is first shown (the page's localStorage stands in for the device's NVS).
EMSCRIPTEN_KEEPALIVE void m5_ds_audio(const float* samples, int n) { if (g_screen == kData && g_ds && g_ds->audio && n > 0) g_ds->audio(samples, (size_t)n); }
EMSCRIPTEN_KEEPALIVE void m5_ds_iq(const uint8_t* data, int n) { if (g_screen == kData && g_ds && g_ds->iq && n > 0) g_ds->iq(data, (size_t)n); }
EMSCRIPTEN_KEEPALIVE void m5_ds_text(const char* decoder, const char* key, const char* value) { if (const dscreens::Screen* s = dscreens::find_decoder(decoder)) if (s->text) s->text(key, value); }
EMSCRIPTEN_KEEPALIVE void m5_ds_pre(const char* decoder, const char* key, double v) { if (const dscreens::Screen* s = dscreens::find_decoder(decoder)) s->set_num(key, v); }
EMSCRIPTEN_KEEPALIVE const char* m5_ds_request() {
  static std::string out;
  const char* r = g_screen == kData && g_ds ? g_ds->take_request() : g_ds_left ? g_ds_left->take_request() : nullptr;
  if (!r && g_screen != kData) g_ds_left = nullptr;
  out = r ? r : "";
  return out.c_str();
}

EMSCRIPTEN_KEEPALIVE const char* m5_action_name() { return g_action_name; }
EMSCRIPTEN_KEEPALIVE unsigned m5_action_value() { return g_action_value; }
EMSCRIPTEN_KEEPALIVE void m5_open_settings() { if (g_started && !(g_screen == kData && is_overlay(g_ds))) open_overlay(&dscreens::kSettings); }
EMSCRIPTEN_KEEPALIVE int m5_action_dashboard() { return g_action_dashboard; }
EMSCRIPTEN_KEEPALIVE const char* m5_dashboard_title(int id) {
  const dashboards::Descriptor* d = dashboards::find((dashboards::Id)id);
  return d ? d->title : "";
}
EMSCRIPTEN_KEEPALIVE unsigned m5_step_span(unsigned span, int dir) { return home::step_span(span, dir); }
EMSCRIPTEN_KEEPALIVE int m5_popup_open() { return home::popup_open() ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE int m5_keypad_open() { return (g_screen == kSw ? shortwave::keypad_open() : g_screen == kAm ? am::keypad_open() : g_screen == kFm ? fm::keypad_open() : home::keypad_open()) ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE int m5_browser_active() { return home::browser_active() ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE void* m5_fb() { return M5.Display.getBuffer(); }

}  // extern "C"
