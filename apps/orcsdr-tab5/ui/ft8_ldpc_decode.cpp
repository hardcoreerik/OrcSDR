#include "ft8_ldpc_decode.hpp"

#include "ft8_ldpc.hpp"
#include "ft8_ldpc_graph.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace orcsdr::ft8::ldpc_decode {
namespace {

// Decoder-specific check-node adjacency is derived at compile time from the
// one protocol parity graph shared with the encoder/syndrome checker. Edge
// numbering remains variable-major: edge = variable * 3 + local check slot.
constexpr std::array<uint16_t, ldpc::kCheckCount + 1> make_check_offsets() {
  std::array<uint16_t, ldpc::kCheckCount + 1> offsets{};
  for (std::size_t variable = 0; variable < codec::kCodewordBits; ++variable)
    for (uint8_t check : ldpc_graph::kColumnChecks[variable])
      ++offsets[static_cast<std::size_t>(check) + 1];
  for (std::size_t check = 0; check < ldpc::kCheckCount; ++check)
    offsets[check + 1] = static_cast<uint16_t>(offsets[check + 1] + offsets[check]);
  return offsets;
}

constexpr auto kCheckOffsets = make_check_offsets();

constexpr std::array<uint16_t, kEdgeCount> make_check_edges() {
  std::array<uint16_t, kEdgeCount> edges{};
  std::array<uint16_t, ldpc::kCheckCount> cursor{};
  for (std::size_t check = 0; check < ldpc::kCheckCount; ++check)
    cursor[check] = kCheckOffsets[check];
  for (std::size_t variable = 0; variable < codec::kCodewordBits; ++variable) {
    for (std::size_t local = 0; local < 3; ++local) {
      const uint8_t check = ldpc_graph::kColumnChecks[variable][local];
      edges[cursor[check]++] = static_cast<uint16_t>(variable * 3 + local);
    }
  }
  return edges;
}

constexpr auto kCheckEdges = make_check_edges();
static_assert(kCheckOffsets[ldpc::kCheckCount] == kEdgeCount);

float clamp_llr(float value, float limit) {
  return std::clamp(value, -limit, limit);
}

bool config_valid(const Config& config) {
  return config.max_iterations > 0 &&
         std::isfinite(config.normalization) &&
         config.normalization > 0.0f &&
         config.normalization <= 1.0f &&
         std::isfinite(config.max_abs_llr) &&
         config.max_abs_llr > 0.0f;
}

void hard_decision(const std::array<float, codec::kCodewordBits>& values,
                   codec::CodewordBits* bits) {
  for (std::size_t i = 0; i < values.size(); ++i)
    (*bits)[i] = static_cast<uint8_t>(values[i] < 0.0f);
}

uint8_t syndrome_count(const codec::CodewordBits& bits) {
  const std::size_t weight = ldpc::syndrome_weight(ldpc::syndrome(bits));
  return static_cast<uint8_t>(std::min<std::size_t>(weight, 255));
}

void extract_message(const codec::CodewordBits& codeword,
                     codec::MessageBits* message) {
  std::copy_n(codeword.begin(), codec::kMessageBits, message->begin());
}

}  // namespace

bool decode(const LlrVector& channel_llr, Workspace* workspace, Result* result,
            const Config& config) {
  if (workspace == nullptr || result == nullptr || !config_valid(config))
    return false;

  *result = Result{};
  for (float value : channel_llr)
    if (!std::isfinite(value)) return false;

  for (std::size_t variable = 0; variable < codec::kCodewordBits; ++variable) {
    const float channel = clamp_llr(channel_llr[variable], config.max_abs_llr);
    workspace->posterior[variable] = channel;
    for (std::size_t local = 0; local < 3; ++local)
      workspace->variable_to_check[variable * 3 + local] = channel;
  }
  workspace->check_to_variable.fill(0.0f);

  hard_decision(workspace->posterior, &result->codeword);
  result->unsatisfied_checks = syndrome_count(result->codeword);
  if (result->unsatisfied_checks == 0) {
    result->converged = true;
    extract_message(result->codeword, &result->message);
    return true;
  }

  for (uint16_t iteration = 1; iteration <= config.max_iterations; ++iteration) {
    // Check-node update: normalized min-sum. Each check has degree six or seven.
    for (std::size_t check = 0; check < ldpc::kCheckCount; ++check) {
      const std::size_t begin = kCheckOffsets[check];
      const std::size_t end = kCheckOffsets[check + 1];
      float min1 = std::numeric_limits<float>::infinity();
      float min2 = std::numeric_limits<float>::infinity();
      uint16_t min_edge = 0;
      bool negative_parity = false;

      for (std::size_t pos = begin; pos < end; ++pos) {
        const uint16_t edge = kCheckEdges[pos];
        const float value = workspace->variable_to_check[edge];
        negative_parity ^= value < 0.0f;
        const float magnitude = std::fabs(value);
        if (magnitude < min1) {
          min2 = min1;
          min1 = magnitude;
          min_edge = edge;
        } else if (magnitude < min2) {
          min2 = magnitude;
        }
      }

      for (std::size_t pos = begin; pos < end; ++pos) {
        const uint16_t edge = kCheckEdges[pos];
        const float incoming = workspace->variable_to_check[edge];
        const bool outgoing_negative = negative_parity ^ (incoming < 0.0f);
        const float magnitude =
            config.normalization * (edge == min_edge ? min2 : min1);
        workspace->check_to_variable[edge] =
            outgoing_negative ? -magnitude : magnitude;
      }
    }

    // Variable-node update. Degree is exactly three for every code bit.
    for (std::size_t variable = 0; variable < codec::kCodewordBits; ++variable) {
      const std::size_t edge = variable * 3;
      const float channel = clamp_llr(channel_llr[variable], config.max_abs_llr);
      const float posterior = clamp_llr(
          channel + workspace->check_to_variable[edge] +
              workspace->check_to_variable[edge + 1] +
              workspace->check_to_variable[edge + 2],
          config.max_abs_llr);
      workspace->posterior[variable] = posterior;
    }

    hard_decision(workspace->posterior, &result->codeword);
    result->iterations = static_cast<uint8_t>(iteration);
    result->unsatisfied_checks = syndrome_count(result->codeword);
    if (result->unsatisfied_checks == 0) {
      result->converged = true;
      extract_message(result->codeword, &result->message);
      return true;
    }

    for (std::size_t variable = 0; variable < codec::kCodewordBits; ++variable) {
      const std::size_t edge = variable * 3;
      const float posterior = workspace->posterior[variable];
      for (std::size_t local = 0; local < 3; ++local) {
        const std::size_t edge_id = edge + local;
        workspace->variable_to_check[edge_id] = clamp_llr(
            posterior - workspace->check_to_variable[edge_id],
            config.max_abs_llr);
      }
    }
  }

  extract_message(result->codeword, &result->message);
  return true;
}

bool self_check() {
  codec::MessageBits message{};
  for (std::size_t i = 0; i < message.size(); ++i)
    message[i] = static_cast<uint8_t>(((i * 19u + 7u) ^ (i >> 2u)) & 1u);
  const codec::CodewordBits codeword = ldpc::encode(message);
  LlrVector llr{};
  for (std::size_t i = 0; i < llr.size(); ++i)
    llr[i] = codeword[i] ? -8.0f : 8.0f;

  Workspace workspace{};
  Result result{};
  return decode(llr, &workspace, &result) && result.converged &&
         result.iterations == 0 && result.message == message;
}

}  // namespace orcsdr::ft8::ldpc_decode
