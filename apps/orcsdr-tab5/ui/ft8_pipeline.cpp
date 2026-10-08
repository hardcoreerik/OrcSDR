#include "ft8_pipeline.hpp"

#include <algorithm>
#include <cmath>

namespace orcsdr::ftx::pipeline {
namespace {

bool config_valid(const Config& config) {
  return config.candidate_limit > 0 &&
         config.candidate_limit <= kMaxCandidates;
}

bool supported_profile(const ModeProfile& profile) {
  if (!implementation_ready(profile.mode) ||
      profile.code_family != CodeFamily::ftx_77_crc14_ldpc174_91)
    return false;
  if (profile.mode == Mode::ft8)
    return profile.payload_transform == PayloadTransform::none;
  if (profile.mode == Mode::ft4)
    return profile.payload_transform == PayloadTransform::ft4_xor;
  return false;
}

}  // namespace

const char* outcome_name(Outcome outcome) {
  switch (outcome) {
    case Outcome::demod_failed: return "demod_failed";
    case Outcome::ldpc_failed: return "ldpc_failed";
    case Outcome::crc_failed: return "crc_failed";
    case Outcome::unpack_unsupported: return "unpack_unsupported";
    case Outcome::not_plausible: return "not_plausible";
    case Outcome::accepted: return "accepted";
  }
  return "unknown";
}

Outcome try_candidate(const ModeProfile& profile, const sync::EnergyGrid& grid,
                      const sync::Geometry& geometry, const sync::Candidate& candidate,
                      const Config& config, Workspace* workspace, FrameResult* frame,
                      CandidateTrace* trace) {
  CandidateTrace local{};
  CandidateTrace& seen = trace != nullptr ? *trace : local;
  seen = CandidateTrace{};
  demod::Result soft{};
  if (workspace == nullptr || !demod::soft_demodulate(profile, grid, geometry, candidate, &soft, config.demod))
    return Outcome::demod_failed;
  seen.mean_symbol_contrast = soft.mean_symbol_contrast;

  orcsdr::ft8::ldpc_decode::LlrVector llr{};
  std::copy(soft.llr.begin(), soft.llr.end(), llr.begin());

  orcsdr::ft8::ldpc_decode::Result decoded{};
  if (!orcsdr::ft8::ldpc_decode::decode(llr, &workspace->ldpc, &decoded, config.ldpc) || !decoded.converged)
    return Outcome::ldpc_failed;
  seen.ldpc_iterations = decoded.iterations;
  seen.ldpc_converged = true;
  if (!orcsdr::ft8::codec::crc_valid(decoded.message)) return Outcome::crc_failed;
  seen.crc_ok = true;

  orcsdr::ft8::codec::PayloadBits payload{};
  std::copy_n(decoded.message.begin(), payload.size(), payload.begin());
  if (profile.payload_transform == PayloadTransform::ft4_xor)
    orcsdr::ft8::codec::restore_ft4_payload(&payload);

  orcsdr::ft8::message::StandardMessage standard{};
  if (!orcsdr::ft8::message::unpack_standard(payload, &standard)) return Outcome::unpack_unsupported;
  if (!standard.fully_renderable) return Outcome::not_plausible;

  if (frame != nullptr) {
    *frame = FrameResult{};
    frame->mode = profile.mode;
    frame->candidate = candidate;
    frame->mean_symbol_contrast = soft.mean_symbol_contrast;
    frame->ldpc_iterations = decoded.iterations;
    frame->message = decoded.message;
    frame->standard = standard;
  }
  return Outcome::accepted;
}

std::size_t decode_grid(const ModeProfile& profile, const sync::EnergyGrid& grid,
                        const sync::Geometry& geometry, const Config& config,
                        Workspace* workspace, FrameResult* output,
                        std::size_t capacity) {
  if (workspace == nullptr || output == nullptr || capacity == 0 ||
      !config_valid(config) || !supported_profile(profile))
    return 0;

  const std::size_t candidate_capacity =
      std::min<std::size_t>(config.candidate_limit,
                            workspace->candidates.size());
  const std::size_t candidate_count =
      sync::search(profile, grid, geometry, config.search,
                   workspace->candidates.data(), candidate_capacity);

  std::size_t accepted = 0;
  for (std::size_t i = 0; i < candidate_count && accepted < capacity; ++i) {
    FrameResult frame{};
    if (try_candidate(profile, grid, geometry, workspace->candidates[i], config, workspace, &frame) ==
        Outcome::accepted)
      output[accepted++] = frame;
  }

  return accepted;
}

bool self_check() {
  Config config{};
  return config_valid(config) &&
         supported_profile(profile(Mode::ft8)) &&
         supported_profile(profile(Mode::ft4)) &&
         !supported_profile(profile(Mode::js8_normal));
}

}  // namespace orcsdr::ftx::pipeline
