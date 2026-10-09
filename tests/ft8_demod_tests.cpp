#include "ft8_demod.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <vector>

using namespace orcsdr::ftx;
using namespace orcsdr::ftx::demod;

static uint8_t tone_for_label(const ModeProfile& p, uint8_t label) {
  for (uint8_t tone = 0; tone < p.tone_count; ++tone)
    if (p.tone_bits[tone] == label) return tone;
  return 0xff;
}

static void run_sign_test(Mode mode) {
  const auto& p = profile(mode);
  constexpr std::size_t rows = 120;
  constexpr std::size_t bins = 40;
  std::vector<float> cells(rows * bins, 1.0f);
  const sync::Geometry geometry{1, 1};
  const sync::Candidate candidate{3, 9, 0.9f, 10.0f, 1.0f};
  uint16_t expected_bits = 0;
  uint32_t state = 0x12345678u;

  for (std::size_t b = 0; b < p.data_block_count; ++b) {
    const auto& block = p.data[b];
    for (std::size_t s = 0; s < block.length; ++s) {
      uint8_t label = 0;
      for (uint8_t bit = 0; bit < p.bits_per_tone; ++bit) {
        state = state * 1664525u + 1013904223u;
        label = static_cast<uint8_t>((label << 1) | ((state >> 31) & 1u));
      }
      const uint8_t tone = tone_for_label(p, label);
      assert(tone != 0xff);
      const std::size_t row =
          candidate.start_row + block.first_symbol + s;
      cells[row * bins + candidate.base_bin + tone] = 20.0f;
      expected_bits =
          static_cast<uint16_t>(expected_bits + p.bits_per_tone);
    }
  }
  assert(expected_bits == 174);

  sync::EnergyGrid grid{cells.data(), rows, bins, bins};
  Result result{};
  assert(soft_demodulate(p, grid, geometry, candidate, &result));
  assert(result.bit_count == 174);
  assert(result.mean_symbol_contrast > 1.0f);

  uint16_t index = 0;
  state = 0x12345678u;
  for (std::size_t b = 0; b < p.data_block_count; ++b) {
    const auto& block = p.data[b];
    for (std::size_t s = 0; s < block.length; ++s) {
      (void)s;
      for (uint8_t bit = 0; bit < p.bits_per_tone; ++bit) {
        state = state * 1664525u + 1013904223u;
        const bool one = ((state >> 31) & 1u) != 0;
        assert(one ? result.llr[index] < 0.0f
                   : result.llr[index] > 0.0f);
        ++index;
      }
    }
  }
  assert(index == 174);
}

static void test_equal_tones_are_zero_reliability() {
  const auto& p = profile(Mode::ft8);
  std::vector<float> cells(90 * 32, 2.0f);
  sync::EnergyGrid grid{cells.data(), 90, 32, 32};
  Result result{};
  assert(soft_demodulate(p, grid, sync::Geometry{}, sync::Candidate{},
                         &result));
  for (std::size_t i = 0; i < result.bit_count; ++i)
    assert(std::fabs(result.llr[i]) < 1e-6f);
}

static void test_js8_is_not_enabled() {
  std::vector<float> cells(90 * 32, 1.0f);
  sync::EnergyGrid grid{cells.data(), 90, 32, 32};
  Result result{};
  assert(!soft_demodulate(profile(Mode::js8_normal), grid,
                          sync::Geometry{}, sync::Candidate{}, &result));
}

int main() {
  assert(orcsdr::ftx::demod::self_check());
  run_sign_test(Mode::ft8);
  run_sign_test(Mode::ft4);
  test_equal_tones_are_zero_reliability();
  test_js8_is_not_enabled();
  return 0;
}
