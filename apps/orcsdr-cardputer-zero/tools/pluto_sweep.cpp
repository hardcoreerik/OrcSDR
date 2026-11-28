// Measures the Pluto's usable receive range on this unit by sweeping frequency at
// a fixed manual gain and recording the noise floor.
//
// The advertised span is the AD9361's (70 MHz - 6 GHz), but the Pluto carries an
// AD9363. Outside its real range tuning is still accepted and reads back happily,
// so the only honest way to know the edges is to look at what comes out of the ADC.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "pluto_receiver.hpp"

namespace {

// 0.3 s at 960 kS/s is plenty to establish a noise floor.
constexpr int kReadMs = 300;

double floor_at(orc::linux_sdr::PlutoReceiver& rx, std::uint32_t freq_hz, int gain_tenth_db) {
  rx.retune(freq_hz);
  rx.set_tuner_gain_tenth_db(gain_tenth_db);
  std::vector<std::uint8_t> buffer(32768);
  double sum_sq = 0;
  std::uint64_t samples = 0;
  const auto start = std::chrono::steady_clock::now();
  while (true) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();
    if (elapsed >= kReadMs) break;
    const std::size_t got = rx.read_cu8(buffer.data(), buffer.size());
    if (!got) break;
    for (std::size_t i = 0; i + 1 < got; i += 2) {
      const double i_part = double(int(buffer[i]) - 128);
      const double q_part = double(int(buffer[i + 1]) - 128);
      sum_sq += i_part * i_part + q_part * q_part;
      ++samples;
    }
  }
  return samples ? std::sqrt(sum_sq / (2.0 * double(samples))) : 0.0;
}

}  // namespace

int main(int argc, char** argv) {
  const int gain_tenth_db = argc > 1 ? std::atoi(argv[1]) : 400;
  static const std::uint32_t kSteps[] = {
      70000000,  80000000,  90000000,  100000000, 150000000, 200000000,
      250000000, 300000000, 325000000, 350000000, 400000000, 500000000,
      700000000, 900000000, 1200000000, 1500000000, 2000000000, 2400000000,
      2800000000, 3200000000, 3500000000, 3700000000, 3800000000, 3900000000,
      4000000000,
  };

  orc::linux_sdr::PlutoReceiver rx;
  try {
    rx.open();
    rx.configure(kSteps[sizeof(kSteps) / sizeof(kSteps[0]) / 2], 960000);
  } catch (const std::exception& error) {
    std::printf("open failed: %s\n", error.what());
    return 2;
  }
  rx.set_tuner_gain_tenth_db(gain_tenth_db);
  std::printf("manual gain %d tenths dB, %d ms per step\n\n", gain_tenth_db, kReadMs);
  std::printf("%-12s %-10s %s\n", "requested", "tuned", "CU8 rms");
  for (std::uint32_t step : kSteps) {
    double rms = -1.0;
    try {
      rms = floor_at(rx, step, gain_tenth_db);
    } catch (const std::exception&) {
      // Outside the advertised range the driver refuses the write; report that
      // rather than pretending the step produced a noise floor.
      std::printf("%-12u %-10s refused\n", step, "-");
      std::fflush(stdout);
      continue;
    }
    std::printf("%-12u %-10u %.2f\n", step, rx.frequency_hz(), rms);
    std::fflush(stdout);
  }
  rx.close();
  return 0;
}
