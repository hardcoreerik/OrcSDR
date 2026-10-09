#include "ft8_mode.hpp"
#include "ft8_spectral.hpp"
#include "ft8_spectral_fft.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace orcsdr::ftx;

namespace {

std::vector<int16_t> make_pcm(size_t n) {
  std::vector<int16_t> x(n);
  uint32_t s = 12345u;
  for (size_t i = 0; i < n; ++i) {
    s = s * 1664525u + 1013904223u;
    double v = (static_cast<double>(static_cast<int32_t>(s >> 16)) - 32768.0) * 0.1;   // noise
    v += 4000.0 * std::sin(2.0 * 3.14159265358979 * 1018.75 * static_cast<double>(i) / 12000.0);
    v += 2500.0 * std::sin(2.0 * 3.14159265358979 * 733.3 * static_cast<double>(i) / 12000.0);
    x[i] = static_cast<int16_t>(v);
  }
  return x;
}

void compare_with_oracle(Mode mode, size_t first_bin, size_t bin_count) {
  const ModeProfile& p = profile(mode);
  const size_t n = p.symbol_samples;
  const size_t hop = n / 2;
  const auto pcm = make_pcm(static_cast<size_t>(12000) * 3);
  const size_t rows = 1 + (pcm.size() - n) / hop;

  spectral_fft::Plan plan;
  spectral_fft::Scratch scratch;
  assert(spectral_fft::make_plan(&plan, n));
  std::vector<float> fast(rows * bin_count);
  assert(spectral_fft::power_rows(plan, &scratch, pcm.data(), pcm.size(), n, hop, first_bin, bin_count, fast.data(), bin_count, rows) ==
         rows);

  // The oracle's bin spacing is the mode's tone spacing: bin j sits at (first_bin + j) * fs / n.
  const double spacing_mhz = 12000.0 * 1000.0 / static_cast<double>(n);
  spectral::Config cfg{};
  cfg.first_bin_millihz = static_cast<uint32_t>(std::llround(spacing_mhz * static_cast<double>(first_bin)));
  cfg.bin_spacing_millihz = static_cast<uint32_t>(std::llround(spacing_mhz));
  cfg.bin_count = static_cast<uint16_t>(bin_count);
  cfg.rows_per_symbol = 2;
  std::vector<float> oracle(rows * bin_count);
  spectral::ReferenceAccumulator acc{};
  assert(spectral::begin(&acc, p, cfg, spectral::OutputGrid{oracle.data(), rows, bin_count}));
  assert(spectral::offer(&acc, pcm.data(), pcm.size()));
  assert(spectral::rows_written(acc) == rows);

  double peak = 0.0;
  for (float v : oracle) peak = std::max(peak, static_cast<double>(v));
  double worst = 0.0;
  for (size_t i = 0; i < fast.size(); ++i) worst = std::max(worst, std::fabs(static_cast<double>(fast[i]) - oracle[i]));
  std::printf("%s: worst error %.3g of peak %.3g (relative %.2e)\n", p.name, worst, peak, worst / peak);
  assert(worst < peak * 2e-3);
}

// A zero-padded 2n-point transform of an n-sample window lands on half-bin frequencies: compare with the oracle at half spacing.
void compare_half_bin_with_oracle() {
  const ModeProfile& p = profile(Mode::ft8);
  const size_t n = p.symbol_samples, hop = n / 4;
  const auto pcm = make_pcm(static_cast<size_t>(12000) * 2);
  const size_t rows = 1 + (pcm.size() - n) / hop;
  spectral_fft::Plan plan;
  spectral_fft::Scratch scratch;
  assert(spectral_fft::make_plan(&plan, 2 * n));
  const size_t first = 64, count = 800;                       // 200 Hz at 3.125 Hz spacing
  std::vector<float> fast(rows * count);
  assert(spectral_fft::power_rows(plan, &scratch, pcm.data(), pcm.size(), n, hop, first, count, fast.data(), count, rows) == rows);
  spectral::Config cfg{};
  cfg.first_bin_millihz = 3125u * first;
  cfg.bin_spacing_millihz = 3125u;
  cfg.bin_count = static_cast<uint16_t>(count);
  cfg.rows_per_symbol = 4;
  std::vector<float> oracle(rows * count);
  spectral::ReferenceAccumulator acc{};
  assert(spectral::begin(&acc, p, cfg, spectral::OutputGrid{oracle.data(), rows, count}));
  assert(spectral::offer(&acc, pcm.data(), pcm.size()));
  double peak = 0.0, worst = 0.0;
  for (float v : oracle) peak = std::max(peak, static_cast<double>(v));
  for (size_t i = 0; i < fast.size(); ++i) worst = std::max(worst, std::fabs(static_cast<double>(fast[i]) - oracle[i]));
  std::printf("half-bin: relative error %.2e\n", worst / peak);
  assert(worst < peak * 1e-3);
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  assert(spectral_fft::self_check());
  compare_with_oracle(Mode::ft8, 32, 449);   // 200-3000 Hz
  compare_with_oracle(Mode::ft4, 10, 135);   // about 208-3020 Hz
  compare_half_bin_with_oracle();
  std::puts("ft8_spectral_fft_tests: PASS");
  return 0;
}
