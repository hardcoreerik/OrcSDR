#include "shortwave_hunt.hpp"

#include <algorithm>
#include <cmath>

namespace orcsdr::shortwave {

// Hunt::start drives the device scan engine; on the web the page reads one wide spectrum instead
// (see platform/web/sw_controller.cpp), so start() and the self check are left out of this copy.

void Hunt::observe(uint32_t frequency_hz, float level_dbfs) {
  if (!std::isfinite(level_dbfs)) return;
  for (size_t i = 0; i < count_; ++i) {
    if (candidates_[i].frequency_hz != frequency_hz) continue;
    candidates_[i].level_dbfs = std::max(candidates_[i].level_dbfs, level_dbfs);
    sort();
    return;
  }
  if (count_ < kCapacity) {
    candidates_[count_++] = {frequency_hz, level_dbfs};
  } else if (level_dbfs > candidates_[count_ - 1].level_dbfs) {
    candidates_[count_ - 1] = {frequency_hz, level_dbfs};
  } else {
    return;
  }
  sort();
}

void Hunt::clear() {
  count_ = 0;
  restore_frequency_hz_ = 0;
}

const Candidate* Hunt::candidate(size_t index) const {
  return index < count_ ? &candidates_[index] : nullptr;
}

void Hunt::sort() {
  std::sort(candidates_, candidates_ + count_, [](const Candidate& left,
                                                  const Candidate& right) {
    if (left.level_dbfs != right.level_dbfs) return left.level_dbfs > right.level_dbfs;
    return left.frequency_hz < right.frequency_hz;
  });
}

}  // namespace orcsdr::shortwave
