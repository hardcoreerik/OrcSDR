#pragma once

#include "ft8_codec.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace orcsdr::ft8::ldpc_decode {

constexpr std::size_t kEdgeCount = codec::kCodewordBits * 3;

using LlrVector = std::array<float, codec::kCodewordBits>;

struct Config {
  uint8_t max_iterations = 20;
  float normalization = 0.80f;
  float max_abs_llr = 32.0f;
};

struct Workspace {
  std::array<float, kEdgeCount> variable_to_check{};
  std::array<float, kEdgeCount> check_to_variable{};
  std::array<float, codec::kCodewordBits> posterior{};
};

struct Result {
  bool converged = false;
  uint8_t iterations = 0;
  uint8_t unsatisfied_checks = 0;
  codec::CodewordBits codeword{};
  codec::MessageBits message{};
};

// LLR convention: positive favors bit 0, negative favors bit 1.
// Returns false only for invalid arguments/config/input. A valid call may return
// true with result.converged == false when decoding does not converge.
// converged means only that the 174-bit LDPC word satisfies all parity checks;
// it is NOT an accepted FT8 decode. CRC and message plausibility are later gates.
bool decode(const LlrVector& channel_llr, Workspace* workspace, Result* result,
            const Config& config = {});

bool self_check();

}  // namespace orcsdr::ft8::ldpc_decode
