#include "ft8_codec.hpp"
#include "ft8_ldpc.hpp"
#include "ft8_pipeline.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using orcsdr::ftx::Mode;
using orcsdr::ftx::profile;

void inject_frame(const orcsdr::ftx::ModeProfile& p,
                  const orcsdr::ft8::codec::ChannelTones& tones,
                  std::vector<float>* cells, std::size_t rows,
                  std::size_t bins, std::size_t start_row,
                  std::size_t base_bin) {
  (void)rows;
  std::fill(cells->begin(), cells->end(), 1.0f);
  for (std::size_t symbol = 0; symbol < tones.size(); ++symbol) {
    const std::size_t row = start_row + symbol;
    const std::size_t bin = base_bin + tones[symbol];
    (*cells)[row * bins + bin] = 30.0f;
  }
  // Keep the profile argument explicit in this helper so future FT4 fixtures
  // cannot accidentally reuse FT8 channel geometry.
  assert(p.mode == Mode::ft8);
}

void test_crc_valid_ft8_frame_is_returned() {
  const auto& p = profile(Mode::ft8);
  orcsdr::ft8::codec::PayloadBits payload{};
  uint32_t state = 0x5a17c3e1u;
  for (auto& bit : payload) {
    state = state * 1664525u + 1013904223u;
    bit = static_cast<uint8_t>((state >> 31) & 1u);
  }

  const auto message = orcsdr::ft8::codec::append_crc(payload);
  const auto codeword = orcsdr::ft8::ldpc::encode(message);
  orcsdr::ft8::codec::DataTones data{};
  assert(orcsdr::ft8::codec::codeword_to_data_tones(codeword, &data));
  const auto tones = orcsdr::ft8::codec::frame_data_tones(data);

  constexpr std::size_t rows = 82;
  constexpr std::size_t bins = 24;
  constexpr std::size_t start_row = 2;
  constexpr std::size_t base_bin = 8;
  std::vector<float> cells(rows * bins);
  inject_frame(p, tones, &cells, rows, bins, start_row, base_bin);

  orcsdr::ftx::sync::EnergyGrid grid{cells.data(), rows, bins, bins};
  orcsdr::ftx::pipeline::Config config{};
  config.search.first_start_row = 0;
  config.search.last_start_row_exclusive = 4;
  config.search.first_base_bin = 4;
  config.search.last_base_bin_exclusive = 12;
  config.search.min_score = 0.70f;

  orcsdr::ftx::pipeline::Workspace workspace{};
  orcsdr::ftx::pipeline::FrameResult result[2]{};
  const std::size_t count = orcsdr::ftx::pipeline::decode_grid(
      p, grid, orcsdr::ftx::sync::Geometry{}, config, &workspace,
      result, 2);
  assert(count == 1);
  assert(result[0].mode == Mode::ft8);
  assert(result[0].candidate.start_row == start_row);
  assert(result[0].candidate.base_bin == base_bin);
  assert(result[0].message == message);
}

void test_crc_corrupt_valid_ldpc_word_is_not_returned() {
  const auto& p = profile(Mode::ft8);
  orcsdr::ft8::codec::MessageBits message{};
  for (std::size_t i = 0; i < message.size(); ++i)
    message[i] = static_cast<uint8_t>((i * 7u + 3u) & 1u);
  assert(!orcsdr::ft8::codec::crc_valid(message));

  const auto codeword = orcsdr::ft8::ldpc::encode(message);
  orcsdr::ft8::codec::DataTones data{};
  assert(orcsdr::ft8::codec::codeword_to_data_tones(codeword, &data));
  const auto tones = orcsdr::ft8::codec::frame_data_tones(data);

  constexpr std::size_t rows = 80;
  constexpr std::size_t bins = 20;
  std::vector<float> cells(rows * bins);
  inject_frame(p, tones, &cells, rows, bins, 0, 5);

  orcsdr::ftx::sync::EnergyGrid grid{cells.data(), rows, bins, bins};
  orcsdr::ftx::pipeline::Config config{};
  config.search.first_base_bin = 3;
  config.search.last_base_bin_exclusive = 8;
  config.search.min_score = 0.70f;

  orcsdr::ftx::pipeline::Workspace workspace{};
  orcsdr::ftx::pipeline::FrameResult result{};
  assert(orcsdr::ftx::pipeline::decode_grid(
             p, grid, orcsdr::ftx::sync::Geometry{}, config, &workspace,
             &result, 1) == 0);
}

void test_ft4_not_prematurely_accepted() {
  std::array<float, 105 * 16> cells{};
  orcsdr::ftx::sync::EnergyGrid grid{cells.data(), 105, 16, 16};
  orcsdr::ftx::pipeline::Workspace workspace{};
  orcsdr::ftx::pipeline::FrameResult result{};
  assert(orcsdr::ftx::pipeline::decode_grid(
             profile(Mode::ft4), grid, orcsdr::ftx::sync::Geometry{},
             orcsdr::ftx::pipeline::Config{}, &workspace, &result, 1) == 0);
}

}  // namespace

int main() {
  assert(orcsdr::ftx::pipeline::self_check());
  test_crc_valid_ft8_frame_is_returned();
  test_crc_corrupt_valid_ldpc_word_is_not_returned();
  test_ft4_not_prematurely_accepted();
  return 0;
}
