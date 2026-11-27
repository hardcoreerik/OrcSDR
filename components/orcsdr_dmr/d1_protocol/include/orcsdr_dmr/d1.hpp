// OrcSDR D1: host-only, fixed-memory DMR Tier II burst protocol receiver.
// Independently implemented from ETSI TS 102 361-1 and TS 102 361-2.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "orcsdr_dmr/d0.hpp"
#include "orcsdr_dmr/cach_cadence.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace orcsdr::dmr::d1 {

enum class Evidence : std::uint8_t {
    sync_only,           // A matching 48-bit SYNC is NOT a decoded message.
    slot_type,          // Golay(20,8) code validated; no identity inferred.
    fec_valid,          // BPTC integrity checked, RS/CRC not yet validated.
    check_valid,        // RS(12,9) or CRC-16 validated, but no supported identity.
    identity_valid,     // Full LC group/private radio IDs with integrity gate.
};

enum class IdentityKind : std::uint8_t { none, group, private_call };

struct Event {
    std::uint64_t burst_start_dibit = 0;
    SyncKind sync = SyncKind::bs_data;
    Evidence evidence = Evidence::sync_only;
    std::uint8_t slot = 0;           // 0 unknown; 1/2 from direct SYNC or corroborated CACH.
    bool slot_from_cach = false;     // True ONLY after cadence/embedded-LC validation.
    std::uint8_t cach_tact_corrections = 0; // Tentative TACT Hamming repairs (0 or 1).
    bool cach_present = false;       // Corroborated CACH, NOT arbitrary Hamming prefix.
    CachConfidence cach_confidence = CachConfidence::none;
    bool cach_tact_decoded = false;  // Diagnostic: every 7-bit TACT pattern decodes.
    std::uint8_t cach_candidate_slot = 0; // Untrusted hint; NEVER a confirmed TS.
    std::uint8_t cach_cadence_bursts = 0; // Saturates at kConfirmationBursts (or 5 for late LC).
    bool golay_valid = false;
    std::uint8_t golay_corrected_bits = 0;
    std::uint8_t colour_code = 0;
    std::uint8_t data_type = 0xff;   // ETSI Table 9.22
    bool bptc_valid = false;
    std::uint16_t bptc_corrected_bits = 0;
    bool integrity_valid = false;    // RS or CRC, never a SYNC proxy
    bool late_entry = false;         // False for full LC header/terminator
    IdentityKind identity = IdentityKind::none;
    bool protected_flag = false;
    std::uint8_t flco = 0;
    std::uint8_t feature_id = 0;
    std::uint8_t service_options = 0;
    std::uint32_t destination_id = 0;
    std::uint32_t source_id = 0;
    // 0 = exact ETSI 48-bit sync; 1 = uniquely matched data sync with
    // independently valid Golay, BPTC and RS/CRC. Never promote a fuzzy
    // voice-sync-only hit or an unchecked payload into a decoder event.
    std::uint8_t sync_bit_errors = 0;
    // The 96 information bits (12 bytes, MSB first) of a data burst after BPTC(196,96) decoding, exactly as decoded: the full-LC or CSBK or
    // data-header content including its RS or CRC bytes. Valid only when bptc_valid; integrity_valid says whether the RS/CRC check passed.
    std::array<std::uint8_t, 12> info{};
    bool info_valid = false;
};

using EventSink = void (*)(const Event&, void*) noexcept;

// One complete BS voice superframe (bursts A..F, 18 AMBE+2 frames) whose five EMB fields all decoded.
// This is NOT call qualification: the call manager decides privacy, identity and continuity.
// Payload is the 216 over-the-air voice bits of each burst (108 before + 108 after the 48-bit centre), MSB first,
// i.e. three 72-bit frames per burst, in over-the-air order, before any deinterleave or FEC.
inline constexpr std::size_t kVoiceBursts = 6;
inline constexpr std::size_t kVoiceBurstBytes = 27;
inline constexpr std::size_t kVoiceC0SoftBytes = 18 * 12; // 18 AMBE frames * 24 bits packed 4 bits/bit (low nibble even, high nibble odd)
struct VoiceGroup {
    std::uint64_t start_dibit = 0;     // burst A, same clock as Event::burst_start_dibit
    std::uint8_t slot = 0;             // 1/2 only if >=5 of 6 CACH TACTs were clean AND identical, else 0
    std::uint8_t colour_code = 0;      // identical in all five EMBs (guaranteed)
    bool pi_seen = false;              // any EMB Privacy Indicator bit set: treat the whole group as PROTECTED
    bool lc_valid = false;             // embedded LC (EMB LCSS 1,3,3,2) passed row/column FEC and checksum
    bool has_c0_soft = false;          // true when soft_dibits were supplied via feed_soft()
    std::array<std::uint8_t, 9> lc{};  // valid only when lc_valid
    std::array<std::uint8_t, kVoiceBursts * kVoiceBurstBytes> payload{};
    std::array<std::uint8_t, kVoiceC0SoftBytes> c0_soft{};
};
using VoiceSink = void (*)(const VoiceGroup&, void*) noexcept;
struct VoiceStats {
    std::uint64_t anchors = 0;         // exact BS voice SYNC starts
    std::uint64_t groups = 0;          // delivered superframes
    std::uint64_t abandoned_sync = 0;  // later burst looked like a SYNC (frame boundary, not voice)
    std::uint64_t abandoned_emb = 0;   // EMB QR(16,7,6) not uniquely decodable / PI set / colour code changed
    std::uint64_t embedded_lc_ok = 0, embedded_lc_bad = 0;
};

// No dynamic allocation or owned pointers. Input is a sequence of hard-sliced
// DMR dibits 0..3 (MSB transmitted first). Not an IQ or 4FSK demodulator.
class Decoder final {
public:
    // Input is validated atomically; invalid dibits/null pointers change no state.
    bool feed(const std::uint8_t* dibits, std::size_t size,
              EventSink sink, void* user = nullptr) noexcept;
    // Optional soft-reliability variant: each byte packs (msb_q4 << 4) | lsb_q4 (0..15).
    bool feed_soft(const std::uint8_t* dibits, const std::uint8_t* soft_dibits,
                   std::size_t size, EventSink sink, void* user = nullptr) noexcept;
    void reset() noexcept;
    std::uint64_t dibits_consumed() const noexcept { return consumed_; }
    // Optional. Called from feed() on the same thread.
    void set_voice_sink(VoiceSink sink, void* user) noexcept { voice_sink_ = sink; voice_user_ = user; }
    [[nodiscard]] const VoiceStats& voice_stats() const noexcept { return voice_stats_; }

private:
    struct Late {
        bool active = false;
        std::uint8_t slot = 0;
        std::uint8_t colour_code = 0;
        std::uint64_t base = 0;
        std::uint8_t stage = 0;
        std::uint8_t cach_checks = 0; // Includes clean anchor; checked at each 288-dibit stage.
        std::array<std::uint8_t, 128> fragments{};
    };
    static constexpr std::size_t kRingCapacity = 256;
    std::array<std::uint8_t, kRingCapacity> history_{};
    std::array<std::uint8_t, kRingCapacity> soft_history_{};
    bool last_feed_soft_ = false;
    std::uint64_t consumed_ = 0;
    std::array<Late, 3> late_{};  // separate unknown, TS1, TS2 collections
    CachCadence cach_cadence_{};   // one BS channel sequence, no heap or history growth

    struct Track {
        bool active = false;
        bool pi_seen = false;
        bool fuzzy_anchor = false;
        bool have_emb_cc = false;
        bool has_c0_soft = false;
        std::uint8_t emb_cc = 0;
        std::uint8_t emb_misses = 0;
        std::uint8_t taken = 0;                       // bursts collected so far
        std::uint8_t fixed_slot = 0;                  // 0 for BS (from CACH), 1/2 for direct mode
        std::uint64_t base = 0;
        std::array<std::uint8_t, kVoiceBursts * kVoiceBurstBytes> payload{};
        std::array<std::uint8_t, kVoiceC0SoftBytes> c0_soft{};
        std::array<std::uint8_t, kVoiceBursts> emb{};           // 7-bit info word per burst B..F (index 1..5)
        std::array<bool, kVoiceBursts> emb_valid{};             // whether burst k decoded a clean/repaired QR(16,7)
        std::array<std::uint8_t, kVoiceBursts> slot{};          // 0 = TACT not clean
        std::array<std::uint8_t, kVoiceBursts> slot_repaired{}; // 1-bit repaired TACT hint (corroboration only)
        std::array<std::uint8_t, 128> fragments{};              // embedded LC bits of bursts B..E
    };
    std::array<Track, 3> tracks_{};   // two slots interleave; a third covers a restart before the old track expires
    VoiceSink voice_sink_ = nullptr;
    void* voice_user_ = nullptr;
    VoiceStats voice_stats_{};
    std::uint8_t last_voice_fail_ = 0;
    void begin_voice(std::uint64_t start, std::uint8_t fixed_slot, bool fuzzy_anchor = false) noexcept;
    void scan_voice() noexcept;
    bool take_voice_burst(Track& track, std::uint64_t start, unsigned k) noexcept;
    std::uint8_t at(std::uint64_t index) const noexcept {
        return history_[static_cast<std::size_t>(index % kRingCapacity)];
    }
    std::uint8_t soft_at(std::uint64_t index) const noexcept {
        return soft_history_[static_cast<std::size_t>(index % kRingCapacity)];
    }
    void burst_bits(std::uint64_t start, std::uint8_t (&bits)[264]) const noexcept;
    void scan(EventSink sink, void* user) noexcept;
    void scan_embedded(EventSink sink, void* user) noexcept;
};

// Internal functions deliberately exposed to independent host conformance tests.
// Bits are MSB-first, 0/1 only; 196-bit blocks follow D0 BptcWire packing.
bool decode_slot_type(std::uint32_t wire20, std::uint8_t& colour_code,
                      std::uint8_t& data_type, std::uint8_t& corrected_bits) noexcept;
// Returns false on unresolved row/column parity or invalid reserved bits.
// Corrected bits and extracted 12 octets stay unchanged on failure.
bool decode_bptc(const BptcWire& transmitted, std::array<std::uint8_t, 12>& data,
                 std::uint16_t& corrected_bits) noexcept;
// Checks the RS(12,9) checksum unmasked by ETSI voice LC type mask.
bool check_full_lc(const std::array<std::uint8_t, 12>& lc, std::uint8_t data_type) noexcept;
// 80-bit data + masked 16-bit CRC, for CSBK (3), Data Header (6), USBD (11).
bool check_crc16(const std::array<std::uint8_t, 12>& data, std::uint8_t data_type) noexcept;
// Requires event.integrity_valid=true from prior RS/embedded-LC validation.
// Returns identity only for ETSI standard group (FLCO 0)/unit-to-unit (FLCO 3).
bool parse_identity(const std::uint8_t* lc9, Event& event) noexcept;

} // namespace orcsdr::dmr::d1
