// FT8 / FT4 receive on the P4 console: the native decoder (ft8_runtime, shared with the Tab5) fed from the receiver's
// IQ, shown on the compact FT8 dashboard (ft8_compact_dashboard.cpp), which fits the small views. Receive only.
// Not on this target yet: the legacy dashboard's MAP, HUNTER and HEARD tabs, expert tuning, the decode log on the SD card,
// clock correction from decoded stations, JS8.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <new>
#include <sys/time.h>
#include "M5Unified.h"
#include "audio_service.hpp"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "ft8_compact_dashboard.hpp"
#include "ft8_decoder_backend.hpp"
#include "ft8_model.hpp"
#include "ft8_runtime.hpp"
#include "home_app.hpp"
#include "iq_tap.hpp"
#include "network_clock.hpp"
#include "present.hpp"
#include "radio_service.hpp"
#include "receiver_location.hpp"
#include "screens.hpp"
#include "ui_support.hpp"

namespace {
namespace ft8 = orcsdr::ft8;
namespace ft8_runtime = orcsdr::ft8_runtime;
namespace cp = orcsdr::ft8::compact;

constexpr size_t kDefaultBand = 5;            // 20 m
constexpr uint32_t kTapRate = 240000;         // the audio tap's base rate: no decimation, so the tap fits the core (960 kS/s did not)

ft8::DecodeStore* g_store = nullptr;          // PSRAM; written by the decoder task, read by the UI
ft8::Snapshot* g_snapshot = nullptr;          // PSRAM
portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;
ft8::DigitalMode g_mode = ft8::DigitalMode::ft8;
size_t g_band = kDefaultBand;
bool g_runtime_started = false;
uint32_t g_next_start_ms = 0;

uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

bool resize_viewport() {
  const auto* g = present_geometry();
  return cp::set_viewport(g ? cp::Rect{int(g->safe.x), int(g->safe.y), int(g->safe.width), int(g->safe.height)}
                            : cp::Rect{0, 0, 1280, 720});
}

uint32_t dial_hz() { return ft8::mode_dial_hz(g_band, g_mode); }

bool clock_valid() { return network_clock::status().wallclock_valid; }

// Dial minus the tuner's actual centre (the driver quantizes tuning), so the tap can find the USB passband.
float dial_offset_hz() {
  const uint32_t dial = ft8_runtime::requested_dial_hz();
  const uint32_t centre = home_app::status().applied_hz;
  if (dial == 0 || centre == 0) return 0.0f;
  // The driver reports the tuner's frequency, which is not the RF centre under the HF up-converter (14.074 MHz shows as 42.874 MHz):
  // only a small difference is a real tuning step, anything else means the reading is not the RF centre.
  const int32_t delta = static_cast<int32_t>(dial) - static_cast<int32_t>(centre);
  if (delta > 100000 || delta < -100000) return 0.0f;
  return static_cast<float>(delta);
}

void on_decode(const ft8::Decode& decode, void*) {
  if (g_store == nullptr) return;
  portENTER_CRITICAL(&g_lock);
  g_store->append(decode);
  portEXIT_CRITICAL(&g_lock);
}

void consume(const uint8_t* iq, size_t bytes, void*) { ft8_runtime::offer_iq(iq, bytes, orcsdr::radio::sample_rate()); }

void tune_band(size_t band) {
  g_band = std::min(band, ft8::band_count() - 1);
  const uint32_t dial = dial_hz();
  if (dial == 0) return;
  ft8_runtime::set_radio_context(static_cast<uint8_t>(g_band), dial);
  ft8_runtime::note_discontinuity();   // FT8 times its slots by sample count: a retune ends the slot in progress
  home_app::request_frequency(dial);
}

bool start_runtime() {
  if (ft8_runtime::active()) return true;
  g_runtime_started = ft8_runtime::start(g_mode, on_decode, nullptr, clock_valid, dial_offset_hz);
  g_next_start_ms = now_ms() + 5000;   // a failed start is retried no more than every 5 s
  return g_runtime_started;
}

void fill(ft8::Snapshot& s) {
  const home_app::Status st = home_app::status();
  const auto rs = orcsdr::radio::snapshot();
  const auto clock = network_clock::status();
  const auto gain = orcsdr::radio::gain();
  const auto rt = ft8_runtime::status();
  const auto here = orcsdr::receiver_location::snapshot();
  timeval tv{};
  gettimeofday(&tv, nullptr);
  s.utc_ms = static_cast<uint64_t>(tv.tv_sec) * 1000u + static_cast<uint64_t>(tv.tv_usec / 1000);
  s.clock_valid = clock.wallclock_valid;
  s.clock_sync_level = static_cast<uint8_t>(clock.level);
  s.clock_sync_age_s = clock.freshness_age_s;
  s.receiver_running = rs.receiver_state == 2;
  s.gain_auto = gain.auto_mode;
  s.gain_tenth_db = static_cast<int16_t>(gain.tenth_db);
  s.gain_available = gain.available;
  s.gain_step_count = gain.step_count;
  for (uint8_t i = 0; i < gain.step_count && i < 32; ++i) s.gain_steps_tenth_db[i] = static_cast<int16_t>(gain.steps[i]);
  s.rtl_agc_available = gain.rtl_agc_available;
  s.rtl_agc = gain.rtl_agc;
  s.iq_dbfs = orcsdr::radio::level_dbfs();
  s.battery_percent = static_cast<int16_t>(M5.Power.getBatteryLevel());
  s.selected_band = g_band;
  s.mode = g_mode;
  s.dial_hz = st.frequency_hz;
  s.dial_custom = false;
  s.decoder_capabilities = ft8_runtime::active() ? (ft8::decoder_cap_ft8 | ft8::decoder_cap_ft4) : 0u;
  using RS = ft8_runtime::State;
  using DS = ft8::DecoderState;
  const bool short_of_memory = !ft8_runtime::active() && ft8_runtime::last_start_report().failure == ft8_runtime::StartFailure::memory;
  if (short_of_memory) {
    const auto report = ft8_runtime::last_start_report();
    s.mem_free = report.psram_free;
    s.mem_largest = report.psram_largest;
    s.mem_needed = report.requested_bytes;
  }
  s.decoder_state = short_of_memory ? DS::no_memory
                  : !ft8_runtime::active() ? DS::unbound
                  : rt.state == RS::listening ? DS::listening
                  : rt.state == RS::decoding ? DS::decoding
                  : rt.state == RS::ready ? DS::ready
                  : rt.state == RS::error ? DS::error
                                          : DS::armed;
  s.last_slot_decodes = static_cast<uint8_t>(std::min<uint32_t>(rt.last_slot_decodes, 255u));
  s.candidate_count = static_cast<uint8_t>(std::min<uint32_t>(rt.last_coarse, 255u));
  const auto wf = ft8_runtime::waterfall();
  s.waterfall = wf.data;
  s.wf_rows = static_cast<uint16_t>(ft8_runtime::kWaterfallRows);
  s.wf_bins = static_cast<uint16_t>(ft8_runtime::kWaterfallBins);
  s.wf_sequence = wf.sequence;
  s.station_known = here.configured;
  s.station_latitude = static_cast<float>(here.latitude_e7) / 1e7f;
  s.station_longitude = static_cast<float>(here.longitude_e7) / 1e7f;
  portENTER_CRITICAL(&g_lock);
  const size_t n = std::min(g_store ? g_store->size() : 0u, ft8::kDecodeCapacity);
  for (size_t i = 0; i < n; ++i) s.decodes[i] = *g_store->newest(n - 1 - i);   // oldest first, as the dashboard expects
  s.decode_count = n;
  portEXIT_CRITICAL(&g_lock);
}




void enter() {
  if (g_store == nullptr) {
    void* store = heap_caps_calloc(1, sizeof(ft8::DecodeStore), MALLOC_CAP_SPIRAM);
    void* snap = heap_caps_calloc(1, sizeof(ft8::Snapshot), MALLOC_CAP_SPIRAM);
    if (store == nullptr || snap == nullptr) {
      heap_caps_free(store);
      heap_caps_free(snap);
      home_app::request_home();
      return;
    }
    g_store = new (store) ft8::DecodeStore();
    g_snapshot = new (snap) ft8::Snapshot();
  }
  if (!resize_viewport()) {
    home_app::request_home();
    return;
  }
  cp::set_header_hook(ui_support::draw_header);
  audio_service::set_active(false);
  orcsdr::radio::set_sample_rate_override(kTapRate);
  g_runtime_started = false;
  ft8_runtime::set_worker_core(1);   // the UI task keeps core 0 nearly full; core 1 is mostly idle
  (void)start_runtime();
  iq_tap::start("ft8", consume, nullptr, 256 * 1024, 1, 3, 16384, true);   // PSRAM stack: measured several times faster than an internal one here
  tune_band(g_band);
  *g_snapshot = ft8::Snapshot();
  fill(*g_snapshot);
  cp::enter(*g_snapshot);
}

void leave() {
  iq_tap::stop();
  ft8_runtime::stop();
  ft8_runtime::reap();
  g_runtime_started = false;
  orcsdr::radio::set_sample_rate_override(0);
  cp::leave();
}

void tick() {
  if (g_snapshot == nullptr || !cp::active()) return;
  ft8_runtime::touch();
  if (!ft8_runtime::active() && static_cast<int32_t>(now_ms() - g_next_start_ms) >= 0) (void)start_runtime();
  fill(*g_snapshot);
  cp::update(*g_snapshot);
}

void act(const ft8::Action& a) {
  switch (a.kind) {
    case ft8::ActionKind::tune_band: tune_band(a.value); break;
    case ft8::ActionKind::tune_auto: tune_band(g_band); break;
    case ft8::ActionKind::clear_decodes:
      portENTER_CRITICAL(&g_lock);
      g_store->clear();
      portEXIT_CRITICAL(&g_lock);
      break;
    case ft8::ActionKind::select_mode:
      if (ft8::valid_mode(static_cast<uint8_t>(a.value)) && ft8_runtime::set_mode(static_cast<ft8::DigitalMode>(a.value))) {
        g_mode = static_cast<ft8::DigitalMode>(a.value);
        portENTER_CRITICAL(&g_lock);
        g_store->clear();
        portEXIT_CRITICAL(&g_lock);
        tune_band(g_band);
      }
      break;
    case ft8::ActionKind::gain_auto: orcsdr::radio::set_gain_auto(); break;
    case ft8::ActionKind::sync_clock: (void)network_clock::request_sync(); break;
    case ft8::ActionKind::open_clock: home_app::request_open(orcsdr::dashboards::Id::settings); break;
    default: break;
  }
}

void tap(int x, int y) { act(cp::handle_touch(x, y)); }
}  // namespace

namespace screens {
const Screen& ft8_screen() {
  static const Screen s{orcsdr::dashboards::Id::ft8, "FT8", enter, leave, tick, tap, nullptr, nullptr, nullptr, []() {
    if (!resize_viewport()) return false;
    cp::draw();
    return true;
  }};
  return s;
}
}  // namespace screens
