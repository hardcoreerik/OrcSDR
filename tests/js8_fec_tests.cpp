#include "js8_fec.hpp"

#include <array>
#include <cassert>
#include <cstdio>
#include <limits>

namespace {
using namespace orcsdr::js8::fec;

constexpr uint16_t kCheckOffsets[]{0, 2, 4, 6};
constexpr uint16_t kCheckVariables[]{0, 1, 1, 2, 2, 3};
constexpr uint16_t kVariableOffsets[]{0, 1, 3, 5, 6};
constexpr uint16_t kVariableEdges[]{0, 1, 2, 3, 4, 5};

const Graph kGraph{
    4, 3, 6,
    kCheckOffsets, kCheckVariables,
    kVariableOffsets, kVariableEdges};

Workspace bind(std::array<float, 32>* storage) {
  Workspace workspace{};
  assert(bind_workspace(storage->data(),
                        storage->size() * sizeof(float),
                        kGraph, &workspace));
  return workspace;
}
}

int main() {
  using namespace orcsdr::js8::fec;
  assert(self_check());
  assert(graph_valid(kGraph));
  assert(workspace_bytes(kGraph) ==
         (2u * kGraph.edge_count + kGraph.variable_count) * sizeof(float));

  std::array<float, 32> storage{};
  Workspace workspace = bind(&storage);
  Result result{};

  const float clean[]{5.0f, 5.0f, 5.0f, 5.0f};
  assert(decode(clean, 4, kGraph, &workspace, &result));
  assert(result.converged);
  assert(result.iterations == 0);
  assert(result.unsatisfied_checks == 0);

  const float weak_error[]{5.0f, 5.0f, -0.4f, 5.0f};
  workspace = bind(&storage);
  assert(decode(weak_error, 4, kGraph, &workspace, &result));
  assert(result.converged);
  assert(result.iterations > 0);
  assert(result.codeword[0] == 0);
  assert(result.codeword[1] == 0);
  assert(result.codeword[2] == 0);
  assert(result.codeword[3] == 0);

  Config bad{};
  bad.max_iterations = 0;
  assert(!decode(clean, 4, kGraph, &workspace, &result, bad));

  float nan_llr[]{5.0f, 5.0f,
                  std::numeric_limits<float>::quiet_NaN(), 5.0f};
  assert(!decode(nan_llr, 4, kGraph, &workspace, &result));

  Graph broken = kGraph;
  broken.edge_count = 7;
  assert(!graph_valid(broken));

  Workspace too_small{};
  std::array<float, 4> tiny{};
  assert(!bind_workspace(tiny.data(), tiny.size() * sizeof(float),
                         kGraph, &too_small));

  std::puts("JS8 generic FEC decoder tests: PASS");
  return 0;
}
