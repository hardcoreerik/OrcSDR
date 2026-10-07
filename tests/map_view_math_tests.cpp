#include <cstdio>
#include <cstdlib>

#include "map_view_math.hpp"

int main() {
  if (!orcsdr::map_view::self_check()) {
    std::fprintf(stderr, "map_view_math self_check failed\n");
    return EXIT_FAILURE;
  }
  std::puts("map view math tests passed");
  return EXIT_SUCCESS;
}
