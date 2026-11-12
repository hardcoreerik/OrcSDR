// DMR D0 host-only protocol primitives. Derived from ETSI TS 102 361-1.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orcsdr_dmr/d0.hpp"

namespace orcsdr::dmr {
namespace {

inline bool bit_at(const std::uint8_t* bytes, std::size_t index) noexcept {
    return ((bytes[index / 8U] >> (7U - (index % 8U))) & 1U) != 0U;
}

inline void set_bit(std::uint8_t* bytes, std::size_t index, bool bit) noexcept {
    const auto mask = static_cast<std::uint8_t>(1U << (7U - (index % 8U)));
    if (bit) {
        bytes[index / 8U] |= mask;
    } else {
        bytes[index / 8U] &= static_cast<std::uint8_t>(~mask);
    }
}

void do_interleave(const BptcWire& encoded, BptcWire& transmitted) noexcept {
    transmitted.fill(0);
    std::size_t tx = 0;
    for (std::size_t index = 0; index < kBptcBits; ++index) {
        set_bit(transmitted.data(), tx, bit_at(encoded.data(), index));
        tx += 181;
        if (tx >= kBptcBits) {
            tx -= kBptcBits;
        }
    }
}

void do_deinterleave(const BptcWire& transmitted, BptcWire& encoded) noexcept {
    encoded.fill(0);
    std::size_t tx = 0;
    for (std::size_t index = 0; index < kBptcBits; ++index) {
        set_bit(encoded.data(), index, bit_at(transmitted.data(), tx));
        tx += 181;
        if (tx >= kBptcBits) {
            tx -= kBptcBits;
        }
    }
}

} // namespace

std::uint8_t encode_tact(std::uint8_t information) noexcept {
    // Generator matrix in ETSI Table B.17. Systematic data bits d3..d0
    // followed by parity h2,h1,h0. Not a competitor lookup table.
    const std::uint8_t d3 = static_cast<std::uint8_t>((information >> 3U) & 1U);
    const std::uint8_t d2 = static_cast<std::uint8_t>((information >> 2U) & 1U);
    const std::uint8_t d1 = static_cast<std::uint8_t>((information >> 1U) & 1U);
    const std::uint8_t d0 = static_cast<std::uint8_t>(information & 1U);
    const std::uint8_t h2 = d3 ^ d2 ^ d1;
    const std::uint8_t h1 = d2 ^ d1 ^ d0;
    const std::uint8_t h0 = d3 ^ d2 ^ d0;
    return static_cast<std::uint8_t>(((information & 0x0FU) << 3U) |
                                     (h2 << 2U) | (h1 << 1U) | h0);
}

bool decode_tact(std::uint8_t codeword, std::uint8_t& information,
                 std::uint8_t& corrected_bits) noexcept {
    if ((codeword & 0x80U) != 0U) {
        return false;
    }
    // Hamming check matrix obtained by appending parity-bit identities to
    // the generator matrix. Syndrome contributions for bit positions 6..0:
    // 5, 7, 6, 3, 4, 2, 1. All nonzero syndromes map to exactly one bit.
    const std::uint8_t d3 = static_cast<std::uint8_t>((codeword >> 6U) & 1U);
    const std::uint8_t d2 = static_cast<std::uint8_t>((codeword >> 5U) & 1U);
    const std::uint8_t d1 = static_cast<std::uint8_t>((codeword >> 4U) & 1U);
    const std::uint8_t d0 = static_cast<std::uint8_t>((codeword >> 3U) & 1U);
    const std::uint8_t h2 = static_cast<std::uint8_t>((codeword >> 2U) & 1U);
    const std::uint8_t h1 = static_cast<std::uint8_t>((codeword >> 1U) & 1U);
    const std::uint8_t h0 = static_cast<std::uint8_t>(codeword & 1U);
    const std::uint8_t syndrome = static_cast<std::uint8_t>(
        ((d3 ^ d2 ^ d1 ^ h2) << 2U) |
        ((d2 ^ d1 ^ d0 ^ h1) << 1U) |
         (d3 ^ d2 ^ d0 ^ h0));
    constexpr std::uint8_t correction_mask[8] = {
        0x00, 0x01, 0x02, 0x08, 0x04, 0x40, 0x10, 0x20,
    };
    const std::uint8_t fixed = static_cast<std::uint8_t>(codeword ^ correction_mask[syndrome]);
    information = static_cast<std::uint8_t>(fixed >> 3U);
    corrected_bits = static_cast<std::uint8_t>(syndrome != 0U);
    return true;
}

bool pack_cach(const CachFields& fields, CachWire& out) noexcept {
    if (fields.lcss > 3U || fields.payload > 0x1FFFFU) {
        return false;
    }
    const std::uint8_t information = static_cast<std::uint8_t>(
        (static_cast<std::uint8_t>(fields.access_type) << 3U) |
        (static_cast<std::uint8_t>(fields.tdma_channel) << 2U) | fields.lcss);
    const std::uint8_t tact = encode_tact(information);
    CachWire result{};
    for (std::size_t i = 0; i < kCachTactTxPositions.size(); ++i) {
        set_bit(result.data(), kCachTactTxPositions[i],
                ((tact >> (6U - i)) & 1U) != 0U);
    }
    for (std::size_t i = 0; i < kCachPayloadTxPositions.size(); ++i) {
        set_bit(result.data(), kCachPayloadTxPositions[i],
                ((fields.payload >> (16U - i)) & 1U) != 0U);
    }
    out = result;
    return true;
}

bool unpack_cach(const CachWire& wire, CachFields& out,
                 std::uint8_t& corrected_tact_bits) noexcept {
    std::uint8_t tact = 0;
    for (const auto position : kCachTactTxPositions) {
        tact = static_cast<std::uint8_t>((tact << 1U) | bit_at(wire.data(), position));
    }
    std::uint8_t information = 0;
    std::uint8_t errors = 0;
    if (!decode_tact(tact, information, errors)) {
        return false;
    }
    CachFields fields{};
    fields.access_type = ((information >> 3U) & 1U) != 0U;
    fields.tdma_channel = ((information >> 2U) & 1U) != 0U;
    fields.lcss = static_cast<std::uint8_t>(information & 3U);
    for (const auto position : kCachPayloadTxPositions) {
        fields.payload = static_cast<std::uint32_t>((fields.payload << 1U) |
                                                    bit_at(wire.data(), position));
    }
    out = fields;
    corrected_tact_bits = errors;
    return true;
}

bool bptc_interleave(const BptcWire& encoded, BptcWire& out) noexcept {
    if ((encoded.back() & kBptcPadMask) != 0U) {
        return false;
    }
    if (&encoded == &out) {
        const BptcWire snapshot = encoded;
        do_interleave(snapshot, out);
    } else {
        do_interleave(encoded, out);
    }
    return true;
}

bool bptc_deinterleave(const BptcWire& transmitted, BptcWire& out) noexcept {
    if ((transmitted.back() & kBptcPadMask) != 0U) {
        return false;
    }
    if (&transmitted == &out) {
        const BptcWire snapshot = transmitted;
        do_deinterleave(snapshot, out);
    } else {
        do_deinterleave(transmitted, out);
    }
    return true;
}

} // namespace orcsdr::dmr
