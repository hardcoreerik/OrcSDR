#include "../ui/map_catalog_paging.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
struct Pack { char id[20]{}; bool installed = false; };

template <std::size_t N>
void activate(Pack (&packs)[N], std::size_t index) {
  assert(index < N);
  std::snprintf(packs[index].id, sizeof(packs[index].id), "pack_%zu", index);
}

void run() {
  using namespace orcsdr::map_catalog_paging;
  Pack packs[16]{};
  assert(entry_count(packs) == 0);
  assert(page_count(packs) == 1);
  assert(index_for_row(packs, 0, 0) == -1);
  assert(index_for_row(packs, 1, 0) == -1);
  for (std::size_t i = 0; i < 5; ++i) activate(packs, i);
  assert(page_count(packs) == 1);
  for (std::size_t i = 0; i < 5; ++i)
    assert(index_for_row(packs, 0, i) == static_cast<int>(i));
  assert(index_for_row(packs, 0, 5) == -1);
  activate(packs, 5);
  assert(page_count(packs) == 2);
  assert(index_for_row(packs, 1, 0) == 5);
  for (std::size_t i = 6; i < 16; ++i) activate(packs, i);
  assert(page_count(packs) == 4);
  for (std::size_t i = 0; i < 16; ++i)
    assert(index_for_row(packs, i / 5, i % 5) == static_cast<int>(i));
  assert(index_for_row(packs, 3, 1) == -1);
  assert(index_for_row(packs, 4, 0) == -1);

  // Sparse list: touch actions must still use original catalog slot indices.
  std::memset(packs[2].id, 0, sizeof(packs[2].id));
  std::memset(packs[6].id, 0, sizeof(packs[6].id));
  assert(entry_count(packs) == 14);
  assert(page_count(packs) == 3);
  assert(index_for_row(packs, 0, 2) == 3);
  assert(index_for_row(packs, 1, 0) == 7);
  assert(index_for_row(packs, 2, 3) == 15);
  assert(index_for_row(packs, 2, 4) == -1);
}
}  // namespace

int main() {
  run();
  std::puts("map_catalog_paging_tests PASS");
  return 0;
}
