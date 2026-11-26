// OrcSDR NXDN SACCH fragment -> OPAQUE message reassembly (no field layout).
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This module only orders, bounds and validates the *transport* of CRC-valid SACCH fragments that the Decoder already
// extracts. It does NOT interpret the assembled bits: no message type, source/destination/group ID, call type, privacy
// class or CRC is derived here, because the normative NXDN Layer 3 definition (NXDN TS 1-A / CAI) has not been obtained.
// Consumers must treat OpaqueMessage::bits as an uninterpreted bit string.
//
// Evidence basis for the ordering rule (the ONLY protocol-shaped assumption): in the three replayed REA-Results real-air
// recordings the decoder's `structure` field decreases by one (mod 4) on every consecutive 192-dibit traffic frame
// (3,2,1,0,3,...). The default policy follows that observation. It is a configurable policy, not a verified standard
// statement; replace/confirm it against the normative document before relying on message boundaries.
#pragma once
#include "orcsdr_nxdn/decoder.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace orcsdr::nxdn {

inline constexpr std::size_t kSacchFragmentBits = 18;
inline constexpr std::size_t kMaxFragmentsPerMessage = 4; // structure is a 2-bit field
inline constexpr std::size_t kMaxOpaqueBits = kSacchFragmentBits * kMaxFragmentsPerMessage;

enum class FragmentOrder : std::uint8_t { descending, ascending };

struct ReassemblyConfig {
    FragmentOrder order = FragmentOrder::descending; // observed in real air; spec-unconfirmed
    std::uint8_t anchor_structure = 3;               // value that begins a message under the policy above
    bool require_anchor = true;                      // false: start on the first fragment seen (unanchored)
    std::uint8_t fragments = 4;                      // 1..kMaxFragmentsPerMessage
};

// Uninterpreted. bits[i] is 0/1, fragments concatenated in arrival order, each fragment MSB-first as the decoder gives it.
struct OpaqueMessage {
    std::uint64_t first_frame_start_dibit = 0;
    std::uint64_t last_frame_start_dibit = 0;
    Mode mode = Mode::nxdn48;
    std::uint8_t rf_channel = 0;
    std::uint8_t functional_channel = 0;
    std::uint8_t direction = 0;
    std::uint8_t ran = 0;            // CRC6-valid SACCH header field, carried through unchanged
    std::uint8_t first_structure = 0;
    std::uint8_t bit_count = 0;
    bool anchored = true;            // false when require_anchor was off
    std::array<std::uint8_t, kMaxOpaqueBits> bits{};
};

struct ReassemblyCounters {
    std::uint64_t fragments_accepted = 0;
    std::uint64_t messages_completed = 0;
    std::uint64_t waiting_for_anchor = 0;   // fragment ignored: not the anchor, nothing in progress
    std::uint64_t dropped_gap = 0;          // frame spacing != 192 dibits; partial discarded
    std::uint64_t dropped_out_of_order = 0; // structure not the successor; partial discarded
    std::uint64_t dropped_context_change = 0; // RAN / RFCT / FCT / direction / mode changed; partial discarded
    std::uint64_t rejected_malformed = 0;   // structure > 3, bit != 0/1, bad mode, non-forward frame position
};

using OpaqueSink = void (*)(const OpaqueMessage&, void*) noexcept;

// Fixed memory, no allocation, no I/O. Not thread-safe. Call reset() on retune / RF epoch / decoder reset.
class SacchReassembler final {
public:
    explicit SacchReassembler(ReassemblyConfig config = {}) noexcept;
    [[nodiscard]] bool valid_config() const noexcept { return config_ok_; }
    void reset() noexcept;
    // Returns false (nothing consumed, state unchanged) for an invalid configuration. Malformed events are counted and
    // discard any partial message but still return true.
    [[nodiscard]] bool feed(const Event& event, OpaqueSink sink, void* user = nullptr) noexcept;
    [[nodiscard]] ReassemblyCounters counters() const noexcept { return counters_; }
    [[nodiscard]] std::uint8_t fragments_pending() const noexcept { return have_; }
private:
    void drop() noexcept { have_ = 0; }
    ReassemblyConfig cfg_;
    bool config_ok_ = false;
    std::uint8_t have_ = 0;
    OpaqueMessage msg_{};
    std::uint8_t expect_structure_ = 0;
    ReassemblyCounters counters_{};
};

} // namespace orcsdr::nxdn
