#pragma once
#include <cstdint>
namespace orcsdr::fcc {
// UI-owner-only gate; worker returns this key unchanged in a value-owned message.
// No SD, decoder, allocation, locks, or worker callbacks. One gate per visible association.
enum class LookupOutcome : uint8_t { unavailable, pending, no_match, candidates, partial, cancelled, error };
struct LookupKey {
  uint64_t ticket=0, observation=0, package_generation=0, location_generation=0;
  bool operator==(const LookupKey& b) const {
    return ticket==b.ticket && observation==b.observation &&
      package_generation==b.package_generation && location_generation==b.location_generation;
  }
};
class LookupGate {
 public:
  // observation must be stable within this owner's lifetime, never a row index.
  // Ticket counter does not wrap: exhaust rather than reuse.
  bool begin(uint64_t observation, uint64_t package_generation, uint64_t location_generation, LookupKey& out) {
    if (!observation || !package_generation || next_==UINT64_MAX) return false;
    key_={++next_,observation,package_generation,location_generation};
    outcome_=LookupOutcome::pending;
    out=key_;
    return true;
  }
  bool settle(const LookupKey& key, LookupOutcome result) {
    if (outcome_!=LookupOutcome::pending || !(key==key_)) return false;
    switch(result) {
      case LookupOutcome::no_match: case LookupOutcome::candidates:
      case LookupOutcome::partial: case LookupOutcome::cancelled:
      case LookupOutcome::error: case LookupOutcome::unavailable:
        outcome_=result; return true;
      default: return false;
    }
  }
  void invalidate() { key_={}; outcome_=LookupOutcome::unavailable; }
  LookupOutcome outcome() const { return outcome_; }
  LookupKey key() const { return key_; }
 private:
  uint64_t next_=0;
  LookupKey key_{};
  LookupOutcome outcome_=LookupOutcome::unavailable;
};
} // namespace orcsdr::fcc
