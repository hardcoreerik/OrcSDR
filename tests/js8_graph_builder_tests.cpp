#include "../tools/js8-graph-builder.hpp"

#include <cassert>
#include <cstdio>
#include <vector>

int main() {
  using namespace orcsdr::js8::reconstruct;

  const std::vector<std::vector<uint16_t>> checks{
      {0,1}, {1,2}, {2,3}};
  GraphData graph{};
  assert(build_graph(checks, 4, &graph));

  assert((graph.check_offsets == std::vector<uint16_t>{0,2,4,6}));
  assert((graph.check_variables ==
          std::vector<uint16_t>{0,1,1,2,2,3}));
  assert((graph.variable_offsets ==
          std::vector<uint16_t>{0,1,3,5,6}));
  assert((graph.variable_edges ==
          std::vector<uint16_t>{0,1,2,3,4,5}));

  GraphData invalid{};
  assert(!build_graph({{0,0}}, 4, &invalid));
  assert(!build_graph({{4}}, 4, &invalid));
  assert(!build_graph({}, 4, &invalid));

  std::puts("JS8 graph builder tests: PASS");
  return 0;
}
