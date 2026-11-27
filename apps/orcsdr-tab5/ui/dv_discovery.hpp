#pragma once
// Persistent system discovery for Digital Voice: a bounded, protocol-agnostic database of what the receiver has actually
// validated. A "system" is (protocol, frequency, area) where area is the DMR colour code or the NXDN RAN. Talkgroups
// (destination IDs) and subscribers (source IDs) hang off a system and exist only when an identity-validated event named them.
// Pure C++, fixed memory, no I/O: the app owns storage and hands serialize()/parse() a buffer. Never stores guessed or
// directory-derived data, never promotes a candidate: record() takes only integrity- or identity-validated observations.
#include <cstddef>
#include <cstdint>

namespace orcsdr::dv {

enum class AreaKind : uint8_t { none = 0, colour_code = 1, ran = 2 };

struct Observation {
  uint8_t protocol = 0;          // digital_voice_ui::Protocol value (stored as-is; the DB does not interpret it)
  uint32_t frequency_hz = 0;
  uint8_t area = 0;              // colour code or RAN
  AreaKind area_kind = AreaKind::none;
  uint8_t evidence = 0;          // 4 = integrity_valid, 5 = identity_valid (digital_voice_ui::Evidence); lower is rejected
  uint8_t slot = 0;              // 0 unknown, 1, 2
  bool has_source = false, has_destination = false, group_call = false;
  uint32_t source_id = 0, destination_id = 0;
  uint32_t utc = 0;              // receiver-provided UTC epoch seconds, 0 = unknown (never guessed)
};

struct SystemRec {
  uint16_t uid = 0;
  uint8_t protocol = 0;
  uint32_t frequency_hz = 0;
  uint8_t area = 0;
  AreaKind area_kind = AreaKind::none;
  uint8_t best_evidence = 0;
  uint8_t slots_seen = 0;        // bit0 = TS1, bit1 = TS2
  uint32_t first_utc = 0, last_utc = 0, count = 0;
  uint32_t seq = 0;              // recency, not persisted as a number (order is persisted)
};
struct TalkgroupRec {
  uint16_t system_uid = 0;
  uint32_t id = 0;
  bool group = true;
  uint32_t first_utc = 0, last_utc = 0, count = 0, seq = 0;
};
struct SubscriberRec {
  uint16_t system_uid = 0;
  uint32_t id = 0;
  uint32_t last_destination = 0;
  bool has_last_destination = false;
  uint32_t first_utc = 0, last_utc = 0, count = 0, seq = 0;
};

constexpr size_t kMaxSystems = 128, kMaxTalkgroups = 128, kMaxSubscribers = 256;   // systems were 32: the oldest were being forgotten
constexpr size_t kMaxSerializedBytes = 40 * 1024;   // upper bound for the text form (checked by a test)

class Discovery {
 public:
  void clear() noexcept;
  // Returns false (and changes nothing) for observations below integrity_valid or without a frequency.
  bool record(const Observation& o) noexcept;
  size_t system_count() const noexcept { return systems_n_; }
  size_t talkgroup_count() const noexcept { return tg_n_; }
  size_t subscriber_count() const noexcept { return sub_n_; }
  const SystemRec& system(size_t i) const noexcept { return systems_[i]; }
  const TalkgroupRec& talkgroup(size_t i) const noexcept { return tgs_[i]; }
  const SubscriberRec& subscriber(size_t i) const noexcept { return subs_[i]; }
  // Per-system tallies (O(n), n is small).
  size_t talkgroups_of(uint16_t uid) const noexcept;
  size_t subscribers_of(uint16_t uid) const noexcept;
  // Index of the system, or SIZE_MAX.
  size_t find_system(uint8_t protocol, uint32_t frequency_hz, uint8_t area, AreaKind kind) const noexcept;
  // Most recently seen first: index into the respective array of the n-th newest entry of a system (SIZE_MAX if none).
  size_t nth_newest_talkgroup(uint16_t uid, size_t n) const noexcept;
  size_t nth_newest_subscriber(uint16_t uid, size_t n) const noexcept;
  size_t nth_newest_system(size_t n) const noexcept;   // n-th most recently seen system, SIZE_MAX if none
  // Changes since the last mark_saved(): lets the app write at most when something moved.
  uint32_t revision() const noexcept { return revision_; }
  // Bumps only when a system, talkgroup or subscriber is added (or the table is replaced): what is worth an SD write now.
  uint32_t structure_revision() const noexcept { return structure_; }
  // Text form "DVDB1". Returns bytes written (excluding the NUL) or 0 when the buffer is too small.
  size_t serialize(char* out, size_t capacity) const noexcept;
  // Replaces the contents only when the whole text parses; otherwise leaves *this untouched and returns false.
  bool parse(const char* text, size_t length) noexcept;

 private:
  SystemRec systems_[kMaxSystems]{};
  TalkgroupRec tgs_[kMaxTalkgroups]{};
  SubscriberRec subs_[kMaxSubscribers]{};
  size_t systems_n_ = 0, tg_n_ = 0, sub_n_ = 0;
  uint16_t next_uid_ = 1;
  uint32_t seq_ = 0, revision_ = 0, structure_ = 0;
  size_t add_system(const Observation& o) noexcept;
  void drop_system(size_t index) noexcept;
};

}  // namespace orcsdr::dv
