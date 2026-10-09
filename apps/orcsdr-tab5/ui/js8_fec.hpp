#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace orcsdr::js8::fec {

constexpr size_t kMaxVariables = 174;
constexpr size_t kMaxEdges = 8192;   // the JS8 LDPC(174,87) parity rows are dense: 3920 edges

struct Graph {
  uint16_t variable_count = 0;
  uint16_t check_count = 0;
  uint16_t edge_count = 0;
  const uint16_t* check_offsets = nullptr;
  const uint16_t* check_variables = nullptr;
  const uint16_t* variable_offsets = nullptr;
  const uint16_t* variable_edges = nullptr;
};

struct Config {
  uint8_t max_iterations = 20;
  float normalization = 0.80f;
  float max_abs_llr = 32.0f;
};

struct Workspace {
  float* variable_to_check = nullptr;
  float* check_to_variable = nullptr;
  float* posterior = nullptr;
  size_t edge_capacity = 0;
  size_t variable_capacity = 0;
};

struct Result {
  bool converged = false;
  uint8_t iterations = 0;
  uint16_t unsatisfied_checks = 0;
  std::array<uint8_t, kMaxVariables> codeword{};
};

size_t workspace_bytes(const Graph& graph);
bool bind_workspace(void* memory, size_t bytes, const Graph& graph,
                    Workspace* workspace);
bool decode(const float* channel_llr, size_t llr_count, const Graph& graph,
            Workspace* workspace, Result* result,
            const Config& config = {});
bool graph_valid(const Graph& graph);
bool self_check();

}  // namespace orcsdr::js8::fec
