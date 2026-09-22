#pragma once

#include "scan_engine.hpp"
#include "shortwave_model.hpp"

#include <cstddef>
#include <cstdint>

namespace orcsdr::shortwave {

struct Candidate {
  uint32_t frequency_hz = 0;
  float level_dbfs = -120.0f;
};

class Hunt {
 public:
  static constexpr size_t kCapacity = 16;
  bool start(scan::Engine& engine, const BroadcastBand& band,
             uint32_t restore_frequency_hz, uint32_t now_ms);
  void observe(uint32_t frequency_hz, float level_dbfs);
  void clear();
  size_t candidate_count() const { return count_; }
  const Candidate* candidate(size_t index) const;
  uint32_t restore_frequency_hz() const { return restore_frequency_hz_; }

 private:
  void sort();
  Candidate candidates_[kCapacity]{};
  size_t count_ = 0;
  uint32_t restore_frequency_hz_ = 0;
};

bool hunt_self_check();

}  // namespace orcsdr::shortwave
