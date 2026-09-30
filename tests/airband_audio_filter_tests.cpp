#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "airband_audio_filter.hpp"

namespace {

[[noreturn]] void fail(const char* expression, int line) {
  std::fprintf(stderr, "FAIL line=%d check=%s\n", line, expression);
  std::exit(1);
}
#define CHECK(expression) do { if (!(expression)) fail(#expression, __LINE__); } while (false)

using namespace orcsdr::airband_audio;

double db(double ratio) { return 20.0 * std::log10(ratio); }

// RMS of the steady-state part of a sine pushed through the real process() path.
double measure(float hz, float amplitude = 8000.0f) {
  reset();
  std::vector<int16_t> samples(48000);
  const double step = 2.0 * 3.14159265358979323846 * hz / kSampleRateHz;
  for (size_t i = 0; i < samples.size(); ++i)
    samples[i] = static_cast<int16_t>(amplitude * std::sin(step * static_cast<double>(i)));
  process(samples.data(), samples.size());
  double sum = 0.0, in_sum = 0.0;
  size_t n = 0;
  for (size_t i = 24000; i < samples.size(); ++i, ++n) {   // skip the settling time
    sum += static_cast<double>(samples[i]) * samples[i];
    in_sum += (amplitude * amplitude) / 2.0;
  }
  return std::sqrt(sum / n) / std::sqrt(in_sum / n);
}

void test_speech_band_passes() {
  for (float hz : {500.0f, 1000.0f, 2000.0f}) {
    const double gain = measure(hz);
    CHECK(db(gain) > -2.0 && db(gain) < 1.0);
  }
}

void test_hiss_and_rumble_are_cut() {
  CHECK(db(measure(60.0f)) < -24.0);      // mains hum
  CHECK(db(measure(6000.0f)) < -18.0);    // hiss above speech
  CHECK(db(measure(12000.0f)) < -40.0);
}

void test_dc_is_removed() {
  reset();
  std::vector<int16_t> samples(48000, 5000);
  process(samples.data(), samples.size());
  CHECK(std::abs(samples.back()) < 10);
}

void test_response_matches_processing() {
  for (float hz : {200.0f, 1000.0f, 4000.0f}) {
    const double predicted = response(hz);
    const double measured = measure(hz);
    CHECK(std::fabs(db(predicted) - db(measured)) < 0.5);
  }
}

void test_no_overflow_on_loud_input() {
  reset();
  std::vector<int16_t> samples(4800);
  for (size_t i = 0; i < samples.size(); ++i) samples[i] = (i / 24) % 2 ? 32767 : -32768;
  process(samples.data(), samples.size());   // must not crash or wrap (sanitizers check this)
  process(nullptr, 10);
  process(samples.data(), 0);
}

}  // namespace

int main() {
  test_speech_band_passes();
  test_hiss_and_rumble_are_cut();
  test_dc_is_removed();
  test_response_matches_processing();
  test_no_overflow_on_loud_input();
  std::puts("airband_audio_filter_tests: PASS");
  return 0;
}
