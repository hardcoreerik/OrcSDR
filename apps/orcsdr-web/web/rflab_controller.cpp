// RF LAB: OrcSDR's measurement screen (live spectrum and waterfall, IQ constellation, noise floor, occupied bandwidth, clipping, DC and
// IQ imbalance, receiver controls) over the node's raw IQ channel. The part of main.cpp that feeds it on the device (the RTL reader
// handing blocks to rf_analysis, the Runtime built from the driver, the actions carried out on the driver) is this file: the page opens
// a narrow channel on the node (raw 8-bit IQ, 300 kS/s, centred 50 kHz away from the tuner's DC spike), hands every block to
// rf_analysis, and the screen's actions become node tuner calls.
//
// What the browser cannot do is switched off in the capability mask the screen reads: sample-rate choice (the channel is 300 kS/s),
// frequency correction, tuner bandwidth and the on-device rate passport. Saving reports needs an SD card; this screen shows "SD
// storage unavailable" as it does on a device without one.
#include <M5Unified.h>
#include <emscripten.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "data_screens.hpp"
#include "esp_rtl_sdr.h"
#include "nvs_store.hpp"
#include "rf_analysis.hpp"
#include "rf_analysis_web.hpp"
#include "rf_lab.hpp"

namespace {

using namespace orcsdr;
using namespace dscreens;

constexpr uint32_t kUnsupported = ESP_RTL_SDR_CAP_CONTINUOUS_RATE | ESP_RTL_SDR_CAP_FREQ_CORRECTION | ESP_RTL_SDR_CAP_TUNER_BANDWIDTH |
                                  ESP_RTL_SDR_CAP_PASSPORT;
constexpr uint32_t kRate = 300000;

NvsStore g_nvs;
rf_lab::Runtime g_rt;
bool g_entered = false, g_inited = false, g_waiting = false, g_have_state = false;
uint32_t g_enter_ms = 0;
uint32_t g_center_hz = 100000000, g_last_iq_ms = 0;

constexpr size_t kRing = 6;
char g_ring[kRing][200];
size_t g_read = 0, g_write = 0;

void push(const char* text) {
  if ((g_write + 1) % kRing == g_read) return;   // full: the newest request is dropped, the screen asks again on the next tap
  snprintf(g_ring[g_write], sizeof g_ring[0], "%s", text);
  g_write = (g_write + 1) % kRing;
}

void ask_channel() {
  char text[96];
  snprintf(text, sizeof text, "{\"op\":\"rfchan\",\"center\":%u,\"sps\":%u}", (unsigned)g_center_hz, (unsigned)kRate);
  push(text);
}

void publish_runtime() {
  g_rt.frequency_hz = g_center_hz;
  g_rt.sample_rate_sps = kRate;
  g_rt.effective_sps = kRate;
  g_rt.source_available = g_last_iq_ms && (uint32_t)emscripten_get_now() - g_last_iq_ms < 1500;
  g_rt.sound_enabled = false;
  g_rt.sd_writable = false;
  snprintf(g_rt.driver_version, sizeof g_rt.driver_version, "node");
  snprintf(g_rt.health, sizeof g_rt.health, "%s", g_rt.source_available ? "STREAMING" : "NO IQ");
  rf_lab::set_runtime(g_rt);
}

// The screen's "restore entry state" needs the receiver's real state at the moment it opens (gain, AGC, bias), as on the device where the
// driver is local. Here it arrives with the page's first tuner report, so the screen opens when that has come (or after two seconds).
void open_screen() {
  g_waiting = false;
  publish_runtime();
  rf_lab::enter(0, 0);
  g_entered = true;
  ask_channel();
}

void enter(uint32_t now) {
  g_nvs.begin("orcsdr");
  g_center_hz = g_nvs.get_u32("rflab_hz", 100000000u);
  g_center_hz = std::max<uint32_t>(24000000u, std::min<uint32_t>(1766000000u - kRate, g_center_hz));
  if (!g_inited) {
    g_inited = true;
    rf_lab::initialize(nullptr);
    rf_analysis::set_observer(nullptr);
  }
  g_rt.capabilities = ESP_RTL_SDR_CAP_RETUNE;
  snprintf(g_rt.device, sizeof g_rt.device, "node receiver");
  g_last_iq_ms = 0;
  g_have_state = false;
  g_waiting = true;
  g_enter_ms = now;
  M5.Display.fillScreen(0);
  M5.Display.setTextColor(0x8c71, 0);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString("READING RECEIVER...", 640, 360);
  g_entered = true;
}

void leave() {
  if (!g_entered) return;
  if (!g_waiting) rf_lab::leave();
  g_waiting = false;
  g_entered = false;
}

void update(uint32_t now) {
  if (!g_entered) return;
  if (g_waiting) {
    if (g_have_state || now - g_enter_ms > 2000) open_screen();
    return;
  }
  publish_runtime();
  rf_analysis::pump();
  rf_lab::service_ui(now);
}

void act(const rf_lab::Action& a) {
  using K = rf_lab::ActionKind;
  char text[120];
  switch (a.kind) {
    case K::tune_hz:
      g_center_hz = std::max<uint32_t>(24000000u, std::min<uint32_t>(1766000000u, (uint32_t)a.value));
      g_nvs.begin("orcsdr");
      g_nvs.put_u32("rflab_hz", g_center_hz);
      ask_channel();
      break;
    case K::gain_mode:   // 1 = switch to manual, 0 = back to automatic (the screen's own convention)
      snprintf(text, sizeof text, a.value ? "{\"op\":\"gain\",\"auto\":false,\"tenth\":%d}" : "{\"op\":\"gain\",\"auto\":true}", g_rt.gain_tenth_db);
      push(text);
      break;
    case K::gain_tenth_db:
      snprintf(text, sizeof text, "{\"op\":\"gain\",\"auto\":false,\"tenth\":%d}", (int)a.value);
      push(text);
      break;
    case K::rtl_agc:
      snprintf(text, sizeof text, "{\"op\":\"agc\",\"on\":%s}", a.value ? "true" : "false");
      push(text);
      break;
    case K::bias_tee:
      snprintf(text, sizeof text, "{\"op\":\"bias\",\"on\":%s}", a.value ? "true" : "false");
      push(text);
      break;
    default: break;   // sample rate, ppm and tuner bandwidth are masked out of the capabilities
  }
}

Result touch(int x, int y, bool pressed, uint32_t now) {
  Result r;
  if (!g_entered || g_waiting) return r;
  rf_lab::handle_touch(x, y, pressed, now);
  rf_lab::Action a;
  while (rf_lab::take_action(&a)) {
    if (a.kind == rf_lab::ActionKind::close) r.exit = true;
    else act(a);
  }
  return r;
}

// The page's tuner/stream report: {"type":"rflab","caps":N,"gain_mode":"auto","gain_tenth_db":N,"rtl_agc":b,"bias":b,"gains":[..],
// "device":"..","drops":N,"usb_errors":N,"center":N}
void event(const char* line, uint32_t) {
  g_have_state = true;
  g_rt.capabilities = ((uint32_t)num(line, "caps")) & ~kUnsupported;
  g_rt.capabilities |= ESP_RTL_SDR_CAP_RETUNE;
  char mode[12];
  str(line, "gain_mode", mode, sizeof mode);
  g_rt.gain_mode = !strcmp(mode, "manual") ? rf_lab::GainMode::manual : rf_lab::GainMode::automatic;
  g_rt.gain_tenth_db = (int)num(line, "gain_tenth_db");
  g_rt.rtl_agc = flag(line, "rtl_agc");
  g_rt.bias_tee = flag(line, "bias");
  g_rt.usb_overruns = (uint32_t)num(line, "usb_errors");
  g_rt.consumer_drops = (uint32_t)num(line, "drops");
  str(line, "device", g_rt.device, sizeof g_rt.device);
  const double center = num(line, "center");
  if (center > 0) g_center_hz = (uint32_t)center;   // what the node actually tuned
  g_rt.gain_count = 0;
  if (const char* p = field(line, "gains")) {
    if (*p == '[') ++p;
    while (*p && *p != ']' && g_rt.gain_count < 32) {
      g_rt.gain_ladder[g_rt.gain_count++] = atoi(p);
      p = strchr(p, ',');
      if (!p) break;
      ++p;
    }
  }
}

void set_num(const char*, double) {}

const char* take_request() {
  if (g_read == g_write) return nullptr;
  const char* text = g_ring[g_read];
  g_read = (g_read + 1) % kRing;
  return text;
}

// 8-bit IQ blocks from the node's channel, newest wins (rf_analysis keeps the latest 16 KB)
void iq(const uint8_t* data, size_t n) {
  if (!g_entered) return;
  g_last_iq_ms = (uint32_t)emscripten_get_now();
  rf_analysis::offer_iq(data, n);
}

}  // namespace

namespace dscreens {
extern const Screen kRfLab = {dashboards::Id::rf_lab, "rflab", enter, leave, update, touch, event, set_num, take_request, nullptr, nullptr, nullptr, iq};
}
