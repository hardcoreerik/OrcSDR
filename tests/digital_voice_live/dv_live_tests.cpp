// Host tests for the Digital Voice live chain: channelizer, scanner state machine and the dv_radio attachment, driven
// with SYNTHETIC IQ only (DMR and NXDN generators from the decoder suites, upsampled to the 240 kS/s CU8 stream the Tab5 gets).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <random>
#include <thread>
#include <vector>

#include "dv_channelizer.hpp"
#include "dv_radio.hpp"
#include "dv_scanner.hpp"

std::vector<float> dv_test_dmr_iq48();
std::vector<float> dv_test_nxdn_iq48(bool nxdn96, unsigned ran);

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)
using namespace orcsdr;

namespace {
uint32_t g_now = 1000;
uint64_t g_lost = 0;
bool g_ready = true, g_accept = true;
uint32_t g_tuned_center = 0, g_tune_calls = 0;
bool hk_ready() { return g_ready; }
bool hk_tune(uint32_t center, uint32_t rate) { ++g_tune_calls; g_tuned_center = center; return g_accept && rate == 240000; }
void hk_release() {}
uint32_t hk_now() { return g_now; }
uint32_t hk_us() { return g_now * 1000u; }
uint64_t hk_lost() { return g_lost; }

// 48 kS/s CF32 (interleaved) -> 240 kS/s CU8 with the channel at +60 kHz (inverse of the channelizer's mixer).
std::vector<uint8_t> to_cu8_240k(const std::vector<float>& x48, double amplitude = 0.8) {
  const size_t n = x48.size() / 2;
  std::vector<uint8_t> out;
  out.reserve(n * 5 * 2);
  for (size_t k = 0; k < n; ++k) {
    const size_t k1 = k + 1 < n ? k + 1 : k;
    for (int m = 0; m < 5; ++m) {
      const double a = 1.0 - m / 5.0, b = m / 5.0;
      const double re = x48[2 * k] * a + x48[2 * k1] * b, im = x48[2 * k + 1] * a + x48[2 * k1 + 1] * b;
      const size_t idx = k * 5 + static_cast<size_t>(m);
      double ur, ui;
      switch (idx & 3) { case 0: ur = re; ui = im; break; case 1: ur = -im; ui = re; break;
                         case 2: ur = -re; ui = -im; break; default: ur = im; ui = -re; break; }
      auto q = [&](double v) { double c = 127.5 + 127.5 * amplitude * v; return static_cast<uint8_t>(c < 0 ? 0 : c > 255 ? 255 : std::lround(c)); };
      out.push_back(q(ur)); out.push_back(q(ui));
    }
  }
  return out;
}

std::thread g_worker;
bool g_threaded = false;
bool hk_spawn(void (*fn)(void*), void* arg) { g_worker = std::thread(fn, arg); return true; }
void hk_join() { if (g_worker.joinable()) g_worker.join(); }
void hk_sleep(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

void install_hooks() {
  dv_radio::Hooks h;
  if (g_threaded) { h.spawn_worker = hk_spawn; h.join_worker = hk_join; h.sleep_ms = hk_sleep; }
  h.device_ready = hk_ready; h.tune = hk_tune; h.release = hk_release; h.now_ms = hk_now; h.micros = hk_us; h.lost_samples = hk_lost;
  dv_radio::install(h);
}
// Feed a stream in DSP-sized blocks, advancing the fake clock 8 ms per block (a 16 KB block at 240 kS/s ~ 34 ms; slower is safe).
void feed(const std::vector<uint8_t>& cu8, size_t block = 8192) {
  for (size_t off = 0; off < cu8.size(); off += block) {
    const size_t n = cu8.size() - off < block ? cu8.size() - off : block;
    dv_radio::offer_iq(cu8.data() + off, n, 240000);
    g_now += 8;
  }
}
void wait_drained() {   // threaded mode: let the worker finish what the DSP side queued
  for (int i = 0; i < 4000 && dv_radio::status().ring_pending != 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(2));
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
}
std::vector<dv_radio::Event> drain() {
  std::vector<dv_radio::Event> v; dv_radio::Event e;
  while (dv_radio::pop_event(&e)) v.push_back(e);
  return v;
}

void test_channelizer_tone() {
  dv::Channelizer ch;
  // A tone at +60 kHz + 3 kHz must land at +3 kHz at 48 kS/s with ~unity gain; a tone 40 kHz off must be rejected.
  auto run = [&](double hz) {
    ch.reset();
    std::vector<uint8_t> cu8(2 * 24000);
    for (size_t n = 0; n < 24000; ++n) {
      const double ph = 2 * M_PI * hz * n / 240000.0;
      cu8[2 * n] = static_cast<uint8_t>(std::lround(127.5 + 100 * std::cos(ph)));
      cu8[2 * n + 1] = static_cast<uint8_t>(std::lround(127.5 + 100 * std::sin(ph)));
    }
    std::vector<dv::Iq48> out(24000 / 5 + 2);
    const size_t n = ch.process_cu8(cu8.data(), 24000, out.data(), out.size());
    CHECK(n == 4800);
    double p = 0; for (size_t i = 200; i < n; ++i) p += out[i].re * out[i].re + out[i].im * out[i].im;
    return std::sqrt(p / (n - 200));
  };
  const double in_band = run(63000);
  const double reject = run(100000);   // 40 kHz above the channel
  CHECK(in_band > 0.6 && in_band < 0.9);   // 100/127.5 = 0.784
  CHECK(reject < in_band * 0.02);          // > 34 dB
}

void test_scanner() {
  CHECK(dv::Scanner::self_check());
  dv::Scanner s;
  const uint32_t plan[] = {460000000u, 461000000u, 462000000u};
  CHECK(s.set_plan(plan, 3));
  const uint32_t bad[] = {460000000u, 5u};
  CHECK(!s.set_plan(bad, 2));                       // validation: plan unchanged
  CHECK(s.plan_count() == 3);
  uint32_t t = 100;
  CHECK(s.start(t));
  dv::ScanInputs in; in.receiver_ready = true;
  auto st = s.service(t, in);
  CHECK(st.tune && st.tune_hz == plan[0]);
  s.retune_result(false, t);                         // refused twice -> third refusal fails
  st = s.service(t, in); CHECK(st.tune); s.retune_result(false, t);
  st = s.service(t, in); CHECK(st.tune); s.retune_result(false, t);
  CHECK(s.status().state == dv::ScanState::failed && s.status().fail == dv::FailReason::retune_refused);
  s.stop();
  // Normal pass with a dwell timeout (nothing verified): moves on, no hold.
  CHECK(s.start(t));
  st = s.service(t, in); CHECK(st.tune); s.retune_result(true, t);
  t += 700; s.service(t, in); CHECK(s.status().state == dv::ScanState::sampling);
  t += 1500; st = s.service(t, in);
  CHECK(s.status().index == 1 && s.status().holds == 0);
  // Verified activity holds, extends on newer frames, then releases after hold_ms.
  st = s.service(t, in); CHECK(st.tune && st.tune_hz == plan[1]); s.retune_result(true, t);
  t += 700; s.service(t, in);
  in.verified_ms = t + 100;
  t += 200; s.service(t, in); CHECK(s.status().state == dv::ScanState::holding && s.status().holds == 1);
  t += 3000; in.verified_ms = t; s.service(t, in); CHECK(s.status().state == dv::ScanState::holding);   // extended
  t += 3000; s.service(t, in); CHECK(s.status().state == dv::ScanState::holding);                        // still inside hold
  t += 2000; s.service(t, in); CHECK(s.status().index == 2);                                              // call ended -> next
  // Manual hold never leaves; skip does; pause freezes; receiver removal fails.
  in.verified_ms = 0;
  st = s.service(t, in); CHECK(st.tune); s.retune_result(true, t);
  t += 700; s.service(t, in);
  s.toggle_manual_hold(); t += 20000; s.service(t, in); CHECK(s.status().index == 2 && s.status().manual_hold);
  s.toggle_manual_hold(); s.skip(t); CHECK(s.status().index == 0 && s.status().passes == 1);
  st = s.service(t, in); CHECK(st.tune); s.retune_result(true, t);
  s.pause(); t += 60000; st = s.service(t, in); CHECK(!st.tune && s.status().state == dv::ScanState::paused);
  s.resume(t); CHECK(s.status().state == dv::ScanState::sampling);
  in.receiver_ready = false; s.service(t, in);
  CHECK(s.status().state == dv::ScanState::failed && s.status().fail == dv::FailReason::receiver_removed);
}

void test_monitor_revisits() {
  uint32_t hz[16] = {100, 101, 102, 103, 104, 105};
  uint8_t mk[16] = {1, 2, 3, 4, 5, 6};
  const uint32_t voice[2] = {101, 999};
  size_t n = dv::add_revisits(hz, mk, 6, 16, voice, 2);      // 999 is not in the list: ignored
  CHECK(n == 7);
  CHECK(hz[1] == 101 && hz[5] == 101 && mk[5] == 2);        // second visit half a round (3) after the first, mask follows
  CHECK(hz[0] == 100 && hz[2] == 102 && hz[6] == 105);      // everything else keeps its order
  uint32_t h2[3] = {1, 2, 3}; uint8_t m2[3] = {1, 1, 1};
  CHECK(dv::add_revisits(h2, m2, 3, 3, voice, 2) == 3);     // no room: unchanged
  const uint32_t v3[1] = {3};
  uint32_t h3[8] = {1, 2, 3}; uint8_t m3[8] = {1, 1, 1};
  n = dv::add_revisits(h3, m3, 3, 8, v3, 1);
  CHECK(n == 4 && h3[3] == 3);                              // last entry: the revisit goes at the end
}

void test_scanner_hold_time() {
  // A longer hold keeps the channel through a pause in a conversation; the default releases after 4 s. Limits are enforced.
  const uint32_t plan[2] = {460000000, 461000000};
  for (uint32_t hold : {4000u, 9000u}) {
    dv::Scanner s; CHECK(s.set_plan(plan, 2)); if (hold != 4000u) s.set_hold_ms(hold);
    CHECK(s.hold_ms() == hold);
    uint32_t t = 1000; dv::ScanInputs in; in.receiver_ready = true;
    CHECK(s.start(t));
    auto st = s.service(t, in); CHECK(st.tune); s.retune_result(true, t);
    t += 700; s.service(t, in);
    const uint32_t verified = t + 100; in.verified_ms = verified;
    t += 200; s.service(t, in); CHECK(s.status().state == dv::ScanState::holding);
    t = verified + 3900; s.service(t, in); CHECK(s.status().state == dv::ScanState::holding);          // inside either hold
    t = verified + 5000; s.service(t, in);
    CHECK((hold == 4000u) == (s.status().index == 1));                                                  // 4 s: moved on; 9 s: still holding
    if (hold == 9000u) { CHECK(s.status().state == dv::ScanState::holding); t = verified + 9100; s.service(t, in); CHECK(s.status().index == 1); }
  }
  dv::Scanner s; s.set_hold_ms(1); CHECK(s.hold_ms() == 500); s.set_hold_ms(1000000); CHECK(s.hold_ms() == 60000);
  s.set_control_hold_max_ms(1); CHECK(s.control_hold_max_ms() == 1000); s.set_control_hold_max_ms(1000000); CHECK(s.control_hold_max_ms() == 60000);
  // Control-only traffic (verified events renewed every second, never an identity) holds a channel until control_hold_max_ms, then the scan moves on.
  for (uint32_t cmax : {20000u, 6000u}) {
    dv::Scanner c; CHECK(c.set_plan(plan, 2)); c.set_control_hold_max_ms(cmax);
    uint32_t t = 1000; dv::ScanInputs in; in.receiver_ready = true;
    CHECK(c.start(t));
    auto st = c.service(t, in); CHECK(st.tune); c.retune_result(true, t);
    t += 700; c.service(t, in);
    const uint32_t begin = t;
    in.verified_ms = t + 50; t += 100; c.service(t, in); CHECK(c.status().state == dv::ScanState::holding);
    uint32_t left_at = 0;
    for (; t < begin + 25000 && !left_at; t += 1000) { in.verified_ms = t; c.service(t, in); if (c.status().index == 1) left_at = t - begin; }
    CHECK(left_at >= cmax && left_at <= cmax + 1500);                                   // released by the control-only rule, not earlier, not much later
    CHECK(c.status().control_releases == 1);
  }
  // List plans: a silent channel is left after dwell_ms (1.5 s) normally, after quick_ms (0.6 s) when quick_lists is on; a pending candidate is never cut short.
  for (int mode = 0; mode < 3; ++mode) {
    dv::Scanner q; CHECK(q.set_plan(plan, 2)); q.set_quick_lists(mode != 0); CHECK(q.quick_lists() == (mode != 0));
    uint32_t t = 1000; dv::ScanInputs in; in.receiver_ready = true; in.candidate = (mode == 2);
    CHECK(q.start(t)); q.service(t, in); q.retune_result(true, t); t += 700; q.service(t, in);
    const uint32_t begin = t; uint32_t left = 0;
    for (; t < begin + 4000 && !left; t += 100) { q.service(t, in); if (q.status().index == 1) left = t - begin; }
    if (mode == 0) CHECK(left >= 1500 && left <= 1700);
    if (mode == 1) CHECK(left >= 600 && left <= 800);
    if (mode == 2) CHECK(left >= 3000);
  }
  // Voice or identity evidence on the held channel overrides the control-only release: the hold continues.
  { dv::Scanner c; CHECK(c.set_plan(plan, 2)); c.set_control_hold_max_ms(6000);
    uint32_t t = 1000; dv::ScanInputs in; in.receiver_ready = true; CHECK(c.start(t));
    c.service(t, in); c.retune_result(true, t); t += 700; c.service(t, in);
    const uint32_t begin = t;
    in.verified_ms = t + 50; t += 100; c.service(t, in);
    for (; t < begin + 15000; t += 1000) { in.verified_ms = t; in.identity_ms = t; c.service(t, in); }
    CHECK(c.status().index == 0 && c.status().state == dv::ScanState::holding && c.status().control_releases == 0); }
}

void test_dmr_end_to_end() {
  install_hooks(); g_ready = true; g_accept = true; g_lost = 0; g_now = 1000;
  CHECK(dv_radio::start());
  CHECK(dv_radio::tune_channel(462562500u));
  CHECK(g_tuned_center == 462562500u - 60000u);
  const auto cu8 = to_cu8_240k(dv_test_dmr_iq48());
  // Blocks during settle are skipped, never decoded.
  dv_radio::offer_iq(cu8.data(), 8192, 240000);
  CHECK(dv_radio::status().settle_skipped == 1 && dv_radio::status().blocks == 0);
  g_now += 700;
  feed(cu8);
  auto ev = drain();
  const auto s = dv_radio::status();
  std::printf("dmr: blocks=%u events=%zu acq=%u id=%u max_us=%u\n", s.blocks, ev.size(), s.dmr_acquisitions, s.dmr_identities, s.max_block_us);
  CHECK(!ev.empty());
  bool saw_identity = false;
  for (const auto& e : ev) {
    CHECK(e.protocol == digital_voice_ui::Protocol::dmr);
    CHECK(e.frequency_hz == 462562500u);
    if (e.evidence == digital_voice_ui::Evidence::identity_valid) {
      saw_identity = true;
      CHECK(e.has_source && e.source_id == 3024109u && e.destination_id == 99u && e.colour_code == 1 && e.colour_code_valid);
    }
  }
  CHECK(saw_identity);
  CHECK(s.verified_ms != 0 && s.candidate);
  // A discontinuity (lost samples upstream) resets decoders and bumps the epoch; nothing bridges across it.
  const uint32_t epoch = s.epoch;
  g_lost += 4096;
  dv_radio::offer_iq(cu8.data(), 8192, 240000);
  CHECK(dv_radio::status().loss_events == 1 && dv_radio::status().epoch == epoch + 1);
  // Another rate is refused, not resampled.
  dv_radio::offer_iq(cu8.data(), 8192, 250000);
  CHECK(dv_radio::status().wrong_rate_blocks == 1);
  dv_radio::stop();
}

void test_worker_matches_inline() {
  // The same synthetic DMR stream through the worker/ring path must give the same verified events as the inline path.
  size_t inline_events = 0;
  for (int pass = 0; pass < 2; ++pass) {
    g_threaded = pass == 1; install_hooks(); g_lost = 0; g_now = 30000;
    CHECK(dv_radio::start());
    CHECK(dv_radio::tune_channel(462562500u));
    g_now += 700;
    feed(to_cu8_240k(dv_test_dmr_iq48()));
    if (g_threaded) { wait_drained(); CHECK(dv_radio::status().worker); }
    const auto ev = drain();
    const auto s = dv_radio::status();
    std::printf("worker=%d events=%zu drops=%u ring_max=%u\n", g_threaded ? 1 : 0, ev.size(), s.ring_drops, s.ring_fill_max);
    CHECK(s.ring_drops == 0);
    if (!g_threaded) inline_events = ev.size(); else CHECK(ev.size() == inline_events && inline_events > 0);
    bool saw = false;
    for (const auto& e : ev) if (e.evidence == digital_voice_ui::Evidence::identity_valid) { saw = true; CHECK(e.source_id == 3024109u && e.destination_id == 99u); }
    CHECK(saw);
    // A discontinuity discards queued samples and resets the decoders without ever waiting.
    g_lost += 10; dv_radio::offer_iq(to_cu8_240k(dv_test_dmr_iq48()).data(), 8192, 240000);
    CHECK(dv_radio::status().loss_events == 1);
    dv_radio::stop();
    CHECK(!dv_radio::running());
  }
  g_threaded = false;
}

void test_nxdn_end_to_end(bool nxdn96) {
  install_hooks(); g_lost = 0; g_now = 5000;
  CHECK(dv_radio::start());
  CHECK(dv_radio::tune_channel(453775000u));
  g_now += 700;
  feed(to_cu8_240k(dv_test_nxdn_iq48(nxdn96, 0)));      // RAN 0 must be a valid decoded value
  auto ev = drain();
  const auto s = dv_radio::status();
  std::printf("nxdn%d: events=%zu n48_ok=%u n96_ok=%u frames48=%u frames96=%u max_us=%u\n", nxdn96 ? 96 : 48, ev.size(),
              s.nxdn48_sacch_valid, s.nxdn96_sacch_valid, s.nxdn48_frames, s.nxdn96_frames, s.max_block_us);
  size_t matching = 0;
  for (const auto& e : ev) {
    if (e.protocol == (nxdn96 ? digital_voice_ui::Protocol::nxdn96 : digital_voice_ui::Protocol::nxdn48)) {
      CHECK(e.ran_valid && e.ran == 0);
      CHECK(!e.has_source && !e.has_destination);        // no inferred identity
      CHECK(e.evidence == digital_voice_ui::Evidence::integrity_valid);
      ++matching;
    }
  }
  CHECK(matching >= 2);
  dv_radio::stop();
}

void test_noise_no_false_identity() {
  install_hooks(); g_lost = 0; g_now = 9000;
  CHECK(dv_radio::start());
  CHECK(dv_radio::tune_channel(462562500u));
  g_now += 700;
  std::mt19937 rng(7);
  std::normal_distribution<float> n(0.f, 0.25f);
  std::vector<float> noise(2 * 48000 * 4);
  for (auto& v : noise) v = n(rng);
  feed(to_cu8_240k(noise, 0.9));
  CHECK(drain().empty());
  CHECK(dv_radio::status().dmr_identities == 0 && dv_radio::status().events == 0);
  dv_radio::stop();
}

void test_no_receiver_and_restart() {
  install_hooks(); g_now = 20000;
  CHECK(dv_radio::start());
  g_accept = false;
  CHECK(!dv_radio::tune_channel(462562500u));
  CHECK(dv_radio::status().tune_refused == 1);
  g_accept = true; g_ready = false;
  CHECK(dv_radio::status().link == dv_radio::Link::no_receiver);
  g_ready = true;
  dv_radio::stop();
  CHECK(dv_radio::status().link == dv_radio::Link::stopped);
  CHECK(dv_radio::start());          // clean restart after stop
  dv_radio::stop();
}
}  // namespace

int main() {
  test_channelizer_tone();
  test_scanner();
  test_scanner_hold_time();
  test_monitor_revisits();
  test_dmr_end_to_end();
  test_worker_matches_inline();
  test_nxdn_end_to_end(false);
  test_nxdn_end_to_end(true);
  test_noise_no_false_identity();
  test_no_receiver_and_restart();
  std::puts("PASS digital voice live chain (synthetic IQ)");
  return 0;
}
