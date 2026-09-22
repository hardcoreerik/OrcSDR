#include "shortwave_hunt.hpp"

#include <algorithm>
#include <cmath>

namespace orcsdr::shortwave {

bool Hunt::start(scan::Engine& engine, const BroadcastBand& band,
                 uint32_t restore_frequency_hz, uint32_t now_ms) {
  constexpr uint32_t kStepHz = 5000;
  constexpr uint32_t kSettleMs = 220;
  if (band.max_hz < band.min_hz) return false;
  const size_t count = (band.max_hz - band.min_hz) / kStepHz + 1;
  const scan::Plan plan{scan::Mode::frequency_range, nullptr, count, band.min_hz,
                        kStepHz, kSettleMs, true};
  if (!engine.start(plan, restore_frequency_hz, now_ms)) return false;
  clear();
  restore_frequency_hz_ = restore_frequency_hz;
  return true;
}

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

bool hunt_self_check() {
  scan::Engine engine;
  Hunt hunt;
  const BroadcastBand band{"31m", 9400000, 9900000, 9550000};
  if (!hunt.start(engine, band, 9800000, 0)) return false;
  hunt.observe(9500000, -45.0f);
  hunt.observe(9600000, -35.0f);
  return hunt.candidate_count() == 2 &&
         hunt.candidate(0)->frequency_hz == 9600000 &&
         hunt.restore_frequency_hz() == 9800000;
}

}  // namespace orcsdr::shortwave
