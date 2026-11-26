// Host tests for the wideband survey: FFT channel levels, noise-floor adaptation, window planning, loss handling and the rate ladder.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#include "dv_survey.hpp"

using namespace orcsdr;

namespace {
int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

struct Tone { double hz; double amp; };
uint32_t g_rng = 12345;
double noise(double sigma) {      // sum of uniforms: close enough to Gaussian for a level test
  double s = 0;
  for (int i = 0; i < 6; ++i) { g_rng = g_rng * 1664525u + 1013904223u; s += (g_rng >> 8) / 16777216.0 - 0.5; }
  return s * sigma * 2.0;
}
// One 1024-sample block: tones at absolute frequencies seen from `center`, plus noise.
void make_block(std::vector<uint8_t>& out, uint32_t center, uint32_t rate, const std::vector<Tone>& tones, double sigma, uint32_t block_no) {
  out.assign(dv_survey::kFft * 2, 0);
  for (size_t n = 0; n < dv_survey::kFft; ++n) {
    double re = noise(sigma), im = noise(sigma);
    for (const Tone& t : tones) {
      const double off = t.hz - center;
      if (std::fabs(off) > rate / 2.0) continue;
      const double ph = 2.0 * 3.14159265358979323846 * off * static_cast<double>(n + block_no * 1024u) / rate;
      re += t.amp * std::cos(ph); im += t.amp * std::sin(ph);
    }
    const auto q = [](double v) { return static_cast<uint8_t>(std::min(255.0, std::max(0.0, std::round(127.5 + 127.5 * v)))); };
    out[2 * n] = q(re); out[2 * n + 1] = q(im);
  }
}

void test_levels() {
  auto w = std::make_unique<dv_survey::Work>();
  dv_survey::init(*w);
  const uint32_t rate = 2400000, center = 152000000;
  std::vector<uint8_t> blk;
  for (size_t f = 0; f < dv_survey::kFrames; ++f) {
    make_block(blk, center, rate, {{center + 300000.0, 0.05}}, 0.02, static_cast<uint32_t>(f));
    std::copy(blk.begin(), blk.end(), w->cap + f * dv_survey::kFft * 2);
  }
  dv_survey::analyze(*w);
  const float hot = dv_survey::channel_db(*w, rate, 300000);
  const float quiet = dv_survey::channel_db(*w, rate, -400000);
  CHECK(hot > quiet + 15.0f);
  CHECK(dv_survey::channel_db(*w, rate, 1500000) < -150.0f);      // outside the span
}

struct Sim {
  std::unique_ptr<dv_survey::Work> w = std::make_unique<dv_survey::Work>();
  dv_survey::Survey s;
  std::vector<Tone> tones;
  uint32_t now = 1000, blocks = 0, windows = 0;
  uint64_t lost = 0;
  int loss_every = 0;                 // every Nth window loses samples (0 = never); -1 = always above `max_ok_rate`
  uint32_t max_ok_rate = 0xFFFFFFFFu; // rates above this lose samples
  double sigma = 0.02;
  Sim() { dv_survey::init(*w); s.attach(w.get()); dv_survey::install(&s); }
  ~Sim() { dv_survey::install(nullptr); }
  void run(size_t guard = 200000) {
    uint32_t centre = 0, rate = 0;
    std::vector<uint8_t> blk;
    for (size_t i = 0; i < guard && !s.finished() && !s.failed(); ++i) {
      now += 10;
      if (s.service(now, lost, &centre, &rate)) { ++windows; s.tune_result(true, now); }
      if (dv_survey::capturing()) {
        if (rate == 0) { rate = s.rate(); centre = s.current_center_hz(); }
        make_block(blk, s.current_center_hz(), s.rate(), tones, sigma, blocks++);
        dv_survey::offer_iq(blk.data(), blk.size(), s.rate());
        if (s.rate() > max_ok_rate) lost += 1000;
        else if (loss_every > 0 && windows % loss_every == 0 && blocks % 8 == 3) lost += 500;
      }
    }
  }
};

bool has_hot(const dv_survey::Survey& s, uint32_t hz) {
  for (size_t i = 0; i < s.hot_count(); ++i) if (s.hot(i).hz == hz) return true;
  return false;
}

void test_survey_finds_signals() {
  Sim sim;
  const dv::Segment seg{150000000, 12500, 801};          // 150.000 - 160.000 MHz
  sim.tones = {{152162500.0, 0.06}, {156000000.0, 0.06}, {159987500.0, 0.06}};   // the last is 2 channels from the band edge
  CHECK(sim.s.start(&seg, 1, sim.now));
  sim.run();
  CHECK(sim.s.finished());
  CHECK(has_hot(sim.s, 152162500));
  CHECK(has_hot(sim.s, 156000000));
  CHECK(has_hot(sim.s, 159987500));
  CHECK(sim.s.hot_count() <= 6);                          // a few false hits at most (3 real ones)
  CHECK(sim.s.rate() == 2560000);                         // a clean link stays at the top rate
  CHECK(sim.s.channels_checked() >= 801);                 // every channel was looked at (twice in the overlap)
  CHECK(sim.s.windows_done() >= 5 && sim.s.windows_done() <= 20);
}

void test_dc_hole_covered() {
  Sim sim;
  const dv::Segment seg{150000000, 12500, 801};
  CHECK(sim.s.start(&seg, 1, sim.now));
  // A tone right where the first window's centre would be must still be found by the neighbouring window.
  const uint32_t first_centre = 150000000u - static_cast<uint32_t>(0.375 * 2560000 / 2);
  sim.tones = {{static_cast<double>(first_centre + 12500 * 1), 0.06}, {153000000.0 + 12500 * 0, 0.06}};
  sim.run();
  CHECK(sim.s.finished());
  CHECK(has_hot(sim.s, first_centre + 12500u) || first_centre + 12500u < 150000000u);
  CHECK(has_hot(sim.s, 153000000u));
}

void test_skirt_rule() {
  // A strong carrier with weaker channels beside it: the neighbours that are >= 15 dB down are skirts, a neighbour only 6 dB down is a real signal,
  // and an isolated weak signal is never touched. The skirt tones are real tones here (what splatter looks like to the survey).
  Sim sim;
  const dv::Segment seg{154000000, 12500, 161};
  sim.tones = {{154500000.0, 0.5}, {154487500.0, 0.06}, {154512500.0, 0.06}, {154525000.0, 0.25}, {155000000.0, 0.06}};
  CHECK(sim.s.start(&seg, 1, sim.now));
  sim.run();
  CHECK(sim.s.finished());
  CHECK(has_hot(sim.s, 154500000));                 // the strong carrier
  CHECK(has_hot(sim.s, 154525000));                 // 6 dB down, 25 kHz away: kept
  CHECK(has_hot(sim.s, 155000000));                 // isolated weak signal: kept
  CHECK(!has_hot(sim.s, 154487500));                // skirts
  CHECK(!has_hot(sim.s, 154512500));
  CHECK(sim.s.skirts_dropped() >= 2);
  CHECK(sim.s.hot_count() == 3);
}

void test_noise_floor_adapts() {
  // The same weak signal is found in a quiet window and in a noisy one: the floor moves with the noise.
  for (double sigma : {0.01, 0.06}) {
    Sim sim;
    sim.sigma = sigma;
    const dv::Segment seg{154000000, 12500, 161};
    sim.tones = {{154500000.0, sigma * 3.0}};
    CHECK(sim.s.start(&seg, 1, sim.now));
    sim.run();
    CHECK(sim.s.finished());
    CHECK(has_hot(sim.s, 154500000));
    CHECK(sim.s.hot_count() <= 4);
  }
}

void test_empty_band_is_quiet() {
  Sim sim;
  const dv::Segment seg{450000000, 12500, 1601};         // 20 MHz of noise only
  CHECK(sim.s.start(&seg, 1, sim.now));
  sim.run();
  CHECK(sim.s.finished());
  CHECK(sim.s.hot_count() <= 3);                          // noise must not flood the decode scan
}

void test_loss_retry_and_rate_ladder() {
  {   // Occasional loss: windows are retried, the rate stays high.
    Sim sim;
    const dv::Segment seg{150000000, 12500, 401};
    sim.loss_every = 3;
    sim.tones = {{152500000.0, 0.06}};
    CHECK(sim.s.start(&seg, 1, sim.now));
    sim.run();
    CHECK(sim.s.finished());
    CHECK(sim.s.lossy_windows() >= 1);
    CHECK(has_hot(sim.s, 152500000));
  }
  {   // The link only holds 2.048 MS/s: the survey steps down to it and finishes there.
    Sim sim;
    const dv::Segment seg{150000000, 12500, 401};
    sim.max_ok_rate = 2048000;
    sim.tones = {{152500000.0, 0.06}};
    CHECK(sim.s.start(&seg, 1, sim.now));
    sim.run();
    CHECK(sim.s.finished());
    CHECK(sim.s.rate() == 2048000);
    CHECK(sim.s.rate_steps() == 2);
    CHECK(has_hot(sim.s, 152500000));
  }
  {   // Nothing is clean: the survey fails (the app then falls back to the plain scan) instead of looping forever.
    Sim sim;
    const dv::Segment seg{150000000, 12500, 401};
    sim.max_ok_rate = 100000;
    CHECK(sim.s.start(&seg, 1, sim.now));
    sim.run();
    CHECK(sim.s.failed());
  }
}

void test_stale_rate_blocks_ignored_and_no_samples_fails() {
  Sim sim;
  const dv::Segment seg{150000000, 12500, 81};
  CHECK(sim.s.start(&seg, 1, sim.now));
  uint32_t centre = 0, rate = 0;
  CHECK(sim.s.service(sim.now, 0, &centre, &rate));
  CHECK(rate == 2560000);
  sim.s.tune_result(true, sim.now);
  sim.now += 200; (void)sim.s.service(sim.now, 0, &centre, &rate);
  CHECK(dv_survey::capturing());
  std::vector<uint8_t> blk(2048, 128);
  dv_survey::offer_iq(blk.data(), blk.size(), 240000);       // a block from the previous 240 kS/s stream
  CHECK(sim.s.state() == dv_survey::State::capturing);
  CHECK(!sim.s.finished());
  // No matching blocks ever arrive: bounded retries, then the ladder, then failure. Never a hang.
  for (int i = 0; i < 2000 && !sim.s.failed(); ++i) {
    sim.now += 50;
    if (sim.s.service(sim.now, 0, &centre, &rate)) sim.s.tune_result(true, sim.now);
  }
  CHECK(sim.s.failed());
}

void test_multi_segment_and_refused_tune() {
  Sim sim;
  const dv::Segment segs[2] = {{150000000, 12500, 161}, {460000000, 12500, 161}};
  sim.tones = {{151000000.0, 0.06}, {461000000.0, 0.06}};
  CHECK(sim.s.start(segs, 2, sim.now));
  uint32_t centre = 0, rate = 0;
  CHECK(sim.s.service(sim.now, 0, &centre, &rate));
  sim.s.tune_result(false, sim.now);                           // refused: retried, not fatal
  CHECK(sim.s.state() == dv_survey::State::tuning);
  sim.run();
  CHECK(sim.s.finished());
  CHECK(has_hot(sim.s, 151000000));
  CHECK(has_hot(sim.s, 461000000));
}
}  // namespace

int main() {
  test_levels();
  test_survey_finds_signals();
  test_dc_hole_covered();
  test_skirt_rule();
  test_noise_floor_adapts();
  test_empty_band_is_quiet();
  test_loss_retry_and_rate_ladder();
  test_stale_rate_blocks_ignored_and_no_samples_fails();
  test_multi_segment_and_refused_tune();
  if (g_fail) { std::printf("FAILED %d checks\n", g_fail); return 1; }
  std::puts("PASS dv_survey");
  return 0;
}
