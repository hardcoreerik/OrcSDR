#pragma once

#include <cstddef>
#include <cstdint>

// Pure, graphics/ESP-IDF-independent mapping between a UI page/row and
// the fixed catalog slot used by catalog_sync's existing action API.
namespace orcsdr::map_catalog_paging {

inline constexpr std::size_t kRowsPerPage = 5;

template <typename Pack, std::size_t N>
std::size_t entry_count(const Pack (&packs)[N]) {
  std::size_t count = 0;
  for (const auto& pack : packs) {
    if (pack.id[0] != '\0') ++count;
  }
  return count;
}

template <typename Pack, std::size_t N>
std::size_t page_count(const Pack (&packs)[N]) {
  const auto count = entry_count(packs);
  return count == 0 ? 1 : (count + kRowsPerPage - 1) / kRowsPerPage;
}

template <typename Pack, std::size_t N>
int index_for_row(const Pack (&packs)[N], std::size_t page, std::size_t row) {
  if (row >= kRowsPerPage || page >= page_count(packs)) return -1;
  const std::size_t target = page * kRowsPerPage + row;
  std::size_t found = 0;
  for (std::size_t i = 0; i < N; ++i) {
    if (packs[i].id[0] == '\0') continue;
    if (found++ == target) return static_cast<int>(i);
  }
  return -1;
}

}  // namespace orcsdr::map_catalog_paging
