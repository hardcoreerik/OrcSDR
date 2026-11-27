#include "orcsdr_dmr/d0.hpp"
#include "test_support.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

using namespace orcsdr::dmr;

namespace {
void flip(CachWire& bits, std::size_t index) {
    bits[index / 8U] ^= static_cast<std::uint8_t>(1U << (7U - (index % 8U)));
}
} // namespace

int main() {
    // Figure B.9, TX(23) first. Independent of pack/unpack implementation.
    constexpr std::array<std::uint8_t, 7> expected_tact{{0, 4, 8, 12, 14, 18, 22}};
    constexpr std::array<std::uint8_t, 17> expected_payload{{
        1, 2, 3, 5, 6, 7, 9, 10, 11, 13, 15, 16, 17, 19, 20, 21, 23,
    }};
    CHECK(kCachTactTxPositions == expected_tact);
    CHECK(kCachPayloadTxPositions == expected_payload);
    std::array<bool, 24> seen{};
    for (const auto index : kCachTactTxPositions) {
        CHECK(index < 24 && !seen[index]);
        seen[index] = true;
    }
    for (const auto index : kCachPayloadTxPositions) {
        CHECK(index < 24 && !seen[index]);
        seen[index] = true;
    }
    for (const auto used : seen) CHECK(used);

    // ETSI Table B.17 row vectors, in order AT, TC, LCSS(1), LCSS(0).
    CHECK(encode_tact(0) == 0x00);
    CHECK(encode_tact(8) == 0x45);
    CHECK(encode_tact(4) == 0x27);
    CHECK(encode_tact(2) == 0x16);
    CHECK(encode_tact(1) == 0x0B);
    CHECK(encode_tact(15) == 0x7F);

    std::uint8_t information = 0xA5;
    std::uint8_t corrected = 0xA5;
    CHECK(!decode_tact(0x80, information, corrected));
    CHECK(information == 0xA5 && corrected == 0xA5);

    for (std::uint8_t v = 0; v < 16; ++v) {
        const auto word = encode_tact(v);
        CHECK(decode_tact(word, information, corrected));
        CHECK(information == v && corrected == 0);
        for (unsigned bit = 0; bit < 7; ++bit) {
            const auto one_error = static_cast<std::uint8_t>(word ^ (1U << bit));
            CHECK(decode_tact(one_error, information, corrected));
            CHECK(information == v && corrected == 1);
        }
        // Full-frame roundtrip for every AT/TC/LCSS combination.
        const CachFields fields{
            (v & 8U) != 0U, (v & 4U) != 0U,
            static_cast<std::uint8_t>(v & 3U), 0x1A55AU,
        };
        CachWire wire{};
        CHECK(pack_cach(fields, wire));
        CachFields decoded{};
        CHECK(unpack_cach(wire, decoded, corrected));
        CHECK(corrected == 0);
        CHECK(decoded.access_type == fields.access_type);
        CHECK(decoded.tdma_channel == fields.tdma_channel);
        CHECK(decoded.lcss == fields.lcss);
        CHECK(decoded.payload == fields.payload);
        // Correct each one-bit corruption in any of the seven TACT positions.
        for (const auto position : kCachTactTxPositions) {
            auto damaged = wire;
            flip(damaged, position);
            CHECK(unpack_cach(damaged, decoded, corrected));
            CHECK(corrected == 1);
            CHECK(decoded.access_type == fields.access_type);
            CHECK(decoded.tdma_channel == fields.tdma_channel);
            CHECK(decoded.lcss == fields.lcss);
            CHECK(decoded.payload == fields.payload);
        }
    }

    // Hand-pinned 3-byte golden burst from Fig B.9: AT=1, TC=0, LCSS=2,
    // Hamming TACT=0x53, payload P(16)..P(0)=0x13579.
    const CachFields known{true, false, 2, 0x13579U};
    CachWire packet{};
    CHECK(pack_cach(known, packet));
    CHECK((packet == CachWire{{0xC6, 0xD1, 0xF3}}));
    CachFields restored{};
    CHECK(unpack_cach(CachWire{{0xC6, 0xD1, 0xF3}}, restored, corrected));
    CHECK(restored.access_type && !restored.tdma_channel && restored.lcss == 2);
    CHECK(restored.payload == 0x13579 && corrected == 0);

    // All 17 payload positions, one at a time. Payload is not protected by TACT.
    for (unsigned i = 0; i < 17; ++i) {
        const CachFields onehot{false, false, 0, static_cast<std::uint32_t>(1U << i)};
        CachWire packet_onehot{};
        CHECK(pack_cach(onehot, packet_onehot));
        const auto physical = kCachPayloadTxPositions[16U - i];
        CHECK((packet_onehot[physical / 8U] & (1U << (7U - physical % 8U))) != 0);
        CHECK(unpack_cach(packet_onehot, restored, corrected));
        CHECK(restored.payload == onehot.payload && corrected == 0);
    }
    CachWire preserved{{0xAA, 0xBB, 0xCC}};
    CHECK(!pack_cach(CachFields{false, false, 4, 0}, preserved));
    CHECK((preserved == CachWire{{0xAA, 0xBB, 0xCC}}));
    CHECK(!pack_cach(CachFields{false, false, 0, 0x20000}, preserved));
    CHECK((preserved == CachWire{{0xAA, 0xBB, 0xCC}}));

    std::puts("cach: Figure B.9 map; Hamming 16+112 vectors; 17 payload bits PASS");
    return 0;
}
