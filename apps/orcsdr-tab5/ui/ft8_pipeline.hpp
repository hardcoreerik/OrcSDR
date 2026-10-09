#pragma once

#include "ft8_codec.hpp"
#include "ft8_demod.hpp"
#include "ft8_ldpc_decode.hpp"
#include "ft8_message.hpp"
#include "ft8_snr.hpp"
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
  snr::Calibration snr{};
  uint8_t candidate_limit = 8;
};

struct FrameResult {
  Mode mode = Mode::ft8;
  sync::Candidate candidate{};
  float mean_symbol_contrast = 0.0f;
  uint8_t ldpc_iterations = 0;
  orcsdr::ft8::codec::MessageBits message{};
  orcsdr::ft8::message::StandardMessage standard{};
  bool snr_valid = false;   // set for accepted frames when the SNR estimator could run
  float snr_db = 0.0f;       // 2500 Hz reference bandwidth, see ft8_snr.hpp
};

struct Workspace {
  std::array<sync::Candidate, kMaxCandidates> candidates{};
  orcsdr::ft8::ldpc_decode::Workspace ldpc{};
};

// Where one sync candidate ended in the acceptance gates, in order. Only `accepted` produces a message; every other value
// is a named reason the candidate was dropped (used by the benchmark tools to classify misses, never shown to a user).
enum class Outcome : uint8_t {
  demod_failed,         // soft demodulation could not run (candidate outside the grid, non-finite energy)
  ldpc_failed,          // LDPC did not converge to a codeword
  crc_failed,           // converged to a codeword whose CRC-14 is wrong
  unpack_unsupported,   // CRC valid but the message family is not parsed (e.g. contest message types)
  not_plausible,        // parsed but not a fully renderable, plausible message
  accepted
};
const char* outcome_name(Outcome outcome);

// What the gates saw for one candidate, for diagnostics.
struct CandidateTrace {
  float mean_symbol_contrast = 0.0f;
  uint8_t ldpc_iterations = 0;
  bool ldpc_converged = false;
  bool crc_ok = false;
};

// Runs ONE candidate through soft demod -> LDPC -> CRC -> FT4 payload restoration -> message unpack -> plausibility and
// reports where it stopped. decode_grid() is this function applied to the ranked candidates; the production acceptance
// rule is exactly the same.
Outcome try_candidate(const ModeProfile& profile, const sync::EnergyGrid& grid,
                      const sync::Geometry& geometry, const sync::Candidate& candidate,
                      const Config& config, Workspace* workspace, FrameResult* frame,
                      CandidateTrace* trace = nullptr);

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
