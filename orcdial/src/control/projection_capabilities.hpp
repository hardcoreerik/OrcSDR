#pragma once
#include "protocol.hpp"

namespace orc::projection {
// Schema 1: FT8 types 17/18. Global bit, outside dashboard-local bits 0..10.
constexpr uint32_t kFt8V1 = 1u << 31;
constexpr uint32_t kSchemaV1 = 0x46543801u;
constexpr uint32_t kLeaseMs = 3500;
constexpr uint32_t kFreshMs = 60000;
constexpr uint32_t kIntervalMs = 1500;

inline void request(Packet& p) {
  p.capabilities = kFt8V1;
  p.revision = kSchemaV1;
}

// Session owner resets this on every secure generation or sender change.
// Heartbeats have their own replay cursor, never the command/ack cursor.
class Peer {
 public:
  void reset() { *this = {}; }
  void observe(const Packet& p, uint32_t now) {
    if (p.role != Role::dial || p.type != Type::heartbeat || !p.sender || !p.sequence) return;
    if (sender_ != p.sender) { reset(); sender_ = p.sender; }
    if (!newer_sequence(p.sequence, sequence_)) return;
    sequence_ = p.sequence;
    accepted_ = p.capabilities == kFt8V1 && p.revision == kSchemaV1;
    at_ = now;
  }
  bool enabled(uint32_t now) const { return accepted_ && uint32_t(now - at_) < kLeaseMs; }
 private:
  uint32_t sender_ = 0, sequence_ = 0, at_ = 0;
  bool accepted_ = false;
};
} // namespace orc::projection
