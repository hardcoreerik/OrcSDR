#include "js8_snr.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

int main() {
  using namespace orcsdr::js8;
  assert(snr::self_check());

  const Profile& p = profile(Submode::normal);
  constexpr size_t rows = 79;
  constexpr size_t bins = 36;
  constexpr uint16_t base_bin = 14;

  std::vector<float> cells(rows * bins, 1.0f);
  RawFrame frame{};
  frame.submode = Submode::normal;

  for (size_t symbol = 0; symbol < p.channel_symbols; ++symbol) {
    const uint8_t tone = static_cast<uint8_t>(symbol % 8);
    frame.tones[symbol] = tone;
    cells[symbol * bins + base_bin + tone] = 101.0f;
  }

  sync::EnergyGrid grid{cells.data(), rows, bins, bins};
  sync::Candidate candidate{0, base_bin, 1.0f};
  float estimate_db = 0.0f;
  assert(snr::estimate(p, grid, sync::Geometry{1,1}, candidate,
                       frame, snr::Calibration{}, &estimate_db));
  assert(std::isfinite(estimate_db));
  assert(estimate_db > -20.0f && estimate_db < 10.0f);

  frame.tones[5] = 9;
  assert(!snr::estimate(p, grid, sync::Geometry{1,1}, candidate,
                        frame, snr::Calibration{}, &estimate_db));

  std::puts("JS8 SNR estimator tests: PASS");
  return 0;
}
