// P4 control-channel monitor adapter. The shared Tab5 renderer and decoder
// run over the P4 receiver's production IQ service; profile management, trunked
// voice following, and audio playback are not implemented on this target.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <atomic>
#include "M5Unified.h"
#include "audio_service.hpp"
#include "home_app.hpp"
#include "p25_dashboard.hpp"
#include "p25_decoder.hpp"
#include "p25_config.hpp"
#include "present.hpp"
#include "radio_service.hpp"
#include "screens.hpp"
#include "wifi_app.hpp"

namespace {
namespace p25 = orcsdr::p25;
namespace decoder = orcsdr::p25decoder;
namespace config = orcsdr::p25config;

constexpr uint32_t kP25SampleRate = 960000;
constexpr uint32_t kMinSpanHz = 60000;
constexpr size_t kSpectrumBins = 256;
constexpr uint32_t kP25MinHz = 136000000;
constexpr uint32_t kP25MaxHz = 941000000;
constexpr char kP4Status[] = "P4 manual control channel; voice/audio unavailable";

config::Config g_config{};
config::StoreState g_profiles{};
orcsdr::radio::SinkHandle g_sink = -1;
std::atomic<bool> g_accept_iq{false};
std::atomic<bool> g_sink_unavailable{false};
std::atomic<bool> g_rate_mismatch{false};
std::atomic<uint32_t> g_expected_hz{0};
uint32_t g_decoder_generation = 0;
std::atomic<uint32_t> g_decoder_hz{0};
std::atomic<uint32_t> g_pending_tune_hz{0};
std::atomic<int> g_pending_span_step{0};
uint32_t g_config_revision = 1;
uint32_t g_span_hz = kP25SampleRate;
char g_action_status[32] = "VOICE AUDIO UNAVAILABLE";

bool resize_viewport() {
  const auto* geometry = present_geometry();
  if (!geometry) return p25::set_viewport(0, 0, 1280, 720);
  return p25::set_viewport(static_cast<int>(geometry->safe.x),
                           static_cast<int>(geometry->safe.y),
                           static_cast<int>(geometry->safe.width),
                           static_cast<int>(geometry->safe.height));
}

void consume(const orcsdr::radio::IqBlock& block, void*) {
  if (!g_accept_iq.load(std::memory_order_acquire) || block.receiver != 0) return;
  const uint32_t expected_hz = g_expected_hz.load(std::memory_order_acquire);
  if (!expected_hz || block.center_hz != expected_hz) return;
  if (block.sample_rate != kP25SampleRate) {
    g_rate_mismatch.store(true, std::memory_order_release);
    return;
  }

  if (block.generation != g_decoder_generation || block.center_hz != g_decoder_hz) {
    decoder::reset_at(static_cast<uint32_t>(block.time_us / 1000));
    decoder::suspend_voice();
    g_decoder_generation = block.generation;
    g_decoder_hz = block.center_hz;
  }
  g_rate_mismatch.store(false, std::memory_order_release);
  decoder::process_cu8_at(block.data, block.bytes,
                          static_cast<uint32_t>(block.time_us / 1000));
}

p25::Snapshot build_snapshot() {
  p25::Snapshot snapshot{};
  const auto home = home_app::status();
  const auto radio = orcsdr::radio::snapshot();
  const auto info = orcsdr::radio::info();

  snapshot.frequency_hz = home.frequency_hz;
  snapshot.span_hz = g_span_hz;
  snapshot.effective_sps = orcsdr::radio::sample_rate();
  snapshot.target_sps = kP25SampleRate;
  snapshot.usb_overruns = info.overruns;
  snapshot.consumer_drops = info.consumer_drops;
  orcsdr::radio::SinkStats sink_stats{};
  if (g_sink >= 0 && orcsdr::radio::sink_stats(g_sink, &sink_stats))
    snapshot.consumer_drops += sink_stats.drops;
  snapshot.relative_dbfs = orcsdr::radio::level_dbfs();
  snapshot.running = radio.receiver_state == 2 && g_sink >= 0 &&
                     g_expected_hz.load(std::memory_order_acquire) != 0 &&
                     g_decoder_hz == g_expected_hz.load(std::memory_order_acquire) &&
                     !g_rate_mismatch.load(std::memory_order_acquire);
  snapshot.driver_ready = radio.receiver_state == 2;
  snapshot.battery_percent = M5.Power.getBatteryLevel();
  snapshot.wifi_connected = wifi_app::status().connected;
  snapshot.sound_enabled = false;
  snapshot.auto_follow = false;
  snapshot.encryption_skip = true;
  snapshot.config = g_config;
  snapshot.profiles = g_profiles;
  snapshot.config_revision = g_config_revision;
  std::snprintf(snapshot.config_status, sizeof(snapshot.config_status), "%s", kP4Status);
  std::snprintf(snapshot.last_error, sizeof(snapshot.last_error), "%s", g_action_status);
  snapshot.decoded = decoder::snapshot();
  return snapshot;
}

void set_action_status(const char* status) {
  if (std::strcmp(g_action_status, status) == 0) return;
  std::snprintf(g_action_status, sizeof(g_action_status), "%s", status);
}

bool valid_frequency(uint32_t hz) {
  return hz >= kP25MinHz && hz <= kP25MaxHz;
}

void apply_action(const p25::Action& action) {
  switch (action.kind) {
    case p25::ActionKind::tune_hz:
      if (valid_frequency(action.value)) {
        g_expected_hz.store(action.value, std::memory_order_release);
        home_app::request_frequency(action.value);
        g_config.last_control_channel_hz = action.value;
        set_action_status("MANUAL CHANNEL; NO VOICE AUDIO");
      } else {
        set_action_status("FREQUENCY OUT OF RANGE");
      }
      break;
    case p25::ActionKind::span_down:
    case p25::ActionKind::span_up:
      g_span_hz = action.kind == p25::ActionKind::span_down
          ? std::max(kMinSpanHz, g_span_hz / 2)
          : std::min(kP25SampleRate, g_span_hz * 2);
      p25::update(build_snapshot());
      break;
    case p25::ActionKind::exit_to_home:
      home_app::request_home();
      break;
    case p25::ActionKind::open_device_settings:
      home_app::request_open(orcsdr::dashboards::Id::settings);
      break;
    case p25::ActionKind::none:
      break;
    default:
      set_action_status("CONTROL UNAVAILABLE ON P4");
      break;
  }
}

void enter() {
  g_accept_iq.store(false, std::memory_order_release);
  if (!resize_viewport()) {
    home_app::request_home();
    return;
  }

  g_pending_tune_hz = 0;
  g_pending_span_step = 0;
  config::defaults(&g_config);
  g_profiles = config::StoreState{};
  g_expected_hz.store(0, std::memory_order_release);
  g_decoder_generation = 0;
  g_decoder_hz = 0;
  g_rate_mismatch.store(false, std::memory_order_release);
  g_action_status[0] = '\0';
  set_action_status("VOICE AUDIO UNAVAILABLE");

  orcsdr::radio::set_sample_rate_override(kP25SampleRate);
  audio_service::set_active(false);
  decoder::configure(g_config.modulation, g_config.cqpsk_timing_gain,
                     g_config.cqpsk_carrier_gain);
  decoder::suspend_voice();
  decoder::reset();
  decoder::suspend_voice();

  p25::enter(build_snapshot());
  orcsdr::radio::SinkSpec spec{};
  spec.name = "p25";
  spec.fn = consume;
  spec.kind = orcsdr::radio::SinkKind::queued;
  spec.receiver = 0;
  spec.queue_bytes = 4 * 16384;
  spec.task_priority = 3;
  spec.task_stack = 8192;
  spec.task_stack_in_psram = true;
  g_sink = g_sink_unavailable.load(std::memory_order_acquire)
               ? -1 : orcsdr::radio::add_sink(spec);
  if (g_sink < 0) {
    set_action_status("P25 IQ SINK UNAVAILABLE");
    p25::update(build_snapshot());
    return;
  }
  g_accept_iq.store(true, std::memory_order_release);
}

void leave() {
  g_accept_iq.store(false, std::memory_order_release);
  g_pending_tune_hz = 0;
  if (g_sink >= 0 && orcsdr::radio::remove_sink(g_sink)) {
    g_sink = -1;
  } else if (g_sink >= 0) {
    // A timed out receiver-service sink is quarantined; keep its callback inert.
    g_sink_unavailable.store(true, std::memory_order_release);
  }
  decoder::suspend_voice();
  orcsdr::radio::set_sample_rate_override(0);
  p25::leave();
}

void tick() {
  if (!p25::active()) return;
  if (const auto hz = g_pending_tune_hz.exchange(0)) {
    apply_action({p25::ActionKind::tune_hz, hz});
    std::printf("RTL_P25_TUNE_APPLIED frequency_hz=%lu\n", static_cast<unsigned long>(hz));
  }
  if (const auto step = g_pending_span_step.exchange(0)) {
    apply_action({step < 0 ? p25::ActionKind::span_down : p25::ActionKind::span_up});
    std::printf("RTL_P25_SPAN_APPLIED span_hz=%lu\n", static_cast<unsigned long>(g_span_hz));
  }
  if (g_rate_mismatch.load(std::memory_order_acquire))
    set_action_status("P25 REQUIRES 960 KSPS");
  else if (std::strcmp(g_action_status, "P25 REQUIRES 960 KSPS") == 0)
    set_action_status("VOICE AUDIO UNAVAILABLE");
  p25::update(build_snapshot());
}

void tap(int x, int y) {
  apply_action(p25::handle_touch(x, y));
}
}  // namespace

namespace screens {
bool p25_tune_request(uint32_t hz) {
  if (!valid_frequency(hz) || !g_accept_iq.load()) return false;
  uint32_t empty = 0;
  return g_pending_tune_hz.compare_exchange_strong(empty, hz);
}

bool p25_span_request(int step) {
  if ((step != -1 && step != 1) || !g_accept_iq.load()) return false;
  int empty = 0;
  return g_pending_span_step.compare_exchange_strong(empty, step);
}

const Screen& p25_screen() {
  static const Screen screen{orcsdr::dashboards::Id::p25, "P25", enter, leave,
                             tick, tap, nullptr, [](const float* levels, size_t, size_t, float floor) {
    if (orcsdr::radio::sample_rate() != kP25SampleRate) return;
    const size_t visible = kSpectrumBins * g_span_hz / kP25SampleRate;
    p25::draw_spectrum(levels, (kSpectrumBins - visible) / 2, visible, floor);
  }, nullptr, []() {
    if (!resize_viewport()) return false;
    p25::draw();
    return true;
  }};
  return screen;
}
}  // namespace screens
