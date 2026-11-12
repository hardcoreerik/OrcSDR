// OrcSDR DMR D1 fixed-memory host-only burst decoder.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orcsdr_dmr/d1.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace orcsdr::dmr::d1 {
namespace {
constexpr std::size_t kBurstDibits = 132;
constexpr std::size_t kCachDibits = 12;
constexpr std::size_t kFrameDibits = kBurstDibits + kCachDibits;
constexpr std::uint8_t kRow15[11] = {9,13,15,14,7,10,5,11,12,6,3};
constexpr std::uint8_t kCol13[9] = {15,14,7,10,5,11,12,6,3};
constexpr std::uint8_t kRow16[11] = {19,26,31,28,14,21,11,22,25,13,7};
// ETSI Table B.11 (20,8,8), confirmed with V2.6.1 printed matrix and d_min=8.
constexpr std::uint16_t kGolayParity[8] = {
    0x3da, 0xd99, 0x6cd, 0x367, 0xdc6, 0xa97, 0x93e, 0x8eb
};
// ETSI Table B.12, 7 information bits and 9 parity bits, minimum distance 6.
constexpr std::uint16_t kQrParity[7] = {
    0x04f, 0x11e, 0x1b7, 0x1e2, 0x1c9, 0x0e5, 0x073
};

bool get_bit(const std::uint8_t* bytes, std::size_t bit) noexcept {
    return ((bytes[bit / 8] >> (7 - (bit % 8))) & 1U) != 0;
}
void set_bit(std::uint8_t* bytes, std::size_t bit, bool value) noexcept {
    const auto mask = static_cast<std::uint8_t>(1U << (7 - bit % 8));
    if (value) bytes[bit / 8] |= mask;
    else bytes[bit / 8] &= static_cast<std::uint8_t>(~mask);
}
unsigned weight(std::uint32_t value) noexcept {
    unsigned n = 0;
    while (value != 0) { n += value & 1U; value >>= 1U; }
    return n;
}
std::uint32_t golay_word(std::uint8_t value) noexcept {
    std::uint16_t parity = 0;
    for (unsigned i = 0; i < 8; ++i) {
        if ((value & (0x80U >> i)) != 0) parity ^= kGolayParity[i];
    }
    return (static_cast<std::uint32_t>(value) << 12U) | parity;
}
std::uint16_t qr_word(std::uint8_t value) noexcept {
    std::uint16_t parity = 0;
    for (unsigned i = 0; i < 7; ++i) {
        if ((value & (0x40U >> i)) != 0) parity ^= kQrParity[i];
    }
    return static_cast<std::uint16_t>((value << 9U) | parity);
}
bool decode_emb(std::uint16_t wire, std::uint8_t& info, std::uint8_t& corrections) noexcept {
    unsigned best = 17;
    bool unique = false;
    std::uint8_t result = 0;
    for (unsigned i = 0; i < 128; ++i) {
        const auto d = weight(wire ^ qr_word(static_cast<std::uint8_t>(i)));
        if (d < best) { best = d; result = static_cast<std::uint8_t>(i); unique = true; }
        else if (d == best) unique = false;
    }
    if (!unique || best > 2) return false;
    info = result;
    corrections = static_cast<std::uint8_t>(best);
    return true;
}

std::uint8_t parity_syndrome(const std::uint8_t* words, unsigned info_count,
                             unsigned parity_count, const std::uint8_t* parity) noexcept {
    std::uint8_t syndrome = 0;
    for (unsigned i = 0; i < info_count; ++i) if (words[i]) syndrome ^= parity[i];
    for (unsigned i = 0; i < parity_count; ++i) {
        if (words[info_count + i]) syndrome ^= static_cast<std::uint8_t>(1U << (parity_count - 1 - i));
    }
    return syndrome;
}
// Correct only uniquely locatable single errors. Never treat a FEC correction
// as proof of correctness: an independent RS/CRC gate is mandatory for identity.
bool correct_hamming(std::uint8_t* words, unsigned info, unsigned check,
                     const std::uint8_t* parity, unsigned& corrections) noexcept {
    const auto syndrome = parity_syndrome(words, info, check, parity);
    if (!syndrome) return true;
    unsigned matches = 0, index = 0;
    for (unsigned i = 0; i < info; ++i) {
        if (parity[i] == syndrome) { ++matches; index = i; }
    }
    for (unsigned i = 0; i < check; ++i) {
        if ((1U << (check - 1 - i)) == syndrome) { ++matches; index = info + i; }
    }
    if (matches != 1) return false;
    words[index] ^= 1U;
    ++corrections;
    return true;
}

// Reed-Solomon GF(256) with irreducible x^8+x^4+x^3+x^2+1.
std::uint8_t gf_mul(std::uint8_t x, std::uint8_t y) noexcept {
    std::uint8_t product = 0;
    for (unsigned i = 0; i < 8; ++i) {
        if (y & 1U) product ^= x;
        const bool carry = (x & 0x80U) != 0;
        x = static_cast<std::uint8_t>((x << 1U) ^ (carry ? 0x1dU : 0U));
        y >>= 1U;
    }
    return product;
}
std::uint8_t gf_power(unsigned exponent) noexcept {
    std::uint8_t x = 1;
    for (unsigned i = 0; i < exponent; ++i) x = gf_mul(x, 2);
    return x;
}
std::uint16_t crc_ccitt(const std::uint8_t* bytes, std::size_t count) noexcept {
    // ETSI Annex B.3.8: init 0x0000, polynomial x^16+x^12+x^5+1,
    // inversion 0xFFFF at the end, transmitted MSB first.
    std::uint16_t crc = 0;
    for (std::size_t j = 0; j < count; ++j) {
        crc ^= static_cast<std::uint16_t>(bytes[j]) << 8U;
        for (unsigned i = 0; i < 8; ++i) {
            crc = static_cast<std::uint16_t>((crc << 1U) ^ ((crc & 0x8000U) ? 0x1021U : 0));
        }
    }
    return static_cast<std::uint16_t>(crc ^ 0xffffU);
}

// BPTC(128,72): 7 rows of 11 LC/checksum bits + H(16,11,4),
// one parity row, then column-major transmission over four embedded bursts.
bool decode_embedded_lc(const std::array<std::uint8_t, 128>& wire,
                        std::array<std::uint8_t, 9>& lc) noexcept {
    std::uint8_t matrix[8][16]{};
    for (unsigned i = 0; i < 128; ++i) matrix[i % 8][i / 8] = wire[i];
    // For this phase, fail on any FEC inconsistency. No speculative correction.
    for (unsigned r = 0; r < 7; ++r) {
        if (parity_syndrome(matrix[r], 11, 5, kRow16) != 0) return false;
    }
    for (unsigned col = 0; col < 16; ++col) {
        unsigned parity = 0;
        for (unsigned r = 0; r < 8; ++r) parity ^= matrix[r][col];
        if (parity != 0) return false;
    }
    std::uint8_t bits[77]{};
    for (unsigned i = 0; i < 77; ++i) bits[i] = matrix[i / 11][i % 11];
    std::array<std::uint8_t, 9> received{};
    for (unsigned i = 0; i < 72; ++i) set_bit(received.data(), i, bits[i] != 0);
    unsigned sum = 0;
    for (auto byte : received) sum += byte;
    unsigned check = 0;
    for (unsigned i = 72; i < 77; ++i) check = (check << 1U) | bits[i];
    if (check != (sum % 31U)) return false;
    lc = received;
    return true;
}

std::uint8_t slot_for_sync(SyncKind kind) noexcept {
    const auto v = static_cast<std::uint8_t>(kind);
    if (v == static_cast<std::uint8_t>(SyncKind::direct_ts1_voice) ||
        v == static_cast<std::uint8_t>(SyncKind::direct_ts1_data)) return 1;
    if (v == static_cast<std::uint8_t>(SyncKind::direct_ts2_voice) ||
        v == static_cast<std::uint8_t>(SyncKind::direct_ts2_data)) return 2;
    return 0;
}
bool is_data(SyncKind kind) noexcept {
    return (static_cast<std::uint8_t>(kind) & 1U) != 0;
}
bool is_bs(SyncKind kind) noexcept {
    return kind == SyncKind::bs_data || kind == SyncKind::bs_voice;
}
std::uint32_t bits20(const std::uint8_t (&bits)[264]) noexcept {
    std::uint32_t value = 0;
    for (unsigned i = 98; i < 108; ++i) value = (value << 1U) | bits[i];
    for (unsigned i = 156; i < 166; ++i) value = (value << 1U) | bits[i];
    return value;
}
} // namespace

bool decode_slot_type(std::uint32_t wire, std::uint8_t& cc,
                      std::uint8_t& type, std::uint8_t& corrected) noexcept {
    if (wire > 0xfffffU) return false;
    unsigned best = 21;
    bool unique = false;
    std::uint8_t value = 0;
    for (unsigned candidate = 0; candidate < 256; ++candidate) {
        const unsigned d = weight(wire ^ golay_word(static_cast<std::uint8_t>(candidate)));
        if (d < best) { best = d; value = static_cast<std::uint8_t>(candidate); unique = true; }
        else if (d == best) unique = false;
    }
    if (!unique || best > 3) return false;
    cc = static_cast<std::uint8_t>(value >> 4U);
    type = static_cast<std::uint8_t>(value & 15U);
    corrected = static_cast<std::uint8_t>(best);
    return true;
}

bool decode_bptc(const BptcWire& transmitted, std::array<std::uint8_t, 12>& data,
                 std::uint16_t& corrected_bits) noexcept {
    BptcWire encoded{};
    if (!bptc_deinterleave(transmitted, encoded)) return false;
    // ETSI B.1.1: encoded index 0 is R(3), followed by 13x15 matrix.
    std::uint8_t matrix[13][15]{};
    for (unsigned row = 0; row < 13; ++row) {
        for (unsigned col = 0; col < 15; ++col) {
            matrix[row][col] = static_cast<std::uint8_t>(get_bit(encoded.data(), 1 + row * 15 + col));
        }
    }
    unsigned total = 0;
    // Iteration tolerates a correctable bit in either matrix dimension.
    for (unsigned pass = 0; pass < 3; ++pass) {
        for (unsigned row = 0; row < 9; ++row) {
            unsigned delta = 0;
            if (!correct_hamming(matrix[row], 11, 4, kRow15, delta)) return false;
            total += delta;
        }
        for (unsigned col = 0; col < 15; ++col) {
            std::uint8_t column[13]{};
            for (unsigned row = 0; row < 13; ++row) column[row] = matrix[row][col];
            unsigned delta = 0;
            if (!correct_hamming(column, 9, 4, kCol13, delta)) return false;
            for (unsigned row = 0; row < 13; ++row) matrix[row][col] = column[row];
            total += delta;
        }
    }
    for (unsigned row = 0; row < 9; ++row) {
        if (parity_syndrome(matrix[row], 11, 4, kRow15) != 0) return false;
    }
    for (unsigned col = 0; col < 15; ++col) {
        std::uint8_t column[13]{};
        for (unsigned row = 0; row < 13; ++row) column[row] = matrix[row][col];
        if (parity_syndrome(column, 9, 4, kCol13) != 0) return false;
    }
    // Fail closed on invalid reserved bits (including R(3)).
    if (get_bit(encoded.data(), 0) || matrix[0][0] || matrix[0][1] || matrix[0][2]) return false;
    std::array<std::uint8_t, 12> output{};
    for (unsigned i = 0; i < 96; ++i) {
        const unsigned p = i + 3;
        set_bit(output.data(), i, matrix[p / 11][p % 11] != 0);
    }
    data = output;
    corrected_bits = static_cast<std::uint16_t>(total);
    return true;
}

bool check_full_lc(const std::array<std::uint8_t, 12>& raw, std::uint8_t type) noexcept {
    if (type != 1 && type != 2) return false;
    const std::uint8_t mask = (type == 1 ? 0x96U : 0x99U);
    std::uint8_t coded[12]{};
    for (unsigned j = 0; j < 12; ++j) coded[j] = static_cast<std::uint8_t>(raw[j] ^ (j >= 9 ? mask : 0U));
    // ETSI Annex B.3.6 generator (x+alpha)(x+alpha^2)(x+alpha^3).
    // Zero syndromes at all 3 roots; no attempt at RS error correction.
    for (unsigned root = 1; root <= 3; ++root) {
        const std::uint8_t alpha = gf_power(root);
        std::uint8_t syndrome = 0;
        for (auto byte : coded) syndrome = static_cast<std::uint8_t>(gf_mul(syndrome, alpha) ^ byte);
        if (syndrome) return false;
    }
    return true;
}

bool check_crc16(const std::array<std::uint8_t, 12>& data, std::uint8_t type) noexcept {
    std::uint16_t mask = 0;
    switch (type) {
        case 3: mask = 0xa5a5U; break;
        case 6: mask = 0xccccU; break;
        case 11: mask = 0x3333U; break;
        default: return false;
    }
    const auto wire_crc = static_cast<std::uint16_t>((data[10] << 8U) | data[11]);
    return static_cast<std::uint16_t>(wire_crc ^ mask) == crc_ccitt(data.data(), 10);
}

bool parse_identity(const std::uint8_t* lc, Event& event) noexcept {
    if (!lc || !event.integrity_valid) return false;
    const auto flco = static_cast<std::uint8_t>(lc[0] & 0x3fU);
    const auto feature_id = lc[1];
    const auto dest = static_cast<std::uint32_t>((lc[3] << 16U) | (lc[4] << 8U) | lc[5]);
    const auto src = static_cast<std::uint32_t>((lc[6] << 16U) | (lc[7] << 8U) | lc[8]);
    if ((lc[0] & 0x40U) != 0 || feature_id != 0 || (flco != 0 && flco != 3) ||
        src == 0 || dest == 0) return false;
    event.flco = flco;
    event.feature_id = feature_id;
    event.protected_flag = (lc[0] & 0x80U) != 0;
    event.service_options = lc[2];
    event.destination_id = dest;
    event.source_id = src;
    event.identity = flco == 0 ? IdentityKind::group : IdentityKind::private_call;
    event.evidence = Evidence::identity_valid;
    return true;
}

void Decoder::reset() noexcept {
    history_.fill(0);
    consumed_ = 0;
    late_ = {};
    cach_cadence_.reset();
}
void Decoder::burst_bits(std::uint64_t start, std::uint8_t (&bits)[264]) const noexcept {
    for (unsigned j = 0; j < kBurstDibits; ++j) {
        const auto d = at(start + j);
        bits[2 * j] = static_cast<std::uint8_t>(d >> 1U);
        bits[2 * j + 1] = static_cast<std::uint8_t>(d & 1U);
    }
}

void Decoder::scan(EventSink sink, void* user) noexcept {
    if (consumed_ < kBurstDibits) return;
    const auto start = consumed_ - kBurstDibits;
    std::uint64_t sync = 0;
    for (unsigned i = 54; i < 78; ++i) sync = (sync << 2U) | at(start + i);
    int sync_index = -1;
    for (std::size_t i = 0; i < kSyncCount; ++i) {
        if (sync == kSyncWords[i]) { sync_index = static_cast<int>(i); break; }
    }
    if (sync_index < 0) return;
    const auto kind = static_cast<SyncKind>(sync_index);
    Event e{};
    e.burst_start_dibit = start;
    e.sync = kind;
    e.slot = slot_for_sync(kind);
    if (is_bs(kind)) {
        CachAssessment assessment{};
        if (start >= kCachDibits) {
            CachWire cach{};
            for (unsigned i = 0; i < kCachDibits; ++i) {
                const auto dibit = at(start - kCachDibits + i);
                cach[i / 4] = static_cast<std::uint8_t>(
                    cach[i / 4] | (dibit << (6U - 2U * (i % 4U))));
            }
            CachFields fields{};
            std::uint8_t corrected = 0;
            if (unpack_cach(cach, fields, corrected)) {
                const auto hint = static_cast<std::uint8_t>(fields.tdma_channel ? 2U : 1U);
                assessment = cach_cadence_.observe(start, true, true, hint, corrected);
            } else {
                cach_cadence_.reset();
            }
        } else {
            cach_cadence_.reset();
        }
        // All seven-bit Hamming TACT patterns decode. A candidate alone
        // cannot claim CACH presence, TDMA slot, or radio identity.
        e.cach_confidence = assessment.confidence;
        e.cach_tact_decoded = assessment.confidence != CachConfidence::none;
        e.cach_candidate_slot = assessment.candidate_slot;
        e.cach_tact_corrections = assessment.corrections;
        e.cach_cadence_bursts = assessment.cadence_bursts;
        if (assessment.confidence == CachConfidence::cadence_validated) {
            e.cach_present = true;
            e.slot_from_cach = true;
            e.slot = assessment.candidate_slot;
        }
    } else {
        // MS/direct SYNC does not corroborate a BS/CACH cadence lock.
        cach_cadence_.reset();
    }
    if (!is_data(kind)) {
        // Only an explicit voice SYNC anchors possible embedded-LC late entry.
        // Do not infer voice metadata from the voice SYNC alone.
        if (is_bs(kind)) {
            // A clean tentative TACT may seed a PRIVATE late-entry hypothesis,
            // but does not publish a slot. Four further matching CACHs plus
            // complete embedded LC are required before an identity event.
            const auto candidate_slot = e.slot ? e.slot :
                ((e.cach_tact_decoded && e.cach_tact_corrections == 0)
                    ? e.cach_candidate_slot : static_cast<std::uint8_t>(0));
            if (candidate_slot != 0) {
                auto& target = late_[candidate_slot];
                target = {};
                target.active = true;
                target.slot = candidate_slot;
                target.base = start;
                target.cach_checks = 1;
            }
        }
        if (sink) sink(e, user);
        return;
    }
    std::uint8_t bits[264]{};
    burst_bits(start, bits);
    std::uint8_t cc = 0, type = 0, corrected = 0;
    if (!decode_slot_type(bits20(bits), cc, type, corrected)) {
        if (sink) sink(e, user);
        return;
    }
    e.golay_valid = true;
    e.colour_code = cc;
    e.data_type = type;
    e.golay_corrected_bits = corrected;
    e.evidence = Evidence::slot_type;
    if (type == 1 || type == 2 || type == 3 || type == 6 || type == 11) {
        BptcWire tx{};
        for (unsigned i = 0; i < 196; ++i) {
            const unsigned pos = i < 98 ? i : i + 68;
            set_bit(tx.data(), i, bits[pos] != 0);
        }
        std::array<std::uint8_t, 12> content{};
        std::uint16_t repairs = 0;
        if (decode_bptc(tx, content, repairs)) {
            e.bptc_valid = true;
            e.bptc_corrected_bits = repairs;
            e.evidence = Evidence::fec_valid;
            const bool check = (type <= 2) ? check_full_lc(content, type) : check_crc16(content, type);
            if (check) {
                e.integrity_valid = true;
                e.evidence = Evidence::check_valid;
                if (type <= 2) parse_identity(content.data(), e);
            }
        }
    }
    if (sink) sink(e, user);
}

void Decoder::scan_embedded(EventSink sink, void* user) noexcept {
    if (consumed_ < kBurstDibits) return;
    const auto start = consumed_ - kBurstDibits;
    for (auto& state : late_) {
        if (!state.active) continue;
        if (start <= state.base) continue;
        const auto delta = start - state.base;
        if (delta > 4 * 2 * kFrameDibits) { state.active = false; continue; }
        if (delta % (2 * kFrameDibits) != 0) continue;
        const unsigned stage = static_cast<unsigned>(delta / (2 * kFrameDibits));
        if (stage == 0 || stage > 4 || static_cast<unsigned>(state.stage) + 1U != stage) continue;
        // The same inferred slot must recur at each 288-dibit voice stage.
        // A repaired/corrupt CACH TACT is insufficient to continue the claim.
        if (start < kCachDibits) { state.active = false; continue; }
        CachWire prefix{};
        for (unsigned i = 0; i < kCachDibits; ++i) {
            const auto dibit = at(start - kCachDibits + i);
            prefix[i / 4] = static_cast<std::uint8_t>(
                prefix[i / 4] | (dibit << (6U - 2U * (i % 4U))));
        }
        CachFields tact{};
        std::uint8_t tact_corrections = 0;
        if (!unpack_cach(prefix, tact, tact_corrections) ||
            tact_corrections != 0 ||
            (tact.tdma_channel ? 2U : 1U) != state.slot) {
            state.active = false;
            continue;
        }
        std::uint8_t bits[264]{};
        burst_bits(start, bits);
        std::uint16_t qr = 0;
        for (unsigned i = 108; i < 116; ++i) qr = static_cast<std::uint16_t>((qr << 1U) | bits[i]);
        for (unsigned i = 148; i < 156; ++i) qr = static_cast<std::uint16_t>((qr << 1U) | bits[i]);
        std::uint8_t info = 0, repairs = 0;
        if (!decode_emb(qr, info, repairs)) { state.active = false; continue; }
        const auto colour = static_cast<std::uint8_t>(info >> 3U);
        const auto lcss = static_cast<std::uint8_t>(info & 3U);
        const auto expected_lcss = (stage == 1) ? 1U : (stage == 4 ? 2U : 3U);
        if (lcss != expected_lcss || (stage > 1 && colour != state.colour_code)) {
            state.active = false;
            continue;
        }
        if (stage == 1) state.colour_code = colour;
        for (unsigned i = 0; i < 32; ++i) state.fragments[(stage - 1) * 32 + i] = bits[116 + i];
        state.stage = static_cast<std::uint8_t>(stage);
        ++state.cach_checks;
        if (stage == 4) {
            std::array<std::uint8_t, 9> lc{};
            if (state.cach_checks == 5 && decode_embedded_lc(state.fragments, lc)) {
                Event e{};
                e.burst_start_dibit = start;
                e.sync = SyncKind::bs_voice;
                e.slot = state.slot;
                e.cach_present = true;
                e.slot_from_cach = true;
                e.cach_tact_decoded = true;
                e.cach_candidate_slot = state.slot;
                e.cach_cadence_bursts = state.cach_checks;
                // This path is grounded by complete LC/FEC/checksum and
                // repeated same-slot CACH; not by the first TACT alone.
                e.cach_confidence = CachConfidence::embedded_lc_validated;
                e.colour_code = colour;
                e.late_entry = true;
                e.integrity_valid = true; // checksum + both BPTC dimensions verified
                e.evidence = Evidence::check_valid;
                if (parse_identity(lc.data(), e) && sink) sink(e, user);
            }
            state.active = false;
        }
    }
}

bool Decoder::feed(const std::uint8_t* dibits, std::size_t count,
                   EventSink sink, void* user) noexcept {
    if (count && !dibits) return false;
    for (std::size_t i = 0; i < count; ++i) if (dibits[i] > 3U) return false;
    for (std::size_t i = 0; i < count; ++i) {
        history_[static_cast<std::size_t>(consumed_ % kRingCapacity)] = dibits[i];
        ++consumed_;
        scan(sink, user);
        scan_embedded(sink, user);
    }
    return true;
}
} // namespace orcsdr::dmr::d1
