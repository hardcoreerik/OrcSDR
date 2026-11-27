#include "orcsdr_dmr/d0.hpp"
#include "test_support.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

using namespace orcsdr::dmr;

namespace {
void put(BptcWire& block, std::size_t index) {
    block[index / 8U] |= static_cast<std::uint8_t>(1U << (7U - (index % 8U)));
}
bool has(const BptcWire& block, std::size_t index) {
    return (block[index / 8U] & (1U << (7U - (index % 8U)))) != 0;
}
} // namespace

int main() {
    static_assert(sizeof(BptcWire) == 25, "196 bits must fit in 25 octets");
    static_assert(bptc_encoded_index(181) == 1, "modular inverse of 181");
    // Explicit ETSI Table B.2 anchors, including matrix bit R(3) and last.
    constexpr std::array<std::size_t, 11> matrix_indices{{
        0, 1, 2, 3, 4, 13, 65, 66, 131, 132, 195,
    }};
    constexpr std::array<std::size_t, 11> tx_indices{{
        0, 181, 166, 151, 136, 1, 5, 186, 191, 176, 15,
    }};
    for (std::size_t i = 0; i < matrix_indices.size(); ++i) {
        CHECK(bptc_tx_index(matrix_indices[i]) == tx_indices[i]);
        CHECK(bptc_encoded_index(tx_indices[i]) == matrix_indices[i]);
    }
    std::array<bool, kBptcBits> seen{};
    for (std::size_t i = 0; i < kBptcBits; ++i) {
        const auto index = bptc_tx_index(i);
        CHECK(index < kBptcBits && !seen[index]);
        seen[index] = true;
        CHECK(bptc_encoded_index(index) == i);
    }
    for (const auto visited : seen) CHECK(visited);

    const BptcWire zeros{};
    BptcWire tx{};
    BptcWire recovered{};
    CHECK(bptc_interleave(zeros, tx));
    CHECK(tx == zeros);
    CHECK(bptc_deinterleave(tx, recovered));
    CHECK(recovered == zeros);

    // Impulse tests cover all 196 physical positions and both directions.
    for (std::size_t i = 0; i < kBptcBits; ++i) {
        BptcWire impulse{};
        put(impulse, i);
        CHECK(bptc_interleave(impulse, tx));
        CHECK(has(tx, bptc_tx_index(i)));
        unsigned ones = 0;
        for (const auto byte : tx) {
            for (unsigned bit = 0; bit < 8; ++bit) {
                ones += ((byte >> bit) & 1U);
            }
        }
        CHECK(ones == 1);
        CHECK((tx.back() & kBptcPadMask) == 0U);
        CHECK(bptc_deinterleave(tx, recovered));
        CHECK(recovered == impulse);
    }

    // Nontrivial, fixed known-answer output derived independently from the
    // Equation B.1 mapping (not from the C++ implementation under test).
    constexpr BptcWire known_encoded{{
        0x92,0xD2,0x5A,0x4B,0x49,0x69,0x2D,0x25,0xA4,0xB4,
        0x96,0x92,0xD2,0x5A,0x4B,0x49,0x69,0x2D,0x25,0xA4,
        0xB4,0x96,0x92,0xD2,0x50,
    }};
    constexpr BptcWire known_tx{{
        0x99,0x33,0x4C,0xC9,0x32,0x64,0x99,0x96,0x64,0xC9,
        0x33,0x2C,0xC9,0xB2,0x66,0x19,0x93,0x64,0xCD,0x33,
        0x24,0xC9,0x9A,0x66,0x40,
    }};
    CHECK(bptc_interleave(known_encoded, tx));
    CHECK(tx == known_tx);
    CHECK(bptc_deinterleave(known_tx, recovered));
    CHECK(recovered == known_encoded);
    auto inplace = known_encoded;
    CHECK(bptc_interleave(inplace, inplace));
    CHECK(inplace == known_tx);
    CHECK(bptc_deinterleave(inplace, inplace));
    CHECK(inplace == known_encoded);

    const BptcWire all_ones{{
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
        0xFF,0xFF,0xFF,0xFF,0xF0,
    }};
    CHECK(bptc_interleave(all_ones, tx));
    CHECK(tx == all_ones);
    CHECK(bptc_deinterleave(all_ones, recovered));
    CHECK(recovered == all_ones);

    auto invalid = known_encoded;
    invalid.back() |= 0x01;
    tx = known_tx;
    CHECK(!bptc_interleave(invalid, tx));
    CHECK(tx == known_tx);
    CHECK(!bptc_interleave(invalid, invalid));
    CHECK((invalid.back() & 0x01U) != 0U);
    recovered = known_encoded;
    CHECK(!bptc_deinterleave(invalid, recovered));
    CHECK(recovered == known_encoded);

    std::puts("bptc: ETSI B.2 anchors; bijection; 196 impulses; golden bytes PASS");
    return 0;
}
