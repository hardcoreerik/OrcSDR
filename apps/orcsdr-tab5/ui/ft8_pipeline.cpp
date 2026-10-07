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
  return implementation_ready(profile.mode) &&
         profile.mode == Mode::ft8 &&
         profile.code_family == CodeFamily::ftx_77_crc14_ldpc174_91 &&
         profile.payload_transform == PayloadTransform::none;
}

}  // namespace

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
    demod::Result soft{};
    if (!demod::soft_demodulate(profile, grid, geometry,
                                workspace->candidates[i], &soft,
                                config.demod))
      continue;

    orcsdr::ft8::ldpc_decode::LlrVector llr{};
    std::copy(soft.llr.begin(), soft.llr.end(), llr.begin());

    orcsdr::ft8::ldpc_decode::Result decoded{};
    if (!orcsdr::ft8::ldpc_decode::decode(
            llr, &workspace->ldpc, &decoded, config.ldpc) ||
        !decoded.converged ||
        !orcsdr::ft8::codec::crc_valid(decoded.message))
      continue;

    FrameResult frame{};
    frame.mode = profile.mode;
    frame.candidate = workspace->candidates[i];
    frame.mean_symbol_contrast = soft.mean_symbol_contrast;
    frame.ldpc_iterations = decoded.iterations;
    frame.message = decoded.message;
    output[accepted++] = frame;
  }

  return accepted;
}

bool self_check() {
  Config config{};
  return config_valid(config) &&
         supported_profile(profile(Mode::ft8)) &&
         !supported_profile(profile(Mode::ft4)) &&
         !supported_profile(profile(Mode::js8_normal));
}

}  // namespace orcsdr::ftx::pipeline
