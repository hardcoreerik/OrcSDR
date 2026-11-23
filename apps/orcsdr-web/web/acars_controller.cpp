// ACARS screen: OrcSDR's acars_app (model, controller, dashboard, archive) without the on-board receiver. The node decodes; the
// page forwards its message events and statistics, which are posted into the same Model the device's decoder callback posts into.
#include <M5Unified.h>
#include <emscripten.h>

#include <cstdio>
#include <cstring>
#include <new>

#include "acars_archive.hpp"
#include "acars_dashboard.hpp"
#include "acars_dashboard_model.hpp"
#include "acars_dashboard_ui.hpp"
#include "dashboard_audio_control.hpp"
#include "data_screens.hpp"
#include "nvs_store.hpp"

namespace {

using namespace orcsdr;
using namespace dscreens;

struct State {
  acars_ui::Model model;
  acars_ui::Controller ui;
  acars_archive::Store archive;
  acars_archive::Logger logger{&archive};
};

State* g_state = nullptr;
NvsStore g_nvs;
audio_header::Control g_header;
bool g_receiving = false, g_started = false;
uint32_t g_utc = 0, g_synced = 0;
char g_request[120];
bool g_request_pending = false;

void header_controls() {
  audio_header::draw(g_header, 40, true, -1);
  audio_header::draw_home_button();
  audio_header::draw_settings_button();
  audio_header::draw_mute_button(true);
  audio_header::draw_visualizer_button(true);
}

void request_start(uint32_t hz) {
  snprintf(g_request, sizeof g_request, "{\"op\":\"start\",\"hz\":%u,\"options\":\"\"}", (unsigned)hz);
  g_request_pending = true;
}

uint32_t wanted_hz() {
  const acars_ui::Snapshot& s = g_state->model.snapshot();
  return s.requested_hz ? s.requested_hz : acars_ui::preset(s.prefs.region, s.prefs.preset).frequency_hz;
}

void enter(uint32_t now) {
  if (!g_state) {
    g_state = new (std::nothrow) State();
    if (!g_state) return;
  }
  g_nvs.begin("orcsdr");
  acars_ui::Store store;
  store.get_bool = [](const char* key, bool fallback) { return g_nvs.get_bool(key, fallback); };
  store.put_bool = [](const char* key, bool value) { g_nvs.put_bool(key, value); };
  store.get_u8 = [](const char* key, uint8_t fallback) { return g_nvs.get_u8(key, fallback); };
  store.put_u8 = [](const char* key, uint8_t value) { g_nvs.put_u8(key, value); };
  store.get_u32 = [](const char* key, uint32_t fallback) { return g_nvs.get_u32(key, fallback); };
  store.put_u32 = [](const char* key, uint32_t value) { g_nvs.put_u32(key, value); };
  g_state->model.set_store(store);
  g_state->model.load_preferences();
  g_state->ui.reset();
  audio_header::reset(g_header);
  g_state->model.set_device_state(-1, true);
  acars_dashboard::install(&g_state->model, &g_state->ui, header_controls);
  g_state->logger.begin(acars_archive::FileOps{}, now);   // no card in a browser: the archive holds this session only
  g_state->model.set_archive(&g_state->archive);
  acars_ui::Capabilities caps;
  caps.live_reception = true;
  g_state->model.post_capabilities(caps);
  g_state->model.post_receiver_state(acars_ui::ReceiverState::paused);
  acars_dashboard::enter();
  g_started = true;
  g_synced = now;
  request_start(wanted_hz());
}

void leave() {
  if (!g_state) return;
  g_state->logger.end((uint32_t)emscripten_get_now());
  acars_dashboard::leave();
  g_state->model.set_demo(false);
  g_started = false;
}

void update(uint32_t now) {
  if (!g_state || !g_started) return;
  g_state->model.service();
  g_state->logger.service(now);
  g_state->model.post_receiver_state(g_receiving ? acars_ui::ReceiverState::running : acars_ui::ReceiverState::paused);
  acars_dashboard::update(now);
}

Result touch(int x, int y, bool pressed, uint32_t now) {
  Result r;
  if (!g_state || !pressed) return r;
  if (audio_header::home_hit(x, y)) { r.exit = true; return r; }
  if (audio_header::settings_hit(x, y) || audio_header::mute_hit(x, y) || audio_header::visualizer_hit(x, y)) return r;
  const acars_ui::Action action = g_state->ui.touch(x, y, g_state->model);
  if (action.kind == acars_ui::ActionKind::tune_requested) request_start(action.value);
  acars_dashboard::update(now);
  return r;
}

void message_event(const char* line) {
  acars::Message m{};
  char one[8] = {};
  char tmp[16];
  m.frequency_hz = (uint32_t)num(line, "frequency_hz");
  str(line, "mode", one, sizeof one);
  m.mode = one[0];
  str(line, "tail", m.tail, sizeof m.tail);
  str(line, "ack", one, sizeof one);
  m.acknowledgement = one[0];
  str(line, "label", m.label, sizeof m.label);
  str(line, "block_id", one, sizeof one);
  m.block_id = one[0];
  str(line, "end", tmp, sizeof tmp);
  m.end_marker = !strcmp(tmp, "ETB") ? 0x17 : 0x03;
  m.corrected = flag(line, "corrected");
  m.corrected_bits = (uint8_t)num(line, "corrected_bits");
  m.raw_bytes = (uint16_t)num(line, "raw_bytes");
  str(line, "flight", m.flight, sizeof m.flight);
  m.text_length = (uint16_t)str(line, "text", m.text, sizeof m.text);
  m.has_text = field(line, "text") != nullptr;
  g_state->model.post_message(m, (uint32_t)emscripten_get_now(), g_utc);
}

void stats_event(const char* line) {
  acars::Stats s{};
  s.sync_candidates = (uint32_t)num(line, "sync_candidates");
  s.crc_ok = (uint32_t)num(line, "crc_ok");
  s.corrected = (uint32_t)num(line, "corrected");
  s.crc_rejected = (uint32_t)num(line, "crc_rejected");
  s.framing_rejected = (uint32_t)num(line, "framing_rejected");
  s.overflows = (uint32_t)num(line, "overflows");
  s.bits = (uint32_t)num(line, "bits");
  g_state->model.post_stats(s);
}

void event(const char* line, uint32_t) {
  if (!g_state) return;
  char type[16] = {};
  str(line, "type", type, sizeof type);
  if (!strcmp(type, "message")) message_event(line);
  else if (!strcmp(type, "stats")) stats_event(line);
}

void set_num(const char* key, double v) {
  if (!strcmp(key, "receiving")) {
    g_receiving = v != 0;
    if (g_state && g_receiving) g_state->model.report_tuned(wanted_hz(), true);
  } else if (!strcmp(key, "utc")) g_utc = (uint32_t)v;
}

const char* take_request() {
  if (!g_request_pending) return nullptr;
  g_request_pending = false;
  return g_request;
}

}  // namespace

namespace dscreens {
extern const Screen kAcars = {dashboards::Id::acars, "acars", enter, leave, update, touch, event, set_num, take_request, nullptr};
}
