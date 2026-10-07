#include <cstdio>
#include <cstdlib>

#include "map_packs.hpp"

int main() {
  if (!orcsdr::map_packs::self_check()) {
    std::fprintf(stderr, "map_packs self_check failed\n");
    return EXIT_FAILURE;
  }
  std::puts("map packs tests passed");
  return EXIT_SUCCESS;
}
