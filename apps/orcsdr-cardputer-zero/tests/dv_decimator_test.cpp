// Verifies the anti-alias decimator in front of the DMR channeliser.
//
// The point of the filter is rejection, so the test measures rejection: a tone
// inside the 120 kHz pass band must survive, and a tone that would otherwise fold
// into the channel must be strongly attenuated. Subsampling without this filter is
// exactly what produced "level rises, nothing ever acquires".
#include <cmath>
#include <cstdio>
#include <vector>

#include "dv_decimator.hpp"

namespace {
int failures = 0;
void check(bool ok, const char* what) {
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) ++failures;
}

// Returns the RMS of the decimated output, in CU8 units, for a tone at `hz`
// sampled at 2.4 MS/s.
double run(double hz) {
  constexpr double kRate = 2400000.0;
  const size_t pairs = 240000;  // 100 ms
  std::vector<uint8_t> cu8(pairs * 2);
  for (size_t n = 0; n < pairs; ++n) {
    const double phase = 2.0 * 3.14159265358979323846 * hz * double(n) / kRate;
    cu8[n * 2 + 0] = uint8_t(128 + int(64 * std::cos(phase)));
    cu8[n * 2 + 1] = uint8_t(128 + int(64 * std::sin(phase)));
  }
  orc::zero::dv::CicDecimator decimator;
  decimator.reset();
  // Capacity is counted in IQ pairs, and each pair is two bytes.
  std::vector<uint8_t> out((pairs / 10 + 8) * 2);
  const size_t produced = decimator.process_cu8(cu8.data(), pairs, out.data(), out.size() / 2);
  double sum = 0;
  for (size_t n = 0; n < produced * 2; ++n) {
    const double value = double(out[n]) - 128.0;
    sum += value * value;
  }
  const double rms = produced ? std::sqrt(sum / double(produced * 2)) : 0.0;
  std::printf("       %8.0f Hz -> %zu pairs, rms %.3f\n", hz, produced, rms);
  return rms;
}
}  // namespace

int main() {
  std::printf("Anti-alias decimator (2.4 MS/s -> 240 kS/s)\n");

  const double in_band = run(50000.0);    // inside the pass band
  const double folds_in = run(700000.0);  // would fold to 220 kHz without filtering
  const double high = run(1150000.0);     // well outside

  check(in_band > 20.0, "a 50 kHz tone passes at close to full amplitude");
  check(in_band > folds_in * 8.0, "a 700 kHz tone is rejected at least 8x (18 dB)");
  check(in_band > high * 8.0, "a 1.15 MHz tone is rejected at least 8x (18 dB)");

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
