#include "shortwave_ssb.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace orcsdr::shortwave_ssb;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      std::printf("FAIL line %d: %s\n", __LINE__, #cond);             \
      ++g_failures;                                                   \
    }                                                                 \
  } while (0)

constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 960000.0;

// A single complex tone at `offset_hz` from the tuned frequency (negative = below), as 8-bit unsigned IQ.
std::vector<uint8_t> tone_iq(double offset_hz, double seconds, double amplitude = 40.0) {
  const size_t n = static_cast<size_t>(kRate * seconds);
  std::vector<uint8_t> iq(n * 2);
  for (size_t k = 0; k < n; ++k) {
    const double ph = 2.0 * kPi * offset_hz * static_cast<double>(k) / kRate;
    iq[2 * k] = static_cast<uint8_t>(std::lround(128.0 + amplitude * std::cos(ph)));
    iq[2 * k + 1] = static_cast<uint8_t>(std::lround(128.0 + amplitude * std::sin(ph)));
  }
  return iq;
}

std::vector<float> run(const std::vector<uint8_t>& iq, Mode sideband, int32_t clar = 0) {
  Demod d{};
  std::vector<float> out;
  process(d, iq.data(), iq.size(), 4, sideband, clar, static_cast<float>(kRate),
          [&](float s) { out.push_back(s); });
  return out;
}

// Power of the audio at `hz` (Goertzel over the second half, so filter start-up is excluded).
double power_at(const std::vector<float>& audio, double hz) {
  const size_t start = audio.size() / 2;
  const double w = 2.0 * kPi * hz / 48000.0;
  double re = 0, im = 0;
  for (size_t k = start; k < audio.size(); ++k) {
    re += audio[k] * std::cos(w * static_cast<double>(k));
    im -= audio[k] * std::sin(w * static_cast<double>(k));
  }
  const double n = static_cast<double>(audio.size() - start);
  return (re * re + im * im) / (n * n);
}

double db(double ratio) { return 10.0 * std::log10(ratio + 1e-30); }

}  // namespace

int main() {
  // Band-plan suggestion: the 14.300 MHz maritime net is USB; 40 m is LSB; 60 m is the USB exception;
  // broadcast stays AM.
  CHECK(suggest(14300000) == Mode::usb);
  CHECK(suggest(7200000) == Mode::lsb);
  CHECK(suggest(3850000) == Mode::lsb);
  CHECK(suggest(1900000) == Mode::lsb);
  CHECK(suggest(5357000) == Mode::usb);
  CHECK(suggest(21300000) == Mode::usb);
  CHECK(suggest(10120000) == Mode::usb);
  CHECK(suggest(9650000) == Mode::am);
  CHECK(suggest(6010000) == Mode::am);
  CHECK(resolve(Choice::automatic, 14300000) == Mode::usb);
  CHECK(resolve(Choice::lsb, 14300000) == Mode::lsb);
  CHECK(resolve(Choice::am, 14300000) == Mode::am);
  CHECK(next_choice(Choice::automatic) == Choice::am);
  CHECK(next_choice(Choice::lsb) == Choice::automatic);

  // Direct-sampling fold above 14.4 MHz swaps the sidebands; AM is untouched.
  CHECK(!spectrum_inverted(14300000));
  CHECK(spectrum_inverted(21300000));
  CHECK(demod_sideband(Mode::usb, 14300000) == Mode::usb);
  CHECK(demod_sideband(Mode::usb, 21300000) == Mode::lsb);
  CHECK(demod_sideband(Mode::lsb, 28400000) == Mode::usb);
  CHECK(demod_sideband(Mode::am, 28400000) == Mode::am);

  CHECK(clamp_clarifier(5000) == kClarifierLimitHz);
  CHECK(clamp_clarifier(-5000) == -kClarifierLimitHz);
  CHECK(clamp_clarifier(100) == 100);

  // A tone 1 kHz above the dial is wanted USB audio at 1 kHz, and is rejected by the LSB demodulator.
  const auto above = tone_iq(+1000.0, 0.5);
  const auto below = tone_iq(-1000.0, 0.5);
  const auto usb_on_above = run(above, Mode::usb);
  const auto lsb_on_above = run(above, Mode::lsb);
  const auto lsb_on_below = run(below, Mode::lsb);
  const auto usb_on_below = run(below, Mode::usb);
  CHECK(usb_on_above.size() > 20000);
  const double usb_good = power_at(usb_on_above, 1000.0);
  const double lsb_good = power_at(lsb_on_below, 1000.0);
  CHECK(usb_good > 0.01);
  CHECK(lsb_good > 0.01);
  // Opposite sideband at the same 1 kHz offset: 1 kHz from the carrier is inside the filter's reject side.
  std::printf("usb wanted=%.1f dB  unwanted=%.1f dB\n", db(usb_good), db(power_at(lsb_on_above, 1000.0)));
  CHECK(db(usb_good / (power_at(lsb_on_above, 1000.0) + 1e-12)) > 30.0);
  CHECK(db(lsb_good / (power_at(usb_on_below, 1000.0) + 1e-12)) > 30.0);
  // Also nothing leaks to the image pitch.
  CHECK(db(usb_good / (power_at(usb_on_above, 2000.0) + 1e-12)) > 30.0);

  // The same wanted level at 2.5 kHz stays within a few dB of 1 kHz (voice passband flatness).
  const auto high = run(tone_iq(+2500.0, 0.5), Mode::usb);
  CHECK(std::fabs(db(power_at(high, 2500.0) / usb_good)) < 3.0);
  // And a signal far outside the voice band is attenuated.
  const auto out_of_band = run(tone_iq(+5000.0, 0.5), Mode::usb);
  CHECK(db(usb_good / (power_at(out_of_band, 5000.0 - 48000.0 * 0) + 1e-12)) > 30.0);

  // Clarifier: raising the dial lowers USB pitch and raises LSB pitch.
  const auto usb_clar = run(above, Mode::usb, +200);
  CHECK(power_at(usb_clar, 800.0) > 0.01);
  const auto lsb_clar = run(below, Mode::lsb, +200);
  CHECK(power_at(lsb_clar, 1200.0) > 0.01);

  // Block boundaries must not change the output (state carried across calls).
  {
    Demod whole{}, split{};
    std::vector<float> a, b;
    process(whole, above.data(), above.size(), 4, Mode::usb, 0, static_cast<float>(kRate), [&](float s) { a.push_back(s); });
    const size_t half = (above.size() / 2) & ~size_t(7);
    process(split, above.data(), half, 4, Mode::usb, 0, static_cast<float>(kRate), [&](float s) { b.push_back(s); });
    process(split, above.data() + half, above.size() - half, 4, Mode::usb, 0, static_cast<float>(kRate), [&](float s) { b.push_back(s); });
    CHECK(a.size() == b.size());
    double worst = 0;
    for (size_t k = 0; k < a.size() && k < b.size(); ++k) worst = std::fmax(worst, std::fabs(a[k] - b[k]));
    std::printf("split worst=%g\n", worst);
    CHECK(worst < 0.05);  // audio peaks near 20: float phasor renormalisation only
  }

  // Bad arguments produce no audio rather than garbage.
  {
    Demod d{};
    size_t n = process(d, above.data(), above.size(), 0, Mode::usb, 0, static_cast<float>(kRate), [](float) {});
    CHECK(n == 0);
    n = process(d, above.data(), above.size(), 4, Mode::am, 0, static_cast<float>(kRate), [](float) {});
    CHECK(n == 0);
  }

  if (g_failures) {
    std::printf("shortwave_ssb_tests: %d FAILED\n", g_failures);
    return 1;
  }
  std::puts("shortwave_ssb_tests: PASS");
  return 0;
}
