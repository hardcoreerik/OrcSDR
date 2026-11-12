// LORA / MESH screen: OrcSDR's LoRa and Meshtastic receive tools (live traffic, nodes, traffic table, map, RF health, channel picker)
// driven by the node's own LoRa decoder. The part of main.cpp that owns this screen's state is this file: the packet ring with its
// de-duplication, the per-node history, the channel plan (region and slot of the Meshtastic channel hash), and what the screen's actions
// ask of the radio. The node decodes (960 kS/s IQ at the channel, preamble-triggered captures; `docs/ui/PHASE5_DECODERS.md`); this file
// shows what it decoded.
//
// Not here: the band survey (it retunes the receiver and reads its level, and a node session has no level meter to read), IQ recording
// and the SD packet log (no card), the receive-side signal level per packet (the decoder reports the capture's relative power, which
// this screen shows as the signal figure; the SNR is unknown and the screen shows a dash).
#include <M5Unified.h>
#include <emscripten.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "data_screens.hpp"
#include "lora_channel_control.hpp"
#include "lora_dashboard.hpp"
#include "lora_history.hpp"
#include "lora_packet_log.hpp"
#include "nvs_store.hpp"

namespace {

using namespace orcsdr;
using namespace dscreens;

constexpr size_t kRing = 16;

NvsStore g_nvs;
lora::Snapshot g_snap;
lora_history::Store g_history;
lora_log::Record g_ring[kRing];
size_t g_ring_count = 0;
bool g_entered = false, g_receiving = false;
uint32_t g_frames = 0, g_crc_ok = 0, g_encrypted = 0, g_revision = 0;
uint32_t g_favorite = 0;
uint8_t g_selected = 0;
double g_utc_at_ms = 0;
uint32_t g_utc = 0;
float g_rf_dbfs = -90.0f;
char g_request[160];
bool g_request_pending = false;
const char* g_action = nullptr;
uint32_t g_last_push = 0;

uint32_t utc_now() {
  return g_utc ? g_utc + (uint32_t)(std::max(0.0, emscripten_get_now() - g_utc_at_ms) / 1000.0) : 0;
}

void ask_start() {
  const lora_channel::Selection& sel = lora_channel::selection();
  snprintf(g_request, sizeof g_request, "{\"op\":\"start\",\"hz\":%u,\"options\":\"sf=11\"}", (unsigned)sel.frequency_hz);
  g_request_pending = true;
}

void choose(size_t region, uint16_t slot) {
  g_nvs.begin("orcsdr");
  if (!lora_channel::choose(region, slot, g_nvs)) return;
  g_history.clear();   // nodes heard on another channel are another mesh
  g_ring_count = 0;
  ++g_revision;
  ask_start();
}

void build_snapshot(uint32_t now) {
  lora::Snapshot s{};
  const lora_channel::Selection& sel = lora_channel::selection();
  s.frequency_hz = sel.frequency_hz;
  s.span_hz = 960000;
  s.sf = 11;
  s.region_index = sel.region_index;
  s.region_count = (uint8_t)lora_channel::region_count();
  s.channel_slot = lora_channel::slot_for_frequency(sel.region_index, sel.frequency_hz);
  s.channel_count = lora_channel::slot_count(sel.region_index);
  s.default_slot = lora_channel::default_slot(sel.region_index);
  s.bandwidth_hz = 250000;
  s.noise_dbfs = -90.0f;
  s.trigger_dbfs = -75.0f;
  s.relative_dbfs = g_rf_dbfs;
  s.decoded_frames = g_frames;
  s.crc_ok = g_crc_ok;
  s.encrypted_frames = g_encrypted;
  s.uptime_seconds = now / 1000u;
  s.running = g_receiving;
  s.driver_ready = true;
  s.wifi_connected = true;
  s.sound_enabled = false;
  s.sd_logging = false;
  s.native_decoder_ready = true;
  s.key_loaded = false;
  s.effective_sps = g_receiving ? 960000 : 0;
  s.target_sps = 960000;
  snprintf(s.profile, sizeof s.profile, "LONG FAST");
  snprintf(s.region, sizeof s.region, "%s", lora_channel::region(sel.region_index).code);
  snprintf(s.log_status, sizeof s.log_status, "NO SD CARD IN A BROWSER");
  for (size_t i = 0; i < g_history.count() && s.node_count < lora::kNodeCapacity; ++i) {
    const lora_history::Node* n = g_history.at(i);
    lora::Node& node = s.nodes[s.node_count++];
    node.id = n->id;
    node.seen_ms = n->last_ms;
    node.latitude_e7 = n->latitude_e7;
    node.longitude_e7 = n->longitude_e7;
    node.signal_tenths = n->signal_tenths;
    node.snr_tenths = n->snr_tenths;
    node.favorite = n->id == g_favorite;
    snprintf(node.name, sizeof node.name, "%s", n->short_name[0] ? n->short_name : n->long_name);
  }
  for (size_t i = 0; i < g_ring_count && s.event_count < lora::kEventCapacity; ++i) {
    const lora_log::Record& r = g_ring[i];
    lora::Event& e = s.events[s.event_count++];
    e.sender = r.sender;
    e.destination = r.destination;
    e.packet_id = r.packet_id;
    e.received_ms = r.received_ms;
    e.received_utc = r.received_utc;
    e.latitude_e7 = r.latitude_e7;
    e.longitude_e7 = r.longitude_e7;
    e.signal_tenths = r.signal_tenths;
    e.snr_tenths = r.snr_tenths;
    e.port = r.port;
    e.encrypted = r.encrypted;
    e.verified = r.sender != 0;
    snprintf(e.text, sizeof e.text, "%s", r.text);
  }
  s.selected_node = std::min<uint8_t>(g_selected, s.node_count ? (uint8_t)(s.node_count - 1) : 0);
  s.revision = s.decoded_frames ^ ((uint32_t)s.event_count << 24) ^ ((uint32_t)s.node_count << 16) ^ ((uint32_t)s.selected_node << 8) ^ g_favorite ^
               g_revision ^ (g_receiving ? 1u : 0u);
  g_snap = s;
}

void enter(uint32_t now) {
  g_nvs.begin("orcsdr");
  lora_channel::load(g_nvs);
  g_receiving = false;
  g_rf_dbfs = -90.0f;
  build_snapshot(now);
  M5.Display.fillScreen(0);
  lora::enter(g_snap);
  g_entered = true;
  ask_start();
}

void leave() {
  if (!g_entered) return;
  lora::leave();
  g_entered = false;
}

void update(uint32_t now) {
  if (!g_entered) return;
  if (now - g_last_push < 250) return;
  g_last_push = now;
  build_snapshot(now);
  lora::update(g_snap);
}

void note(const char* message) {
  char text[200];
  snprintf(text, sizeof text, "{\"op\":\"toast\",\"text\":\"%s\"}", message);
  snprintf(g_request, sizeof g_request, "%s", text);
  g_request_pending = true;
}

void handle(const lora::Action& a) {
  using K = lora::ActionKind;
  const lora_channel::Selection& sel = lora_channel::selection();
  switch (a.kind) {
    case K::select_view:
      if (a.value < (uint32_t)lora::View::count) lora::show_documentation_view((lora::View)a.value, g_snap);
      break;
    case K::select_node:
      if (a.value < lora::kNodeCapacity) g_selected = (uint8_t)a.value;
      break;
    case K::toggle_favorite:
      if (g_snap.node_count) {
        const uint32_t id = g_snap.nodes[g_snap.selected_node].id;
        g_favorite = g_favorite == id ? 0 : id;
        ++g_revision;
      }
      break;
    case K::filter_next: lora::toggle_filter(); break;
    case K::toggle_packet_details: lora::toggle_packet_details(); break;
    case K::center_map: lora::center_on_selected(); break;
    case K::follow_node: lora::toggle_follow_node(); break;
    case K::open_channels: lora::open_channel_picker(); break;
    case K::clear_events: g_ring_count = 0; ++g_revision; break;
    case K::scan_toggle: note("The band survey needs a level meter on the receiver; the node's decode session has none."); break;
    case K::record_iq_toggle: note("IQ recording needs an SD card."); break;
    case K::logging_toggle: case K::export_log: note("The packet log needs an SD card; the node keeps the decoded packets in its event history."); break;
    case K::region_previous: choose((sel.region_index + lora_channel::region_count() - 1) % lora_channel::region_count(), 0); break;
    case K::region_next: choose((sel.region_index + 1) % lora_channel::region_count(), 0); break;
    case K::region_select:
      if (a.value >= 1 && a.value <= lora_channel::region_count()) choose(a.value - 1, 0);
      break;
    case K::channel_previous: choose(sel.region_index, sel.slot <= 1 ? lora_channel::slot_count(sel.region_index) : sel.slot - 1); break;
    case K::channel_next: choose(sel.region_index, sel.slot >= lora_channel::slot_count(sel.region_index) ? 1 : sel.slot + 1); break;
    case K::channel_select: choose(sel.region_index, (uint16_t)a.value); break;
    case K::open_settings: g_action = "open_device_settings"; break;
    default: break;
  }
  ++g_revision;
}

Result touch(int x, int y, bool pressed, uint32_t) {
  Result r;
  if (!g_entered || !pressed) return r;
  g_action = nullptr;
  const lora::Action a = lora::handle_touch(x, y);
  if (a.kind == lora::ActionKind::exit_home) { r.exit = true; return r; }
  if (a.kind != lora::ActionKind::none) handle(a);
  r.action = g_action;
  return r;
}

// the decoder's events: "packet" and "capture"; "stats" is its running totals
void event(const char* line, uint32_t now) {
  char type[16];
  str(line, "type", type, sizeof type);
  if (!strcmp(type, "capture")) {
    g_crc_ok += (uint32_t)num(line, "crc_ok");
    return;
  }
  if (!strcmp(type, "stats")) {
    g_encrypted = std::max(g_encrypted, (uint32_t)num(line, "encrypted"));
    return;
  }
  if (strcmp(type, "packet")) return;
  lora_log::Record rec{};
  str(line, "text", rec.text, sizeof rec.text);
  rec.sender = (uint32_t)num(line, "sender");
  rec.destination = (uint32_t)num(line, "destination");
  rec.packet_id = (uint32_t)num(line, "packet_id");
  rec.received_ms = now ? now : 1;
  rec.received_utc = utc_now();
  rec.latitude_e7 = field(line, "lat_e7") ? (int32_t)num(line, "lat_e7") : INT32_MAX;
  rec.longitude_e7 = field(line, "lon_e7") ? (int32_t)num(line, "lon_e7") : INT32_MAX;
  rec.snr_tenths = INT16_MAX;
  rec.signal_tenths = (int16_t)std::lround(num(line, "level_dbfs", -90) * 10.0);
  rec.port = (uint16_t)num(line, "port");
  rec.frequency_hz = (uint32_t)num(line, "frequency_hz");
  rec.encrypted = flag(line, "encrypted");
  str(line, "short_name", rec.short_name, sizeof rec.short_name);
  str(line, "long_name", rec.long_name, sizeof rec.long_name);
  g_history.observe(rec);
  ++g_frames;
  // the same packet heard twice is shown once; a node's newest position replaces its older one (as main.cpp's ring does)
  if (rec.packet_id)
    for (size_t i = 0; i < g_ring_count; ++i)
      if (g_ring[i].sender == rec.sender && g_ring[i].packet_id == rec.packet_id) return;
  size_t replace = kRing;
  if (rec.port == 3)
    for (size_t i = 0; i < g_ring_count; ++i)
      if (g_ring[i].sender == rec.sender && g_ring[i].port == 3) { replace = i; break; }
  const size_t shift = replace < kRing ? replace : std::min(g_ring_count, kRing - 1);
  memmove(&g_ring[1], &g_ring[0], sizeof(g_ring[0]) * shift);
  g_ring[0] = rec;
  if (replace >= kRing && g_ring_count < kRing) ++g_ring_count;
  ++g_revision;
}

void set_num(const char* key, double v) {
  if (!strcmp(key, "receiving")) g_receiving = v != 0;
  else if (!strcmp(key, "utc")) { g_utc = (uint32_t)v; g_utc_at_ms = emscripten_get_now(); }
  else if (!strcmp(key, "rf_dbfs")) g_rf_dbfs = (float)v;
}

const char* take_request() {
  if (!g_request_pending) return nullptr;
  g_request_pending = false;
  return g_request;
}

void spectrum(const float* levels, int n, float floor_db) {
  if (!g_entered || !lora::spectrum_active() || n < 64) return;
  // the node's spectrum covers the receiver's 960 kHz; the channel is the middle 250 kHz of it
  const size_t visible = std::min<size_t>((size_t)n * 250000 / 960000, (size_t)n);
  lora::draw_spectrum(levels, ((size_t)n - visible) / 2, visible, floor_db);
}

}  // namespace

namespace dscreens {
extern const Screen kLora = {dashboards::Id::lora, "lora", enter, leave, update, touch, event, set_num, take_request, spectrum, nullptr, nullptr, nullptr};
}
