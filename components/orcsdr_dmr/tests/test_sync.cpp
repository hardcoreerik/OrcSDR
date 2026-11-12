#include "orcsdr_dmr/d0.hpp"
#include "test_support.hpp"

#include <array>
#include <cstdint>

using namespace orcsdr::dmr;

int main() {
    // Independently transcribed from ETSI TS 102 361-1 V2.7.1, Table 9.2,
    // in section order. A 48-bit symbol is 24 dibits, MSB transmitted first.
    constexpr std::array<std::uint64_t, 8> expected{{
        0x755FD7DF75F7ULL, 0xDFF57D75DF5DULL,
        0x7F7D5DD57DFDULL, 0xD5D7F77FD757ULL,
        0x5D577F7757FFULL, 0xF7FDD5DDFD55ULL,
        0x7DFFD5F55D5FULL, 0xD7557F5FF7F5ULL,
    }};
    static_assert(kSyncCount == 8, "Only the eight voice/data words are in D0");
    static_assert(sync_word(SyncKind::direct_ts2_voice) == 0x7DFFD5F55D5FULL,
                  "TS2 voice must match ETSI, not a transcription typo");
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const auto observed = sync_word(static_cast<SyncKind>(i));
        CHECK(observed == expected[i]);
        CHECK((observed & ~kSyncMask48) == 0U);
        for (std::size_t j = 0; j < i; ++j) {
            CHECK(observed != expected[j]);
        }
    }
    CHECK(sync_word(static_cast<SyncKind>(255)) == 0U);
    // Table 9.2: each voice/data pair is a symbol-wise complement.
    for (std::size_t i = 0; i < expected.size(); i += 2) {
        CHECK((expected[i] ^ expected[i + 1]) == 0xAAAAAAAAAAAAULL);
    }
    // 'MS RC sync' and the reserved pattern are deliberately not assigned a
    // voice/data SyncKind and must not collide with one of the eight entries.
    for (const auto excluded : {0x77D55F7DFD77ULL, 0xDD7FF5D757DDULL}) {
        for (const auto included : expected) {
            CHECK(excluded != included);
        }
    }
    std::puts("sync: 8 ETSI words, pair polarity, reserved/RC exclusions PASS");
    return 0;
}
