#pragma once
#include <cstdint>
namespace orcsdr::fcc {
// UI-owner-only gate. Ticket source lives for the entire async service
// lifetime, longer than screens, rows and pending worker results.
enum class LookupOutcome : uint8_t { unavailable, pending, no_match, candidates, partial, cancelled, error };
struct LookupKey {
  uint64_t ticket=0, observation=0, package_generation=0, location_generation=0;
  bool operator==(const LookupKey& b) const {
    return ticket==b.ticket && observation==b.observation &&
      package_generation==b.package_generation && location_generation==b.location_generation;
  }
};
class LookupTicketSource {
 public:
  LookupTicketSource() = default;
  LookupTicketSource(const LookupTicketSource&) = delete;
  LookupTicketSource& operator=(const LookupTicketSource&) = delete;
  uint64_t next() { return last_==UINT64_MAX ? 0 : ++last_; }
 private:
  uint64_t last_=0;
};
class LookupGate {
 public:
  explicit LookupGate(LookupTicketSource& tickets):tickets_(tickets) {}
  LookupGate(const LookupGate&) = delete;
  LookupGate& operator=(const LookupGate&) = delete;
  // Stable observation identity, never a row index. No allocation or I/O.
  bool begin(uint64_t observation, uint64_t package_generation, uint64_t location_generation, LookupKey& out) {
    if (!observation || !package_generation) return false;
    const auto ticket=tickets_.next();
    if (!ticket) return false;
    key_={ticket,observation,package_generation,location_generation};
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
  LookupTicketSource& tickets_;
  LookupKey key_{};
  LookupOutcome outcome_=LookupOutcome::unavailable;
};
} // namespace orcsdr::fcc
