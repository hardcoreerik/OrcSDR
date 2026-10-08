#include "ft8_audio_tap.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace orcsdr::ftx::audio_tap;

namespace {

constexpr double kPi = 3.14159265358979323846;

// A complex tone `freq_hz` above (positive) or below (negative) the LO, as 8-bit unsigned I/Q.
std::vector<uint8_t> make_cu8(double freq_hz, double amplitude, uint32_t rate, double seconds, int dc_i = 0, int dc_q = 0) {
  const size_t n = static_cast<size_t>(seconds * rate);
  std::vector<uint8_t> out(2 * n);
  for (size_t k = 0; k < n; ++k) {
    const double ph = 2.0 * kPi * freq_hz * static_cast<double>(k) / rate;
    const long i = std::lround(amplitude * std::cos(ph)) + dc_i + 128;
    const long q = std::lround(amplitude * std::sin(ph)) + dc_q + 128;
    out[2 * k] = static_cast<uint8_t>(std::min(255L, std::max(0L, i)));
    out[2 * k + 1] = static_cast<uint8_t>(std::min(255L, std::max(0L, q)));
  }
  return out;
}

std::vector<int16_t> run(const std::vector<uint8_t>& iq, uint32_t rate) {
  Tap tap;
  assert(begin(&tap, rate));
  std::vector<int16_t> out(max_output_samples(tap, iq.size()));
  const size_t n = process_cu8(&tap, iq.data(), iq.size(), out.data(), out.size());
  out.resize(n);
  return out;
}

// RMS of the second half of the output (settled), and the amplitude of the `freq_hz` component there.
double rms_tail(const std::vector<int16_t>& x) {
  double sum = 0.0;
  const size_t start = x.size() / 2;
  for (size_t i = start; i < x.size(); ++i) sum += static_cast<double>(x[i]) * x[i];
  return std::sqrt(sum / static_cast<double>(x.size() - start));
}

double tone_amplitude(const std::vector<int16_t>& x, double freq_hz) {
  double re = 0.0, im = 0.0;
  const size_t start = x.size() / 2;
  for (size_t i = start; i < x.size(); ++i) {
    const double ph = 2.0 * kPi * freq_hz * static_cast<double>(i) / kOutputRateHz;
    re += x[i] * std::cos(ph);
    im += x[i] * std::sin(ph);
  }
  return 2.0 * std::sqrt(re * re + im * im) / static_cast<double>(x.size() - start);
}

void test_usb_passes_and_lsb_is_rejected(uint32_t rate) {
  const double amp = 60.0;
  const double expected = amp * kOutputScale;
  for (double f : {250.0, 1000.0, 1600.0, 2900.0}) {
    const auto y = run(make_cu8(f, amp, rate, 1.5), rate);
    const double got = tone_amplitude(y, f);
    assert(got > expected * 0.89 && got < expected * 1.12);   // within about 1 dB
    assert(rms_tail(y) < got * 0.75);                         // dominated by the tone
  }
  const double ref = rms_tail(run(make_cu8(1000.0, amp, rate, 1.5), rate));
  for (double f : {-400.0, -1000.0, -2000.0, -3000.0}) {
    const auto y = run(make_cu8(f, amp, rate, 1.5), rate);
    assert(rms_tail(y) < ref * 0.0032);                       // opposite sideband at least 50 dB down
  }
}

void test_block_split_matches_one_call() {
  const uint32_t rate = 2400000;
  const auto iq = make_cu8(1234.0, 40.0, rate, 0.5);
  const auto whole = run(iq, rate);
  Tap tap;
  assert(begin(&tap, rate));
  std::vector<int16_t> pieces;
  size_t pos = 0;
  const size_t sizes[] = {2, 998, 4000, 130, 65536, 12};
  size_t k = 0;
  while (pos < iq.size()) {
    const size_t take = std::min(iq.size() - pos, sizes[k++ % 6]);
    std::vector<int16_t> out(max_output_samples(tap, take));
    const size_t n = process_cu8(&tap, iq.data() + pos, take, out.data(), out.size());
    pieces.insert(pieces.end(), out.begin(), out.begin() + static_cast<long>(n));
    pos += take;
  }
  assert(pieces.size() == whole.size());
  for (size_t i = 0; i < whole.size(); ++i) assert(pieces[i] == whole[i]);
}

void test_dc_offset_is_removed() {
  const uint32_t rate = 240000;
  const auto y = run(make_cu8(1000.0, 60.0, rate, 3.0, 12, -9), rate);
  const double tone = tone_amplitude(y, 1000.0);
  assert(tone > 60.0 * kOutputScale * 0.88);
  // Residual away from the tone (DC leakage near 0 Hz is outside the 200 Hz audio edge): small compared with the tone.
  const double low = tone_amplitude(y, 60.0);
  assert(low < tone * 0.01);
}

void test_output_count_and_rate() {
  Tap tap;
  assert(begin(&tap, 2400000));
  const auto iq = make_cu8(500.0, 30.0, 2400000, 1.0);
  std::vector<int16_t> out(max_output_samples(tap, iq.size()));
  const size_t n = process_cu8(&tap, iq.data(), iq.size(), out.data(), out.size());
  assert(n >= 11990 && n <= 12010);                       // 12 kS/s
  // A too-small output buffer drops samples instead of overrunning.
  Tap small;
  assert(begin(&small, 2400000));
  int16_t tiny[8];
  assert(process_cu8(&small, iq.data(), iq.size(), tiny, 8) == 8);
  assert(!begin(&small, 2048000) && !begin(nullptr, 240000));
}

}  // namespace

int main() {
  assert(self_check());
  test_usb_passes_and_lsb_is_rejected(240000);
  test_usb_passes_and_lsb_is_rejected(2400000);
  test_block_split_matches_one_call();
  test_dc_offset_is_removed();
  test_output_count_and_rate();
  std::puts("ft8_audio_tap_tests: PASS");
  return 0;
}
