// DMR D0 host-only protocol primitives. Derived independently from
// ETSI TS 102 361-1 V2.7.1 (2026-05), clauses 9.1.1/9.1.4 and annex B.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace orcsdr::dmr {

// Eight 48-bit voice/data synchronization words from ETSI Table 9.2.
// The MSB is the first transmitted bit; the upper 16 bits are zero.
// MS reverse-channel and the reserved word are intentionally not in this
// voice/data family. A match is not proof of a valid decoded DMR message.
enum class SyncKind : std::uint8_t {
    bs_voice = 0,
    bs_data,
    ms_voice,
    ms_data,
    direct_ts1_voice,
    direct_ts1_data,
    direct_ts2_voice,
    direct_ts2_data,
};

inline constexpr std::size_t kSyncCount = 8;
inline constexpr std::uint64_t kSyncMask48 = 0x0000FFFFFFFFFFFFULL;
inline constexpr std::array<std::uint64_t, kSyncCount> kSyncWords = {{
    0x755FD7DF75F7ULL,
    0xDFF57D75DF5DULL,
    0x7F7D5DD57DFDULL,
    0xD5D7F77FD757ULL,
    0x5D577F7757FFULL,
    0xF7FDD5DDFD55ULL,
    0x7DFFD5F55D5FULL,
    0xD7557F5FF7F5ULL,
}};

constexpr std::uint64_t sync_word(SyncKind kind) noexcept {
    const auto index = static_cast<std::size_t>(kind);
    return index < kSyncCount ? kSyncWords[index] : 0ULL;
}

// CACH: 24 transmitted bits (three octets), big-endian bit ordering:
// bit index zero is bit 7 of octet zero, i.e. TX(23) in ETSI Fig. B.9.
// TACT is AT, TC, LCSS(1), LCSS(0), H(2), H(1), H(0).
using CachWire = std::array<std::uint8_t, 3>;
inline constexpr std::array<std::uint8_t, 7> kCachTactTxPositions = {{
    0, 4, 8, 12, 14, 18, 22,
}};
inline constexpr std::array<std::uint8_t, 17> kCachPayloadTxPositions = {{
    1, 2, 3, 5, 6, 7, 9, 10, 11, 13, 15, 16, 17, 19, 20, 21, 23,
}};

struct CachFields {
    bool access_type = false;
    bool tdma_channel = false;
    std::uint8_t lcss = 0; // 2-bit LC start/stop; NOT an embedded-signalling LCSS decoder
    std::uint32_t payload = 0; // 17 bits, P(16) first, down to P(0)
};

// Information word is AT:TC:LCSS(1):LCSS(0), in bits 3..0.
// Returns the systematic Hamming(7,4,3) TACT codeword, bits 6..0.
std::uint8_t encode_tact(std::uint8_t information) noexcept;

// Corrects the single-bit error assumed by Hamming(7,4,3), if any.
// IMPORTANT: A two-or-more-bit error can miscorrect; no CRC protects TACT.
// Only 7-bit codewords are accepted. Output is unchanged on invalid input.
bool decode_tact(std::uint8_t codeword, std::uint8_t& information,
                 std::uint8_t& corrected_bits) noexcept;

// Preserves 'out' on invalid lcss/payload. No heap, I/O, or platform calls.
bool pack_cach(const CachFields& fields, CachWire& out) noexcept;

// The 17 payload bits are *not* decoded as Short LC. No payload CRC here.
// A correction count of one is a Hamming inference, not proof of RF quality.
bool unpack_cach(const CachWire& wire, CachFields& out,
                 std::uint8_t& corrected_tact_bits) noexcept;

// BPTC(196,96) interleaving only; no row/column Hamming FEC is performed.
// 196 indexed bits, MSB first in each octet, with four unused low bits in
// octet 24. Encoded bit 0 is the extra reserved R(3), conventionally zero.
// Figure B.1 and equation B.1: encoded index i -> TX index (i*181)%196.
// Input/output may alias: the implementation uses at most 25 scratch bytes.
using BptcWire = std::array<std::uint8_t, 25>;
inline constexpr std::size_t kBptcBits = 196;
inline constexpr std::uint8_t kBptcPadMask = 0x0FU;

constexpr std::size_t bptc_tx_index(std::size_t encoded_index) noexcept {
    return (encoded_index * 181U) % kBptcBits;
}
constexpr std::size_t bptc_encoded_index(std::size_t tx_index) noexcept {
    // 13 is the inverse of 181 modulo 196: 13*181 = 12*196 + 1.
    return (tx_index * 13U) % kBptcBits;
}

// Returns false and preserves 'out' when pad bits in 'in' are nonzero.
bool bptc_interleave(const BptcWire& encoded, BptcWire& out) noexcept;
bool bptc_deinterleave(const BptcWire& transmitted, BptcWire& out) noexcept;

} // namespace orcsdr::dmr
