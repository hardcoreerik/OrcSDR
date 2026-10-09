#include "../apps/orcsdr-tab5/ui/ft8_codec.hpp"
#include "../apps/orcsdr-tab5/ui/ft8_ldpc.hpp"
#include "../apps/orcsdr-tab5/ui/ft8_ldpc_decode.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {
using namespace orcsdr::ft8;

struct Rng {
  uint32_t state = 0x83d2e41bu;
  uint32_t next() {
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return state;
  }
  float uniform() { return static_cast<float>((next() >> 8u) + 1u) / 16777217.0f; }
  float normal() {
    const float a = uniform();
    const float b = uniform();
    return std::sqrt(-2.0f * std::log(a)) * std::cos(6.28318530718f * b);
  }
};

struct Score {
  int converged = 0;
  int wrong_codeword = 0;
  uint64_t iterations = 0;
};

Score run(float sigma, float normalization, int trials) {
  Rng rng{};
  rng.state ^= static_cast<uint32_t>(sigma * 1000.0f);
  Score score{};
  ldpc_decode::Config config{};
  config.normalization = normalization;

  for (int trial = 0; trial < trials; ++trial) {
    codec::MessageBits message{};
    for (auto& bit : message) bit = static_cast<uint8_t>(rng.next() >> 31u);
    const auto codeword = ldpc::encode(message);
    ldpc_decode::LlrVector llr{};
    for (std::size_t i = 0; i < llr.size(); ++i) {
      const float symbol = codeword[i] ? -1.0f : 1.0f;
      const float received = symbol + sigma * rng.normal();
      llr[i] = 2.0f * received / (sigma * sigma);
    }
    ldpc_decode::Workspace workspace{};
    ldpc_decode::Result result{};
    if (!ldpc_decode::decode(llr, &workspace, &result, config)) return {-1, -1, 0};
    score.iterations += result.iterations;
    if (result.converged) {
      ++score.converged;
      if (result.message != message) ++score.wrong_codeword;
    }
  }
  return score;
}

}  // namespace

int main() {
  constexpr int kTrials = 2000;
  constexpr float kSigmas[] = {0.75f, 0.85f, 0.95f};
  constexpr float kNormalizations[] = {0.70f, 0.75f, 0.80f, 0.85f};
  std::printf("FT8 LDPC NMS host benchmark -- deterministic BPSK/AWGN, %d trials/cell\n", kTrials);
  std::printf("NOTE: 'wrong' means LDPC converged to another valid codeword; CRC is not part of this layer.\n");
  const auto started = std::chrono::steady_clock::now();
  for (float sigma : kSigmas) {
    for (float normalization : kNormalizations) {
      const Score score = run(sigma, normalization, kTrials);
      if (score.converged < 0) return 2;
      std::printf("sigma=%.2f alpha=%.2f converged=%d/%d wrong=%d avg_iter=%.2f\n",
                  sigma, normalization, score.converged, kTrials,
                  score.wrong_codeword,
                  static_cast<double>(score.iterations) / kTrials);
    }
  }
  const auto ended = std::chrono::steady_clock::now();
  const double seconds = std::chrono::duration<double>(ended - started).count();
  std::printf("elapsed_seconds=%.3f workspace_bytes=%zu\n",
              seconds, sizeof(ldpc_decode::Workspace));
  return 0;
}
