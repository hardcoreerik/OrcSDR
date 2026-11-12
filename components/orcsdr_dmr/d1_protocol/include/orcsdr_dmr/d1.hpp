// OrcSDR D1: host-only, fixed-memory DMR Tier II burst protocol receiver.
// Independently implemented from ETSI TS 102 361-1 and TS 102 361-2.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "orcsdr_dmr/d0.hpp"
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
    std::uint8_t slot = 0;           // 0 unknown, 1/2 direct sync or CACH-inferred (not CRC backed)
    bool slot_from_cach = false;     // Corrected TACT is weaker than uncorrected.
    std::uint8_t cach_tact_corrections = 0;
    bool cach_present = false;
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
};

using EventSink = void (*)(const Event&, void*) noexcept;

// No dynamic allocation or owned pointers. Input is a sequence of hard-sliced
// DMR dibits 0..3 (MSB transmitted first). Not an IQ or 4FSK demodulator.
class Decoder final {
public:
    // Input is validated atomically; invalid dibits/null pointers change no state.
    bool feed(const std::uint8_t* dibits, std::size_t size,
              EventSink sink, void* user = nullptr) noexcept;
    void reset() noexcept;
    std::uint64_t dibits_consumed() const noexcept { return consumed_; }

private:
    struct Late {
        bool active = false;
        std::uint8_t slot = 0;
        std::uint8_t colour_code = 0;
        std::uint64_t base = 0;
        std::uint8_t stage = 0;
        std::array<std::uint8_t, 128> fragments{};
    };
    static constexpr std::size_t kRingCapacity = 256;
    std::array<std::uint8_t, kRingCapacity> history_{};
    std::uint64_t consumed_ = 0;
    std::array<Late, 3> late_{};  // separate unknown, TS1, TS2 collections

    std::uint8_t at(std::uint64_t index) const noexcept {
        return history_[static_cast<std::size_t>(index % kRingCapacity)];
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
