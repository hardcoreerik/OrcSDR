// SPDX-License-Identifier: AGPL-3.0-only
// Host tests for the ACARS receive chain: a synthetic AM/MSK signal at 240 kS/s CU8 goes through Frontend240k, Decoder
// (ten bit-phase hypotheses) and the acars_receiver adapter into the dashboard model. The signal is a test fixture built here
// from the ARINC 618 framing; it is not a recording. These tests prove the signal path is wired and robust to bit timing,
// carrier offset, noise and stream gaps. They say nothing about performance on real off-air signals.
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "acars_core.hpp"
#include "acars_dashboard_model.hpp"
#include "acars_receiver.hpp"

using namespace orcsdr;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 240000.0;

std::vector<uint8_t> make_block(const char* tail, const char* text) {
  std::vector<uint8_t> b;
  auto put = [&](uint8_t c) { b.push_back(acars::odd_parity_byte(c)); };
  put('2');
  for (int i = 0; i < 7; ++i) put(uint8_t(tail[i]));
  put(0x15);
  put('Q'); put('0');
  put('1');
  put(0x02);
  for (const char* c = text; *c; ++c) put(uint8_t(*c));
  put(0x03);
  const uint16_t crc = acars::crc16_kermit(b.data(), b.size());
  b.push_back(uint8_t(crc));
  b.push_back(uint8_t(crc >> 8));
  b.push_back(0x7f);
  return b;
}

// Bits as the frame decoder consumes them (post-differential): preamble, SYN SYN SOH framing, block bytes LSB first.
std::vector<bool> make_bits(const std::vector<uint8_t>& block) {
  std::vector<bool> s;
  for (int i = 0; i < 20 * 8; ++i) s.push_back(true);
  auto byte = [&](uint8_t v) { for (int bit = 0; bit < 8; ++bit) s.push_back(((v >> bit) & 1) != 0); };
  byte(acars::odd_parity_byte('+'));
  byte(acars::odd_parity_byte('*'));
  byte(0x16); byte(0x16); byte(0x01);
  for (uint8_t b : block) byte(b);
  for (int i = 0; i < 16; ++i) s.push_back(true);
  return s;
}

struct SignalOptions {
  int start_offset = 0;          // samples of lead-in (sets the bit phase)
  double carrier_hz = 60000.0;   // where the AM carrier sits in the 240 kS/s stream
  double depth = 0.5;
  double amplitude = 0.5;        // of full scale
  double noise_sigma = 0.0;      // in units of full scale
  size_t tail_samples = 4000;
  uint32_t seed = 1;
};

// AM carrier modulated by a phase-continuous 1200 / 2400 Hz tone: 1200 Hz is a state change, 2400 Hz holds (the decoder's
// differential convention, initial state 1).
std::vector<uint8_t> modulate(const std::vector<bool>& states, const SignalOptions& o) {
  const size_t per_bit = size_t(kRate / 2400.0);
  std::vector<uint8_t> out;
  std::mt19937 rng(o.seed);
  std::normal_distribution<double> noise(0.0, o.noise_sigma > 0 ? o.noise_sigma : 1.0);
  auto push = [&](double i, double q) {
    if (o.noise_sigma > 0) { i += noise(rng); q += noise(rng); }
    auto q8 = [](double v) { return uint8_t(std::lround(std::fmax(0.0, std::fmin(255.0, 127.5 + 127.5 * v)))); };
    out.push_back(q8(i));
    out.push_back(q8(q));
  };
  size_t n = 0;
  double carrier_phase = 0.0;
  const double carrier_step = 2.0 * kPi * o.carrier_hz / kRate;
  for (int i = 0; i < o.start_offset; ++i, ++n) { push(0, 0); carrier_phase += carrier_step; }
  bool prev = true;
  double audio_phase = 0.0;
  for (bool s : states) {
    const double f = (s != prev) ? 1200.0 : 2400.0;
    prev = s;
    for (size_t k = 0; k < per_bit; ++k, ++n) {
      audio_phase += 2.0 * kPi * f / kRate;
      const double env = (1.0 + o.depth * std::sin(audio_phase)) / (1.0 + o.depth);
      push(o.amplitude * env * std::cos(carrier_phase), o.amplitude * env * std::sin(carrier_phase));
      carrier_phase += carrier_step;
    }
  }
  for (size_t i = 0; i < o.tail_samples; ++i, ++n) {
    push(0, 0);
    carrier_phase += carrier_step;
  }
  return out;
}

struct Sink {
  unsigned count = 0;
  acars::Message last{};
  static void receive(void* p, const acars::Message& m) {
    auto* self = static_cast<Sink*>(p);
    ++self->count;
    self->last = m;
  }
};

void feed_chain(const std::vector<uint8_t>& cu8, int32_t offset_hz, acars::Frontend240k& front, acars::Decoder& dec) {
  front.reset();
  (void)offset_hz;   // the front end expects the carrier at +60 kHz
  size_t pos = 0;
  while (pos < cu8.size()) {
    const size_t chunk = std::min<size_t>(cu8.size() - pos, 8192);   // the driver's block size, an even byte count
    front.process_cu8(cu8.data() + pos, chunk / 2,
                      [](void* c, float i, float q) { static_cast<acars::Decoder*>(c)->process_channel_iq(i, q); }, &dec);
    pos += chunk;
  }
}

unsigned decode(const SignalOptions& o, int32_t mixer_offset_hz, Sink& sink) {
  const auto bits = make_bits(make_block("N123456", "1234UAL111 TEST MESSAGE"));
  const auto cu8 = modulate(bits, o);
  acars::Frontend240k front;
  acars::Decoder dec;
  dec.set_callback(Sink::receive, &sink);
  dec.set_frequency(131550000);
  feed_chain(cu8, mixer_offset_hz, front, dec);
  return sink.count;
}

void test_every_bit_phase() {
  for (int offset = 0; offset < 100; offset += 7) {
    SignalOptions o;
    o.start_offset = 3000 + offset;
    Sink sink;
    decode(o, 60000, sink);
    if (sink.count != 1) std::printf("bit phase offset %d decoded %u blocks\n", offset, sink.count);
    assert(sink.count == 1);
    assert(std::strcmp(sink.last.tail, "N123456") == 0);
    assert(sink.last.frequency_hz == 131550000);
    assert(sink.last.has_text && sink.last.end_marker == 0x03);
    assert(!sink.last.corrected);
  }
  std::puts("  every bit phase: one verified block each");
}

void test_carrier_offset_error() {
  // A cheap dongle's crystal error moves the carrier away from the nominal channel position.
  for (double error : {-4000.0, -2000.0, 2000.0, 4000.0}) {
    SignalOptions o;
    o.start_offset = 3033;
    o.carrier_hz = 60000.0 + error;
    Sink sink;
    decode(o, 60000, sink);
    if (sink.count != 1) std::printf("carrier error %.0f Hz decoded %u blocks\n", error, sink.count);
    assert(sink.count == 1);
  }
  std::puts("  carrier error +-4 kHz: decoded");
}

void test_noise_and_levels() {
  // Noise at -26 dBFS rms against a -6 dBFS carrier is a clean-signal check, not a sensitivity claim.
  unsigned ok = 0, runs = 0;
  for (uint32_t seed = 1; seed <= 12; ++seed) {
    SignalOptions o;
    o.start_offset = 3000 + int(seed) * 8;
    o.noise_sigma = 0.05;
    o.seed = seed;
    Sink sink;
    decode(o, 60000, sink);
    assert(sink.count <= 1);          // never a duplicate, whatever the phase
    ok += sink.count;
    ++runs;
  }
  std::printf("  noise sigma 0.05: %u / %u decoded, no duplicates\n", ok, runs);
  assert(ok >= 10);
}

void test_noise_only_makes_nothing() {
  std::mt19937 rng(7);
  std::normal_distribution<double> noise(0.0, 0.2);
  std::vector<uint8_t> cu8;
  for (int i = 0; i < 240000 * 2 * 2; ++i)
    cu8.push_back(uint8_t(std::lround(std::fmax(0.0, std::fmin(255.0, 127.5 + 127.5 * noise(rng))))));
  acars::Frontend240k front;
  acars::Decoder dec;
  Sink sink;
  dec.set_callback(Sink::receive, &sink);
  feed_chain(cu8, 60000, front, dec);
  assert(sink.count == 0);
  const acars::Stats& s = dec.stats();
  std::printf("  two seconds of noise: 0 blocks, %u bits, %u sync candidates, %u crc rejects\n", unsigned(s.bits),
              unsigned(s.sync_candidates), unsigned(s.crc_rejected));
}

void test_two_blocks_not_merged() {
  const auto a = make_bits(make_block("N123456", "FIRST"));
  const auto b = make_bits(make_block("N123456", "FIRST"));   // identical content, genuinely sent twice
  std::vector<bool> both = a;
  both.insert(both.end(), b.begin(), b.end());
  SignalOptions o;
  o.start_offset = 3041;
  const auto cu8 = modulate(both, o);
  acars::Frontend240k front;
  acars::Decoder dec;
  Sink sink;
  dec.set_callback(Sink::receive, &sink);
  feed_chain(cu8, 60000, front, dec);
  assert(sink.count == 2);
  assert(dec.stats().crc_ok == 2);
  std::puts("  two identical blocks sent back to back: both delivered");
}

// ---- adapter ----
struct FakeRadio {
  bool ready = true;
  bool accept = true;
  uint32_t rate = 0;
  uint32_t center = 0;
  uint32_t now = 1000;
  uint32_t utc = 1760000000u;
  unsigned tune_calls = 0;
  unsigned releases = 0;
};
FakeRadio g_radio;

void test_adapter() {
  acars_receiver::Hooks hooks;
  hooks.device_ready = [] { return g_radio.ready; };
  hooks.tune = [](uint32_t c) {
    ++g_radio.tune_calls;
    if (!g_radio.accept) return false;
    g_radio.center = c;
    g_radio.rate = 240000;
    return true;
  };
  hooks.release = [] { ++g_radio.releases; };
  hooks.sample_rate = [] { return g_radio.rate; };
  hooks.tuned_center = [] { return g_radio.center; };
  hooks.now_ms = [] { return g_radio.now; };
  hooks.utc_epoch = [] { return g_radio.utc; };
  acars_receiver::install(hooks);

  static acars_ui::Model model;
  assert(acars_receiver::start(&model));
  model.service();
  assert(model.snapshot().caps.live_reception);
  assert(model.snapshot().receiver == acars_ui::ReceiverState::paused);

  // The dashboard's requested channel (preset 0 of the default region) becomes the radio's centre minus nothing: centre = channel - 60 kHz.
  const uint32_t channel = model.snapshot().requested_hz;
  assert(channel >= 118000000u && channel <= 137000000u);
  acars_receiver::service();
  assert(g_radio.tune_calls == 1);
  assert(g_radio.center == channel - 60000u);

  // Blocks inside the settle window are dropped, not decoded.
  const auto bits = make_bits(make_block("N777AB ", "HELLO FROM THE FIXTURE"));
  SignalOptions o;
  o.start_offset = 3017;
  const auto cu8 = modulate(bits, o);
  g_radio.now += 100;
  acars_receiver::offer_iq(cu8.data(), 8192, 240000);
  assert(acars_receiver::status().settle_skipped == 1);
  assert(acars_receiver::status().blocks == 0);

  // A block at another sample rate is refused and counted.
  g_radio.now += 1000;
  acars_receiver::offer_iq(cu8.data(), 8192, 2400000);
  assert(acars_receiver::status().wrong_rate_blocks == 1);
  assert(acars_receiver::status().blocks == 0);

  // The real stream: 8192-byte blocks every 17 ms of device time.
  size_t pos = 0;
  while (pos < cu8.size()) {
    const size_t chunk = std::min<size_t>(cu8.size() - pos, 8192);
    acars_receiver::offer_iq(cu8.data() + pos, chunk, 240000);
    pos += chunk;
    g_radio.now += 17;
  }
  acars_receiver::service();
  model.service();
  const acars_ui::Snapshot& snap = model.snapshot();
  assert(snap.receiver == acars_ui::ReceiverState::running);
  assert(snap.tune == acars_ui::TuneStatus::tuned && snap.tuned_hz == channel);
  assert(snap.messages_total == 1);
  assert(std::strcmp(snap.messages[0].tail, "N777AB") == 0);
  assert(snap.messages[0].source == acars_ui::SourceKind::live);
  assert(snap.messages[0].time_valid && snap.messages[0].utc_epoch == g_radio.utc);
  assert(snap.messages[0].frequency_hz == channel);
  assert(snap.stats_available && snap.stats.crc_ok == 1);
  char line[640];
  acars_receiver::format_status(line, sizeof(line));
  std::printf("  adapter: %s\n", line);

  // A gap in the stream resets the decoder instead of bridging it.
  g_radio.now += 2000;
  acars_receiver::offer_iq(cu8.data(), 8192, 240000);
  assert(acars_receiver::status().discontinuities == 1);

  // The wall clock being invalid must be shown as such, never invented.
  g_radio.utc = 0;
  g_radio.now += 17;
  {
    const auto b2 = modulate(make_bits(make_block("N888CD ", "NO CLOCK")), o);
    for (size_t p = 0; p < b2.size(); p += 8192) {
      acars_receiver::offer_iq(b2.data() + p, std::min<size_t>(8192, b2.size() - p), 240000);
      g_radio.now += 17;
    }
    model.service();
    const acars_ui::Snapshot& s2 = model.snapshot();
    assert(s2.messages_total == 2);
    // Counters are one scope: the gap reset in between must not have zeroed crc_ok.
    assert(s2.stats.crc_ok == 2);
    assert(!s2.messages[0].time_valid);
  }

  // Following a changed request retunes once; a radio that refuses is reported as failed, not as tuned.
  g_radio.utc = 1760000000u;
  assert(model.set_preset(1));
  acars_receiver::service();
  assert(g_radio.tune_calls == 2);
  assert(g_radio.center == model.snapshot().requested_hz - 60000u);
  const uint32_t channel_before_refusal = acars_receiver::status().channel_hz;
  g_radio.accept = false;
  assert(model.set_preset(0));
  acars_receiver::service();
  model.service();
  assert(model.snapshot().tune == acars_ui::TuneStatus::failed);
  // A refused request must not relabel the decoder: the radio is still on the old centre.
  assert(acars_receiver::status().channel_hz == channel_before_refusal);
  g_radio.accept = true;
  // Opening the screen again while it is running must not reset the receiver.
  const uint32_t blocks_before_reopen = acars_receiver::status().blocks;
  assert(acars_receiver::start(&model));
  assert(acars_receiver::status().blocks == blocks_before_reopen && acars_receiver::running());

  // Dongle unplugged: unavailable. Closing releases the rate exactly once and drops the feed.
  g_radio.ready = false;
  acars_receiver::service();
  model.service();
  assert(model.snapshot().receiver == acars_ui::ReceiverState::unavailable);
  g_radio.ready = true;
  acars_receiver::stop();
  acars_receiver::stop();
  model.service();
  assert(g_radio.releases == 1);
  assert(model.snapshot().receiver == acars_ui::ReceiverState::disconnected);
  assert(!model.snapshot().caps.live_reception);
  assert(model.snapshot().tune == acars_ui::TuneStatus::unavailable);
  std::puts("  adapter: tune, settle, rate guard, gap reset, clock honesty, refusal, unplug, release");
}

}  // namespace

int main() {
  std::puts("acars_receiver_tests");
  test_every_bit_phase();
  test_carrier_offset_error();
  test_noise_and_levels();
  test_noise_only_makes_nothing();
  test_two_blocks_not_merged();
  test_adapter();
  std::puts("acars_receiver_tests: all passed");
  return 0;
}
