#pragma once

#include "ft8_codec.hpp"
#include "ft8_demod.hpp"
#include "ft8_ldpc_decode.hpp"
#include "ft8_sync.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace orcsdr::ftx::pipeline {

constexpr std::size_t kMaxCandidates = 16;

struct Config {
  sync::SearchConfig search{};
  demod::Config demod{};
  orcsdr::ft8::ldpc_decode::Config ldpc{};
  uint8_t candidate_limit = 8;
};

struct FrameResult {
  Mode mode = Mode::ft8;
  sync::Candidate candidate{};
  float mean_symbol_contrast = 0.0f;
  uint8_t ldpc_iterations = 0;
  orcsdr::ft8::codec::MessageBits message{};
};

struct Workspace {
  std::array<sync::Candidate, kMaxCandidates> candidates{};
  orcsdr::ft8::ldpc_decode::Workspace ldpc{};
};

// Runs the already-built receive stages on one spectral grid.
//
// Current acceptance gate:
//   sync candidate -> soft demod -> LDPC parity convergence -> CRC-14.
//
// This returns internal CRC-valid frames only. It does NOT produce the UI's
// orcsdr::ft8::Decode record because full 77-bit message unpack/plausibility
// is a later mandatory truth gate.
//
// FT8 is supported now. FT4 sync/demod are ready, but the pipeline refuses FT4
// until its payload XOR restoration is implemented and tested.
std::size_t decode_grid(const ModeProfile& profile, const sync::EnergyGrid& grid,
                        const sync::Geometry& geometry, const Config& config,
                        Workspace* workspace, FrameResult* output,
                        std::size_t capacity);

bool self_check();

}  // namespace orcsdr::ftx::pipeline
