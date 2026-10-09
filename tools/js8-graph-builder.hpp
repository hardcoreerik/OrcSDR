#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace orcsdr::js8::reconstruct {

struct GraphData {
  uint16_t variable_count = 0;
  std::vector<uint16_t> check_offsets;
  std::vector<uint16_t> check_variables;
  std::vector<uint16_t> variable_offsets;
  std::vector<uint16_t> variable_edges;
};

inline bool build_graph(const std::vector<std::vector<uint16_t>>& checks,
                        uint16_t variable_count, GraphData* out) {
  if (out == nullptr || variable_count == 0 || checks.empty()) return false;

  GraphData graph{};
  graph.variable_count = variable_count;
  graph.check_offsets.reserve(checks.size() + 1);
  graph.check_offsets.push_back(0);

  for (const auto& input_check : checks) {
    if (input_check.empty()) return false;
    std::vector<uint16_t> check = input_check;
    std::sort(check.begin(), check.end());
    if (std::adjacent_find(check.begin(), check.end()) != check.end())
      return false;
    for (uint16_t variable : check)
      if (variable >= variable_count) return false;

    graph.check_variables.insert(
        graph.check_variables.end(), check.begin(), check.end());
    if (graph.check_variables.size() > 65535u) return false;
    graph.check_offsets.push_back(
        static_cast<uint16_t>(graph.check_variables.size()));
  }

  graph.variable_offsets.assign(
      static_cast<size_t>(variable_count) + 1, 0);
  for (uint16_t variable : graph.check_variables)
    ++graph.variable_offsets[static_cast<size_t>(variable) + 1];

  for (size_t i = 0; i < variable_count; ++i)
    graph.variable_offsets[i + 1] =
        static_cast<uint16_t>(graph.variable_offsets[i + 1] +
                              graph.variable_offsets[i]);

  graph.variable_edges.resize(graph.check_variables.size());
  std::vector<uint16_t> cursor(
      graph.variable_offsets.begin(), graph.variable_offsets.end() - 1);

  for (size_t edge = 0; edge < graph.check_variables.size(); ++edge) {
    const uint16_t variable = graph.check_variables[edge];
    graph.variable_edges[cursor[variable]++] =
        static_cast<uint16_t>(edge);
  }

  *out = std::move(graph);
  return true;
}

}  // namespace orcsdr::js8::reconstruct
