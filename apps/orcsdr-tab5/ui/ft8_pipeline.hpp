#pragma once

#include "ft8_codec.hpp"
#include "ft8_demod.hpp"
#include "ft8_ldpc_decode.hpp"
#include "ft8_message.hpp"
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
  orcsdr::ft8::message::StandardMessage standard{};
};

struct Workspace {
  std::array<sync::Candidate, kMaxCandidates> candidates{};
  orcsdr::ft8::ldpc_decode::Workspace ldpc{};
};

// Runs the already-built receive stages on one spectral grid.
//
// Current acceptance gate:
//   sync candidate -> soft demod -> LDPC parity convergence -> CRC-14
//   -> supported source-message unpack -> fully renderable/plausible message.
//
// This still returns an internal FrameResult rather than the UI's Decode
// record. Timing/SNR/provenance and backend binding remain separate concerns.
//
// FT8 and FT4 share the CRC/LDPC/message family. FT4's protocol-defined
// payload XOR is restored only after CRC validation and before source-message
// unpacking. JS8 remains outside this pipeline until its separate FEC/frame
// family is independently implemented.
std::size_t decode_grid(const ModeProfile& profile, const sync::EnergyGrid& grid,
                        const sync::Geometry& geometry, const Config& config,
                        Workspace* workspace, FrameResult* output,
                        std::size_t capacity);

bool self_check();

}  // namespace orcsdr::ftx::pipeline
