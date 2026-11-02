#include "orcsdr_gfx/dirty_rows.hpp"

#include <array>
#include <cstdint>

using orcsdr::gfx::DirtyRows;

extern "C" __declspec(dllexport) int test_disjoint_plot_and_footer_ranges() {
  DirtyRows<> dirty(568);
  if (!dirty.add(150, 386) || !dirty.add(520, 568) || dirty.size() != 2) return 1;
  int y0 = -1, y1 = -1;
  if (!dirty.take(y0, y1) || y0 != 150 || y1 != 386 || dirty.size() != 1) return 2;
  if (!dirty.take(y0, y1) || y0 != 520 || y1 != 568 || !dirty.empty()) return 3;
  return dirty.take(y0, y1) ? 4 : 0;
}

extern "C" __declspec(dllexport) int test_overlap_adjacency_clipping_and_empty() {
  DirtyRows<> dirty(100);
  if (dirty.add(-20, 0) || dirty.add(100, 140) || dirty.add(24, 24)) return 1;
  if (!dirty.add(-20, 20) || !dirty.add(20, 30) || !dirty.add(29, 40) || !dirty.add(50, 60)) return 2;
  int y0 = -1, y1 = -1;
  if (!dirty.take(y0, y1) || y0 != 0 || y1 != 40) return 3;
  return dirty.take(y0, y1) && y0 == 50 && y1 == 60 && dirty.empty() ? 0 : 4;
}

extern "C" __declspec(dllexport) int test_clear_and_orientation_reset() {
  DirtyRows<> dirty(200);
  dirty.add(40, 60);
  dirty.clear();
  if (!dirty.empty() || dirty.height() != 200) return 1;
  dirty.add(100, 130);
  dirty.reset(50);
  if (!dirty.empty() || dirty.height() != 50) return 2;
  if (!dirty.add(48, 70)) return 3;
  int y0 = 0, y1 = 0;
  return dirty.take(y0, y1) && y0 == 48 && y1 == 50 && dirty.empty() ? 0 : 4;
}

extern "C" __declspec(dllexport) int test_capacity_overflow_preserves_coverage() {
  DirtyRows<3> dirty(100);
  dirty.add(2, 4); dirty.add(12, 14); dirty.add(22, 24);
  if (dirty.size() != 3 || !dirty.add(32, 34) || dirty.size() != 1) return 1;
  int y0 = 0, y1 = 0;
  return dirty.take(y0, y1) && y0 == 2 && y1 == 34 && dirty.empty() ? 0 : 2;
}

extern "C" __declspec(dllexport) int test_randomized_bitmap_coverage() {
  constexpr int height = 257;
  uint32_t random = 0x6d2b79f5u;
  auto next = [&]() { random = random * 1664525u + 1013904223u; return random; };
  for (int round = 0; round < 128; ++round) {
    DirtyRows<> dirty(height);
    std::array<bool, height> requested{};
    for (int i = 0; i < 40; ++i) {
      int begin = static_cast<int>(next() % 297) - 20;
      int end = begin + static_cast<int>(next() % 33);
      dirty.add(begin, end);
      begin = begin < 0 ? 0 : begin;
      end = end > height ? height : end;
      for (int y = begin; y < end; ++y) requested[static_cast<std::size_t>(y)] = true;
    }
    std::array<bool, height> covered{};
    int previous_end = -1, y0 = 0, y1 = 0;
    while (dirty.take(y0, y1)) {
      if (y0 < 0 || y1 > height || y0 >= y1 || y0 < previous_end) return 1;
      for (int y = y0; y < y1; ++y) covered[static_cast<std::size_t>(y)] = true;
      previous_end = y1;
    }
    if (!dirty.empty()) return 2;
    for (int y = 0; y < height; ++y)
      if (requested[static_cast<std::size_t>(y)] && !covered[static_cast<std::size_t>(y)]) return 3;
  }
  return 0;
}
