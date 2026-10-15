#include "memory_reclaim.hpp"

#include <algorithm>
#include <cstring>

namespace orcsdr::memory_reclaim {
namespace {
Holder g_holders[kMaxHolders];
size_t g_count = 0;

// Insertion sort: at most kMaxHolders entries, and it keeps the compiler's array-bounds analysis happy where std::sort does not.
void sort_largest_first(Report* reports, size_t count) {
  if (count > kMaxHolders) count = kMaxHolders;
  for (size_t i = 1; i < count; ++i) {
    const Report item = reports[i];
    size_t j = i;
    while (j > 0 && reports[j - 1].held < item.held) {
      reports[j] = reports[j - 1];
      --j;
    }
    reports[j] = item;
  }
}
}  // namespace

bool register_holder(const Holder& holder) {
  if (holder.name == nullptr || holder.held_bytes == nullptr || holder.release == nullptr) return false;
  for (size_t i = 0; i < g_count; ++i) {
    if (std::strcmp(g_holders[i].name, holder.name) == 0) {
      g_holders[i] = holder;
      return true;
    }
  }
  if (g_count >= kMaxHolders) return false;
  g_holders[g_count++] = holder;
  return true;
}

size_t list(Report* out, size_t capacity) {
  Report all[kMaxHolders];
  for (size_t i = 0; i < g_count; ++i) {
    all[i].name = g_holders[i].name;
    all[i].held = g_holders[i].held_bytes();
  }
  sort_largest_first(all, g_count);
  const size_t n = out ? std::min(capacity, g_count) : 0;
  for (size_t i = 0; i < n; ++i) out[i] = all[i];
  return n;
}

size_t total_held() {
  size_t total = 0;
  for (size_t i = 0; i < g_count; ++i) total += g_holders[i].held_bytes();
  return total;
}

size_t reclaim_all(Report* out, size_t capacity, size_t* out_count) {
  Report released[kMaxHolders];
  size_t total = 0;
  for (size_t i = 0; i < g_count; ++i) {
    released[i].name = g_holders[i].name;
    released[i].held = g_holders[i].release();
    total += released[i].held;
  }
  sort_largest_first(released, g_count);
  const size_t n = out ? std::min(capacity, g_count) : 0;
  for (size_t i = 0; i < n; ++i) out[i] = released[i];
  if (out_count) *out_count = n;
  return total;
}

void clear_for_tests() { g_count = 0; }

}  // namespace orcsdr::memory_reclaim
