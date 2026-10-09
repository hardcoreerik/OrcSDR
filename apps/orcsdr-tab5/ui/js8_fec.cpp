#include "js8_fec.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace orcsdr::js8::fec {
namespace {

bool config_valid(const Config& config) {
  return config.max_iterations > 0 &&
         std::isfinite(config.normalization) &&
         config.normalization > 0.0f &&
         config.normalization <= 1.0f &&
         std::isfinite(config.max_abs_llr) &&
         config.max_abs_llr > 0.0f;
}

float clamp_llr(float value, float limit) {
  return std::clamp(value, -limit, limit);
}

uint16_t syndrome_count(const Graph& graph,
                        const std::array<uint8_t, kMaxVariables>& bits) {
  uint16_t unsatisfied = 0;
  for (size_t check = 0; check < graph.check_count; ++check) {
    uint8_t parity = 0;
    for (size_t edge = graph.check_offsets[check];
         edge < graph.check_offsets[check + 1]; ++edge) {
      parity ^= bits[graph.check_variables[edge]];
    }
    unsatisfied += parity != 0;
  }
  return unsatisfied;
}

void hard_decision(const Graph& graph, const Workspace& workspace,
                   std::array<uint8_t, kMaxVariables>* bits) {
  for (size_t variable = 0; variable < graph.variable_count; ++variable)
    (*bits)[variable] =
        static_cast<uint8_t>(workspace.posterior[variable] < 0.0f);
}

}  // namespace

bool graph_valid(const Graph& graph) {
  if (graph.variable_count == 0 || graph.variable_count > kMaxVariables ||
      graph.check_count == 0 || graph.edge_count == 0 ||
      graph.check_offsets == nullptr || graph.check_variables == nullptr ||
      graph.variable_offsets == nullptr || graph.variable_edges == nullptr)
    return false;

  if (graph.check_offsets[0] != 0 ||
      graph.check_offsets[graph.check_count] != graph.edge_count ||
      graph.variable_offsets[0] != 0 ||
      graph.variable_offsets[graph.variable_count] != graph.edge_count)
    return false;

  for (size_t check = 0; check < graph.check_count; ++check)
    if (graph.check_offsets[check] > graph.check_offsets[check + 1])
      return false;
  for (size_t variable = 0; variable < graph.variable_count; ++variable)
    if (graph.variable_offsets[variable] > graph.variable_offsets[variable + 1])
      return false;

  if (graph.edge_count > kMaxEdges) return false;
  std::array<uint8_t, kMaxEdges / 8> seen{};   // one bit per edge

  for (size_t edge = 0; edge < graph.edge_count; ++edge)
    if (graph.check_variables[edge] >= graph.variable_count) return false;

  for (size_t pos = 0; pos < graph.edge_count; ++pos) {
    const uint16_t edge = graph.variable_edges[pos];
    if (edge >= graph.edge_count || ((seen[edge >> 3] >> (edge & 7u)) & 1u) != 0) return false;
    seen[edge >> 3] = static_cast<uint8_t>(seen[edge >> 3] | (1u << (edge & 7u)));
  }

  for (size_t variable = 0; variable < graph.variable_count; ++variable) {
    for (size_t pos = graph.variable_offsets[variable];
         pos < graph.variable_offsets[variable + 1]; ++pos) {
      const uint16_t edge = graph.variable_edges[pos];
      if (graph.check_variables[edge] != variable) return false;
    }
  }
  return true;
}

size_t workspace_bytes(const Graph& graph) {
  if (!graph_valid(graph)) return 0;
  return (static_cast<size_t>(graph.edge_count) * 2u +
          static_cast<size_t>(graph.variable_count)) *
         sizeof(float);
}

bool bind_workspace(void* memory, size_t bytes, const Graph& graph,
                    Workspace* workspace) {
  if (memory == nullptr || workspace == nullptr || !graph_valid(graph))
    return false;
  const size_t needed = workspace_bytes(graph);
  if (bytes < needed) return false;

  auto* values = static_cast<float*>(memory);
  workspace->variable_to_check = values;
  workspace->check_to_variable = values + graph.edge_count;
  workspace->posterior = values + 2u * graph.edge_count;
  workspace->edge_capacity = graph.edge_count;
  workspace->variable_capacity = graph.variable_count;
  return true;
}

bool decode(const float* channel_llr, size_t llr_count, const Graph& graph,
            Workspace* workspace, Result* result, const Config& config) {
  if (channel_llr == nullptr || workspace == nullptr || result == nullptr ||
      !graph_valid(graph) || !config_valid(config) ||
      llr_count < graph.variable_count ||
      workspace->variable_to_check == nullptr ||
      workspace->check_to_variable == nullptr ||
      workspace->posterior == nullptr ||
      workspace->edge_capacity < graph.edge_count ||
      workspace->variable_capacity < graph.variable_count)
    return false;

  *result = Result{};
  for (size_t variable = 0; variable < graph.variable_count; ++variable)
    if (!std::isfinite(channel_llr[variable])) return false;

  for (size_t variable = 0; variable < graph.variable_count; ++variable) {
    const float channel =
        clamp_llr(channel_llr[variable], config.max_abs_llr);
    workspace->posterior[variable] = channel;
    for (size_t pos = graph.variable_offsets[variable];
         pos < graph.variable_offsets[variable + 1]; ++pos)
      workspace->variable_to_check[graph.variable_edges[pos]] = channel;
  }
  for (size_t edge = 0; edge < graph.edge_count; ++edge)
    workspace->check_to_variable[edge] = 0.0f;

  hard_decision(graph, *workspace, &result->codeword);
  result->unsatisfied_checks = syndrome_count(graph, result->codeword);
  if (result->unsatisfied_checks == 0) {
    result->converged = true;
    return true;
  }

  for (uint16_t iteration = 1; iteration <= config.max_iterations;
       ++iteration) {
    for (size_t check = 0; check < graph.check_count; ++check) {
      const size_t begin = graph.check_offsets[check];
      const size_t end = graph.check_offsets[check + 1];
      if (begin == end) return false;

      float min1 = std::numeric_limits<float>::infinity();
      float min2 = std::numeric_limits<float>::infinity();
      uint16_t min_edge = 0;
      bool negative_parity = false;

      for (size_t edge = begin; edge < end; ++edge) {
        const float value = workspace->variable_to_check[edge];
        negative_parity ^= value < 0.0f;
        const float magnitude = std::fabs(value);
        if (magnitude < min1) {
          min2 = min1;
          min1 = magnitude;
          min_edge = static_cast<uint16_t>(edge);
        } else if (magnitude < min2) {
          min2 = magnitude;
        }
      }
      if (!std::isfinite(min2)) min2 = min1;

      for (size_t edge = begin; edge < end; ++edge) {
        const float incoming = workspace->variable_to_check[edge];
        const bool outgoing_negative =
            negative_parity ^ (incoming < 0.0f);
        const float magnitude =
            config.normalization *
            (edge == min_edge ? min2 : min1);
        workspace->check_to_variable[edge] =
            outgoing_negative ? -magnitude : magnitude;
      }
    }

    for (size_t variable = 0; variable < graph.variable_count; ++variable) {
      float posterior =
          clamp_llr(channel_llr[variable], config.max_abs_llr);
      for (size_t pos = graph.variable_offsets[variable];
           pos < graph.variable_offsets[variable + 1]; ++pos)
        posterior +=
            workspace->check_to_variable[graph.variable_edges[pos]];
      workspace->posterior[variable] =
          clamp_llr(posterior, config.max_abs_llr);
    }

    hard_decision(graph, *workspace, &result->codeword);
    result->iterations = static_cast<uint8_t>(iteration);
    result->unsatisfied_checks = syndrome_count(graph, result->codeword);
    if (result->unsatisfied_checks == 0) {
      result->converged = true;
      return true;
    }

    for (size_t variable = 0; variable < graph.variable_count; ++variable) {
      const float posterior = workspace->posterior[variable];
      for (size_t pos = graph.variable_offsets[variable];
           pos < graph.variable_offsets[variable + 1]; ++pos) {
        const uint16_t edge = graph.variable_edges[pos];
        workspace->variable_to_check[edge] = clamp_llr(
            posterior - workspace->check_to_variable[edge],
            config.max_abs_llr);
      }
    }
  }

  return true;
}

bool self_check() {
  constexpr uint16_t check_offsets[]{0, 2, 4, 6};
  constexpr uint16_t check_variables[]{0, 1, 1, 2, 2, 3};
  constexpr uint16_t variable_offsets[]{0, 1, 3, 5, 6};
  constexpr uint16_t variable_edges[]{0, 1, 2, 3, 4, 5};
  const Graph graph{
      4, 3, 6, check_offsets, check_variables,
      variable_offsets, variable_edges};

  std::array<float, 32> storage{};
  Workspace workspace{};
  if (!bind_workspace(storage.data(), storage.size() * sizeof(float),
                      graph, &workspace))
    return false;

  const float llr[]{4.0f, 4.0f, 4.0f, 4.0f};
  Result result{};
  return decode(llr, 4, graph, &workspace, &result) &&
         result.converged && result.iterations == 0;
}

}  // namespace orcsdr::js8::fec
