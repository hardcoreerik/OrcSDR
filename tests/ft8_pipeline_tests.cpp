#include "ft8_codec.hpp"
#include "ft8_ldpc.hpp"
#include "ft8_pipeline.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using orcsdr::ftx::Mode;
using orcsdr::ftx::profile;

void put_bits(orcsdr::ft8::codec::PayloadBits* payload, std::size_t offset,
              std::size_t count, uint32_t value) {
  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t shift = count - 1 - i;
    (*payload)[offset + i] =
        static_cast<uint8_t>((value >> shift) & 1u);
  }
}

uint16_t grid15(const char* text) {
  assert(std::strlen(text) == 4);
  return static_cast<uint16_t>(
      (text[0] - 'A') * 18 * 100 +
      (text[1] - 'A') * 100 +
      (text[2] - '0') * 10 +
      (text[3] - '0'));
}

orcsdr::ft8::codec::PayloadBits standard_payload(const char* first,
                                                  const char* second,
                                                  uint16_t g15,
                                                  uint8_t type = 1) {
  uint32_t c1 = 0, c2 = 0;
  if (std::strcmp(first, "CQ") == 0)
    c1 = 2;
  else
    assert(orcsdr::ft8::codec::encode_standard_callsign(first, &c1));
  assert(orcsdr::ft8::codec::encode_standard_callsign(second, &c2));

  orcsdr::ft8::codec::PayloadBits payload{};
  put_bits(&payload, 0, 28, c1);
  put_bits(&payload, 28, 1, 0);
  put_bits(&payload, 29, 28, c2);
  put_bits(&payload, 57, 1, 0);
  put_bits(&payload, 58, 1, 0);
  put_bits(&payload, 59, 15, g15);
  put_bits(&payload, 74, 3, type);
  return payload;
}

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
  assert(p.mode == Mode::ft8);
}

std::size_t decode_payload(const orcsdr::ft8::codec::PayloadBits& payload,
                           orcsdr::ftx::pipeline::FrameResult* result) {
  const auto& p = profile(Mode::ft8);
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
  return orcsdr::ftx::pipeline::decode_grid(
      p, grid, orcsdr::ftx::sync::Geometry{}, config, &workspace,
      result, 1);
}

void test_plausible_standard_ft8_frame_is_returned() {
  const auto payload = standard_payload("CQ", "K1ABC", grid15("FN42"));
  orcsdr::ftx::pipeline::FrameResult result{};
  assert(decode_payload(payload, &result) == 1);
  assert(result.mode == Mode::ft8);
  assert(result.candidate.start_row == 2);
  assert(result.candidate.base_bin == 8);
  assert(result.standard.fully_renderable);
  assert(std::strcmp(result.standard.text, "CQ K1ABC FN42") == 0);
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

void test_crc_valid_unsupported_message_is_not_returned() {
  auto payload = standard_payload("CQ", "K1ABC", grid15("FN42"));
  put_bits(&payload, 74, 3, 4);  // unsupported i3 family
  orcsdr::ftx::pipeline::FrameResult result{};
  assert(decode_payload(payload, &result) == 0);
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
  test_plausible_standard_ft8_frame_is_returned();
  test_crc_corrupt_valid_ldpc_word_is_not_returned();
  test_crc_valid_unsupported_message_is_not_returned();
  test_ft4_not_prematurely_accepted();
  return 0;
}
