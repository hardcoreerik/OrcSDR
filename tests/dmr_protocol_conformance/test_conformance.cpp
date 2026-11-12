// Independent, host-only ETSI DMR D0/D1 conformance tests.
// Sources: ETSI TS 102 361-1 V2.7.1 (2026-05), rendered printed
// Tables 9.2, B.11, B.17, B.18, B.21 and Figures 6.4/6.5/B.1/B.3.
// ETSI TS 102 361-2 V2.5.1 (2023-05), standard Full LC fields.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orcsdr_dmr/d0.hpp"
#include "orcsdr_dmr/d1.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
using namespace orcsdr::dmr;
using namespace orcsdr::dmr::d1;

namespace {
#define REQUIRE(c) do { if (!(c)) { std::fprintf(stderr, "%s:%d failed: %s\n", __FILE__, __LINE__, #c); return false; } } while(0)
using Block = std::array<std::uint8_t,12>;
using Burst = std::array<std::uint8_t,132>;
// Transcribed from the two actual *rendered* PDFs, not text extraction.
// Each row consists of an eight-bit identity prefix and 12 parity columns.
constexpr std::uint16_t golay_parity[8] = {
    0x3da, 0xd99, 0x6cd, 0x367, 0xdc6, 0xa97, 0x93e, 0x8eb
};
constexpr std::uint8_t row15[11] = {9,13,15,14,7,10,5,11,12,6,3};
constexpr std::uint8_t col13[9] = {15,14,7,10,5,11,12,6,3};
constexpr std::uint8_t row16[11] = {19,26,31,28,14,21,11,22,25,13,7};
constexpr std::uint16_t qr_parity[7] = {0x4f,0x11e,0x1b7,0x1e2,0x1c9,0xe5,0x73};
// ETSI B.18, 9 rows x three parity columns in GF(256).
constexpr std::uint8_t rs_parity[9][3] = {
    {0x1c,0xbc,0xfd}, {0x89,0x31,0x08}, {0xad,0x41,0x36},
    {0x7d,0x71,0x16}, {0xf3,0xa6,0x3a}, {0x08,0x83,0x7b},
    {0x3f,0x6f,0x02}, {0x6c,0x0d,0xa7}, {0x0e,0x38,0x40}
};
constexpr std::uint64_t sync_reference[8] = {
    0x755fd7df75f7ULL, 0xdff57d75df5dULL,
    0x7f7d5dd57dfdULL, 0xd5d7f77fd757ULL,
    0x5d577f7757ffULL, 0xf7fdd5ddfd55ULL,
    0x7dffd5f55d5fULL, 0xd7557f5ff7f5ULL
};
void write_bit(std::uint8_t* p, std::size_t index, bool value) {
    const std::uint8_t m = static_cast<std::uint8_t>(0x80U >> (index % 8U));
    if (value) p[index / 8U] |= m;
    else p[index / 8U] &= static_cast<std::uint8_t>(~m);
}
bool read_bit(const std::uint8_t* p, std::size_t index) {
    return (p[index / 8U] & (0x80U >> (index % 8U))) != 0;
}
unsigned hamming(std::uint32_t x) {
    unsigned n = 0;
    while (x) { n += x & 1U; x >>= 1U; }
    return n;
}
std::uint32_t reference_slot_word(std::uint8_t info) {
    std::uint16_t parity = 0;
    for (unsigned row = 0; row < 8; ++row)
        if ((info & (0x80U >> row)) != 0) parity ^= golay_parity[row];
    return (static_cast<std::uint32_t>(info) << 12U) | parity;
}
std::uint16_t reference_emb_word(std::uint8_t info) {
    std::uint16_t parity = 0;
    for (unsigned row = 0; row < 7; ++row)
        if ((info & (0x40U >> row)) != 0) parity ^= qr_parity[row];
    return static_cast<std::uint16_t>((info << 9U) | parity);
}
std::uint8_t gf_multiply(std::uint8_t a, std::uint8_t b) {
    // Russian-peasant polynomial arithmetic, modulo x^8+x^4+x^3+x^2+1.
    std::uint8_t value = 0;
    while (b != 0) {
        if (b & 1U) value ^= a;
        b >>= 1U;
        const bool overflow = (a & 0x80U) != 0;
        a = static_cast<std::uint8_t>((a << 1U) ^ (overflow ? 0x1dU : 0U));
    }
    return value;
}
Block reference_lc(unsigned destination = 77, unsigned source = 1234567,
                   unsigned flco = 0, unsigned type = 1) {
    Block word{};
    word[0] = static_cast<std::uint8_t>(flco);
    word[1] = 0;
    word[2] = 3;
    word[3] = static_cast<std::uint8_t>(destination >> 16U);
    word[4] = static_cast<std::uint8_t>(destination >> 8U);
    word[5] = static_cast<std::uint8_t>(destination);
    word[6] = static_cast<std::uint8_t>(source >> 16U);
    word[7] = static_cast<std::uint8_t>(source >> 8U);
    word[8] = static_cast<std::uint8_t>(source);
    for (unsigned col = 0; col < 3; ++col) {
        std::uint8_t parity = 0;
        for (unsigned row = 0; row < 9; ++row)
            parity ^= gf_multiply(word[row], rs_parity[row][col]);
        word[9 + col] = static_cast<std::uint8_t>(parity ^ (type == 1 ? 0x96U : 0x99U));
    }
    return word;
}
std::uint16_t reference_crc(const Block& b) {
    // Bitwise polynomial long division, zero init, final complement.
    std::uint16_t remainder = 0;
    for (unsigned k = 0; k < 80; ++k) {
        const bool input = read_bit(b.data(), k);
        const bool high = ((remainder >> 15U) & 1U) != 0;
        remainder = static_cast<std::uint16_t>((remainder << 1U) ^ ((input != high) ? 0x1021U : 0U));
    }
    return static_cast<std::uint16_t>(remainder ^ 0xffffU);
}
unsigned parity_from_matrix(const std::uint8_t* bits, unsigned count, const std::uint8_t* columns) {
    unsigned out = 0;
    for (unsigned i = 0; i < count; ++i) if (bits[i]) out ^= columns[i];
    return out;
}
BptcWire reference_bptc(const Block& payload) {
    std::uint8_t rows[13][15]{};
    for (unsigned i = 0; i < 96; ++i)
        rows[(i + 3U) / 11U][(i + 3U) % 11U] = static_cast<std::uint8_t>(read_bit(payload.data(), i));
    for (unsigned r = 0; r < 9; ++r) {
        const unsigned p = parity_from_matrix(rows[r], 11, row15);
        for (unsigned k = 0; k < 4; ++k) rows[r][11U + k] = static_cast<std::uint8_t>((p >> (3U - k)) & 1U);
    }
    for (unsigned c = 0; c < 15; ++c) {
        std::uint8_t col[9]{};
        for (unsigned r = 0; r < 9; ++r) col[r] = rows[r][c];
        const unsigned p = parity_from_matrix(col, 9, col13);
        for (unsigned k = 0; k < 4; ++k) rows[9U + k][c] = static_cast<std::uint8_t>((p >> (3U - k)) & 1U);
    }
    BptcWire transmitted{};
    // R(3) at encoded index zero; ETSI (181*i)%196 mapping.
    for (unsigned r = 0; r < 13; ++r)
        for (unsigned c = 0; c < 15; ++c)
            write_bit(transmitted.data(), ((1U + r * 15U + c) * 181U) % 196U, rows[r][c] != 0);
    return transmitted;
}
Burst reference_burst(SyncKind kind, unsigned colour, unsigned dtype, const Block& payload) {
    std::array<std::uint8_t, 264> bits{};
    const auto bptc = reference_bptc(payload);
    for (unsigned i = 0; i < 196; ++i)
        bits[i < 98U ? i : i + 68U] = static_cast<std::uint8_t>(read_bit(bptc.data(), i));
    const std::uint32_t word = reference_slot_word(static_cast<std::uint8_t>((colour << 4U) | dtype));
    for (unsigned i = 0; i < 20; ++i)
        bits[i < 10U ? 98U + i : 156U + i - 10U] = static_cast<std::uint8_t>((word >> (19U - i)) & 1U);
    const std::uint64_t sync = sync_reference[static_cast<unsigned>(kind)];
    for (unsigned i = 0; i < 48; ++i)
        bits[108 + i] = static_cast<std::uint8_t>((sync >> (47U - i)) & 1U);
    Burst burst{};
    for (unsigned i = 0; i < 132; ++i)
        burst[i] = static_cast<std::uint8_t>((bits[2U * i] << 1U) | bits[2U * i + 1U]);
    return burst;
}
Burst reference_voice(SyncKind kind) {
    Burst burst{};
    const std::uint64_t word = sync_reference[static_cast<unsigned>(kind)];
    for (unsigned j = 0; j < 24; ++j)
        burst[54U + j] = static_cast<std::uint8_t>((word >> (46U - 2U * j)) & 3U);
    return burst;
}
std::array<std::uint8_t, 12> reference_cach(bool slot2) {
    // B.17 Hamming matrix, Fig B.9 TACT-to-wire bit locations.
    constexpr unsigned tact_pos[7] = {0,4,8,12,14,18,22};
    const std::uint8_t info = static_cast<std::uint8_t>(slot2 ? 4U : 0U);
    const std::uint8_t d3 = static_cast<std::uint8_t>((info >> 3U) & 1U);
    const std::uint8_t d2 = static_cast<std::uint8_t>((info >> 2U) & 1U);
    const std::uint8_t d1 = static_cast<std::uint8_t>((info >> 1U) & 1U);
    const std::uint8_t d0 = static_cast<std::uint8_t>(info & 1U);
    const std::uint8_t parity = static_cast<std::uint8_t>(((d3 ^ d2 ^ d1) << 2U) |
                              ((d2 ^ d1 ^ d0) << 1U) | (d3 ^ d2 ^ d0));
    const std::uint8_t tact = static_cast<std::uint8_t>((info << 3U) | parity);
    std::array<std::uint8_t, 3> bytes{};
    for (unsigned k = 0; k < 7; ++k)
        write_bit(bytes.data(), tact_pos[k], ((tact >> (6U - k)) & 1U) != 0);
    std::array<std::uint8_t,12> out{};
    for (unsigned k = 0; k < 12; ++k)
        out[k] = static_cast<std::uint8_t>((bytes[k/4U] >> (6U - 2U * (k%4U))) & 3U);
    return out;
}
std::array<std::uint8_t, 128> reference_embedded(const Block& lc) {
    std::uint8_t rows[8][16]{};
    for (unsigned i = 0; i < 72; ++i)
        rows[i/11U][i%11U] = static_cast<std::uint8_t>(read_bit(lc.data(), i));
    unsigned sum = 0;
    for (unsigned i = 0; i < 9; ++i) sum += lc[i];
    const unsigned check = sum % 31U;
    for (unsigned i = 0; i < 5; ++i)
        rows[(72U + i)/11U][(72U + i)%11U] = static_cast<std::uint8_t>((check >> (4U - i)) & 1U);
    for (unsigned r = 0; r < 7; ++r) {
        const unsigned parity = parity_from_matrix(rows[r], 11, row16);
        for (unsigned b = 0; b < 5; ++b)
            rows[r][11U + b] = static_cast<std::uint8_t>((parity >> (4U - b)) & 1U);
    }
    for (unsigned c = 0; c < 16; ++c) {
        unsigned checkbit = 0;
        for (unsigned r = 0; r < 7; ++r) checkbit ^= rows[r][c];
        rows[7][c] = static_cast<std::uint8_t>(checkbit);
    }
    std::array<std::uint8_t,128> tx{};
    for (unsigned i = 0; i < 128; ++i) tx[i] = rows[i%8U][i/8U];
    return tx;
}
Burst reference_embedded_burst(const std::array<std::uint8_t,128>& tx, unsigned stage, unsigned colour) {
    std::array<std::uint8_t,264> bits{};
    const unsigned lcss = stage==0U ? 1U : (stage==3U ? 2U : 3U);
    const std::uint16_t emb = reference_emb_word(static_cast<std::uint8_t>((colour << 3U) | lcss));
    for (unsigned i = 0; i < 8; ++i)
        bits[108U+i] = static_cast<std::uint8_t>((emb >> (15U-i)) & 1U);
    for (unsigned i = 0; i < 32; ++i) bits[116U+i] = tx[stage*32U+i];
    for (unsigned i = 0; i < 8; ++i)
        bits[148U+i] = static_cast<std::uint8_t>((emb >> (7U-i)) & 1U);
    Burst out{};
    for (unsigned i = 0; i < 132; ++i)
        out[i] = static_cast<std::uint8_t>((bits[i*2U] << 1U) | bits[i*2U+1U]);
    return out;
}
void append(std::vector<std::uint8_t>& v, const std::array<std::uint8_t,12>& cach, const Burst& b) {
    v.insert(v.end(), cach.begin(), cach.end());
    v.insert(v.end(), b.begin(), b.end());
}
void collect(const Event& event, void* ctx) noexcept {
    static_cast<std::vector<Event>*>(ctx)->push_back(event);
}
std::vector<Event> decode_stream(const std::vector<std::uint8_t>& data, unsigned chunk) {
    Decoder decoder;
    std::vector<Event> events;
    for (std::size_t offset = 0; offset < data.size(); offset += chunk) {
        const std::size_t n = ((data.size()-offset)<chunk ? data.size()-offset : chunk);
        if (!decoder.feed(data.data()+offset, n, collect, &events)) std::abort();
    }
    return events;
}

bool golay_test() {
    for (unsigned k = 0; k < 8; ++k)
        REQUIRE(sync_word(static_cast<SyncKind>(k)) == sync_reference[k]);
    // Rendered 2023/2026 Golay matrices identical; vector 0x21 -> 0x21E26.
    REQUIRE(reference_slot_word(0x21) == 0x21e26U);
    unsigned min_distance = 21;
    for (unsigned a = 0; a < 256; ++a) {
        const std::uint32_t code = reference_slot_word(static_cast<std::uint8_t>(a));
        std::uint8_t cc=255, type=255, errors=255;
        REQUIRE(decode_slot_type(code,cc,type,errors));
        REQUIRE(cc==(a>>4U) && type==(a&15U) && errors==0U);
        for (unsigned b = a+1; b < 256; ++b) {
            const unsigned d = hamming(code ^ reference_slot_word(static_cast<std::uint8_t>(b)));
            if (d < min_distance) min_distance = d;
        }
    }
    REQUIRE(min_distance==8U);
    const std::uint32_t reference = reference_slot_word(0x21);
    for (unsigned x=0;x<20;++x) for(unsigned y=x+1;y<20;++y)
      for(unsigned z=y+1;z<20;++z) {
        std::uint8_t cc=255, type=255, corrections=255;
        REQUIRE(decode_slot_type(reference^(1U<<x)^(1U<<y)^(1U<<z),cc,type,corrections));
        REQUIRE(cc==2U && type==1U && corrections==3U);
    }
    std::uint8_t cc=0xa1, type=0xb2, corrections=0xc3;
    REQUIRE(!decode_slot_type(0x100000U,cc,type,corrections));
    REQUIRE(cc==0xa1 && type==0xb2 && corrections==0xc3);
    for (unsigned k=0;k<17;++k) {
        cc=0xa1;type=0xb2;corrections=0xc3;
        REQUIRE(!decode_slot_type(reference^(0x0fU<<k),cc,type,corrections));
        REQUIRE(cc==0xa1 && type==0xb2 && corrections==0xc3);
    }
    std::puts("PASS Golay 256 codewords, dmin=8, 1140 triple flips, 4-bit rejects, 8 printed sync words");
    return true;
}
bool cach_bptc_test() {
    // Fig B.9 mapping plus all 112 valid information/single-error TACT words.
    const auto zero=reference_cach(false);
    const auto one=reference_cach(true);
    REQUIRE(zero != one);
    for (auto c : {zero,one}) {
        CachWire wire{};
        for (unsigned i=0;i<12;++i)wire[i/4U] |= static_cast<std::uint8_t>(c[i] << (6U-2U*(i%4U)));
        CachFields f{};std::uint8_t repair=255;
        REQUIRE(unpack_cach(wire,f,repair));
        REQUIRE(f.tdma_channel==(c==one));
        REQUIRE(repair==0U);
    }
    for (unsigned t=0;t<16;++t) {
        const auto encoded=encode_tact(static_cast<std::uint8_t>(t));
        for (unsigned bit=0;bit<7;++bit) {
            std::uint8_t info=255, repaired=255;
            REQUIRE(decode_tact(static_cast<std::uint8_t>(encoded^(1U<<bit)),info,repaired));
            REQUIRE(info==t && repaired==1U);
        }
    }
    // Direct oracle for B.1 interleaving; inject one-hot at every index.
    for(unsigned i=0;i<196;++i) {
        BptcWire original{},tx{},recovered{};
        write_bit(original.data(),i,true);
        REQUIRE(bptc_interleave(original,tx));
        REQUIRE(read_bit(tx.data(), (i*181U)%196U));
        REQUIRE(bptc_deinterleave(tx,recovered));
        REQUIRE(recovered==original);
    }
    auto lc=reference_lc();
    auto tx=reference_bptc(lc);
    std::array<std::uint8_t,12> decoded{};
    std::uint16_t corrections=99;
    REQUIRE(decode_bptc(tx,decoded,corrections));
    REQUIRE(decoded==lc && corrections==0U);
    for (unsigned bit : {5U,20U,78U,145U,194U}) {
        auto corrupted=tx;
        write_bit(corrupted.data(),bit,!read_bit(corrupted.data(),bit));
        REQUIRE(decode_bptc(corrupted,decoded,corrections));
        REQUIRE(decoded==lc && corrections>0U);
    }
    // D0 rejects invalid padding without modifying output.
    BptcWire invalid=tx,untouched{};
    invalid[24] |= 1U;
    untouched.fill(0xa5);
    REQUIRE(!bptc_deinterleave(invalid,untouched));
    for(const auto v:untouched) REQUIRE(v==0xa5U);
    std::puts("PASS CACH Hamming, 196-bit BPTC permutation, product parity and corruption");
    return true;
}
bool integrity_test() {
    // RS B.18 verified with both header and terminator masks B.21.
    for (unsigned mode : {1U,2U}) {
        Block block=reference_lc(0x123,0x112233, mode==1?0U:3U,mode);
        REQUIRE(check_full_lc(block,static_cast<std::uint8_t>(mode)));
        REQUIRE(!check_full_lc(block,static_cast<std::uint8_t>(mode==1?2:1)));
        Event identity{};
        REQUIRE(!parse_identity(block.data(),identity));
        identity.integrity_valid=true;
        REQUIRE(parse_identity(block.data(),identity));
        REQUIRE(identity.destination_id==0x123 && identity.source_id==0x112233);
        REQUIRE(identity.evidence==Evidence::identity_valid);
        block[6] ^= 0x20U;
        REQUIRE(!check_full_lc(block,static_cast<std::uint8_t>(mode)));
        auto bad=reference_burst(SyncKind::ms_data,2,mode,block);
        auto events=decode_stream({bad.begin(),bad.end()},1);
        REQUIRE(events.size()==1 && events[0].bptc_valid && !events[0].integrity_valid);
        REQUIRE(events[0].identity==IdentityKind::none);
    }
    Block crc{};
    for(unsigned i=0;i<10;++i)crc[i]=static_cast<std::uint8_t>(i);
    REQUIRE(reference_crc(crc)==0xdc87U);
    for (auto type : {3U,6U,11U}) {
        const std::uint16_t mask= type==3?0xa5a5U:(type==6?0xccccU:0x3333U);
        const auto value=static_cast<std::uint16_t>(reference_crc(crc)^mask);
        crc[10]=static_cast<std::uint8_t>(value>>8U);
        crc[11]=static_cast<std::uint8_t>(value);
        REQUIRE(check_crc16(crc,static_cast<std::uint8_t>(type)));
        REQUIRE(!check_crc16(crc,1));
        auto b=reference_burst(SyncKind::ms_data,2,type,crc);
        auto events=decode_stream({b.begin(),b.end()},132);
        REQUIRE(events.size()==1 && events[0].integrity_valid);
        REQUIRE(events[0].evidence==Evidence::check_valid);
        REQUIRE(events[0].identity==IdentityKind::none);
        crc[3]^=1U;
        REQUIRE(!check_crc16(crc,static_cast<std::uint8_t>(type)));
        crc[3]^=1U;
    }
    // Valid RS but unrecognized feature/vendor or zero IDs MUST NOT claim identity.
    auto bad_fid=reference_lc();
    bad_fid[1]=1;
    Event e{};e.integrity_valid=true;
    REQUIRE(!parse_identity(bad_fid.data(),e));
    auto zero_id=reference_lc(0,42);
    e={};e.integrity_valid=true;
    REQUIRE(!parse_identity(zero_id.data(),e));
    // Two independent content-bit flips after regenerated FEC must not publish IDs.
    // Re-encode a valid RS frame containing a non-standard FID. The packet
    // integrity is valid but no standard voice radio identity may be inferred.
    auto vendor=reference_lc();
    vendor[1]=1;
    for (unsigned col=0;col<3;++col) {
        std::uint8_t p=0;
        for (unsigned row=0;row<9;++row) p ^= gf_multiply(vendor[row],rs_parity[row][col]);
        vendor[9+col]=static_cast<std::uint8_t>(p^0x96U);
    }
    REQUIRE(check_full_lc(vendor,1));
    auto unsupported=reference_burst(SyncKind::ms_data,6,1,vendor);
    auto no_identity=decode_stream({unsupported.begin(),unsupported.end()},7);
    REQUIRE(no_identity.size()==1 && no_identity[0].integrity_valid);
    REQUIRE(no_identity[0].evidence==Evidence::check_valid);
    REQUIRE(no_identity[0].identity==IdentityKind::none);
    const auto good=reference_lc();
    for (unsigned i=0;i<9;++i) {
        auto corrupted=good;
        corrupted[i]^=1U;
        const auto burst=reference_burst(SyncKind::ms_data,7,1,corrupted);
        const auto results=decode_stream({burst.begin(),burst.end()},7);
        REQUIRE(results.size()==1 && !results[0].integrity_valid);
        REQUIRE(results[0].identity==IdentityKind::none);
    }
    std::puts("PASS independent RS B.18, CRC B.3.8, masks B.21, zero false identities");
    return true;
}
bool stream_test() {
    const auto first=reference_burst(SyncKind::bs_data,1,1,reference_lc(123,456));
    const auto second=reference_burst(SyncKind::bs_data,3,2,reference_lc(888,999,3,2));
    std::vector<std::uint8_t> stream{3,1,2,3,1,0,2};
    append(stream,reference_cach(false),first);
    append(stream,reference_cach(true),second);
    const auto baseline=decode_stream(stream,static_cast<unsigned>(stream.size()));
    REQUIRE(baseline.size()==2);
    REQUIRE(baseline[0].slot==1 && baseline[1].slot==2);
    REQUIRE(baseline[0].slot_from_cach && baseline[1].slot_from_cach);
    REQUIRE(baseline[0].identity==IdentityKind::group);
    REQUIRE(baseline[1].identity==IdentityKind::private_call);
    for (unsigned n : {1U,2U,3U,7U,13U,131U,144U}) {
        const auto chunked=decode_stream(stream,n);
        REQUIRE(chunked.size()==baseline.size());
        for (std::size_t i=0;i<baseline.size();++i) {
            REQUIRE(chunked[i].burst_start_dibit==baseline[i].burst_start_dibit);
            REQUIRE(chunked[i].slot==baseline[i].slot);
            REQUIRE(chunked[i].destination_id==baseline[i].destination_id);
            REQUIRE(chunked[i].source_id==baseline[i].source_id);
        }
    }
    for (unsigned index=0;index<8;++index) {
        const auto kind=static_cast<SyncKind>(index);
        const auto voice=reference_voice(kind);
        const auto events=decode_stream({voice.begin(),voice.end()},1);
        REQUIRE(events.size()==1);
        if ((index & 1U)==0) REQUIRE(events[0].evidence==Evidence::sync_only);
        REQUIRE(events[0].identity==IdentityKind::none);
    }
    // KNOWN DEFECT: all-zero non-CACH guard/previous-content decodes as valid
    // Hamming TACT(7,4). D1 assumes CACH for every BS sync >=12 dibits.
    // Pin currently observed behavior; corrected behavior requires separate
    // 'CACH verified by cadence/BS mode' flag, not just Hamming correction.
    std::vector<std::uint8_t> not_cach(12,0U);
    const auto voice=reference_voice(SyncKind::bs_voice);
    not_cach.insert(not_cach.end(),voice.begin(),voice.end());
    const auto phantom=decode_stream(not_cach,1);
    REQUIRE(phantom.size()==1);
    REQUIRE(phantom[0].slot==1 && phantom[0].slot_from_cach && phantom[0].cach_present);
    std::puts("KNOWN DEFECT D1-CACH-01: 12 zero guard dibits are reported as CACH slot 1");
    // Independent confirmed severity: zero/noise prefix MUST NOT upgrade
    // sync-only evidence into identity, even if inferred slot is incorrect.
    REQUIRE(phantom[0].evidence==Evidence::sync_only);
    REQUIRE(!phantom[0].integrity_valid && phantom[0].identity==IdentityKind::none);
    // A one-bit-corrupted 48-bit SYNC shall not upgrade any event into a
    // decoded identity, even if the rest of the burst is a valid LC.
    auto bad_sync=first;
    bad_sync[54] ^= 1U;
    const auto sync_damaged=decode_stream({bad_sync.begin(),bad_sync.end()},13);
    REQUIRE(sync_damaged.empty());
    // Deterministic pseudo-random hard dibits with no deliberately injected
    // sync: cannot manufacture a verified identity from noise.
    std::vector<std::uint8_t> noise(4096);
    std::uint32_t rng=0x12345678U;
    for (auto& d : noise) {
        rng ^= rng << 13U; rng ^= rng >> 17U; rng ^= rng << 5U;
        d=static_cast<std::uint8_t>(rng & 3U);
    }
    const auto random_events=decode_stream(noise,1);
    for(const auto& e:random_events) REQUIRE(e.identity==IdentityKind::none);
    Decoder d;
    std::vector<Event> output;
    const std::uint8_t invalid[]={0,1,4,2};
    REQUIRE(!d.feed(invalid,4,collect,&output));
    REQUIRE(d.dibits_consumed()==0);
    REQUIRE(!d.feed(nullptr,1,collect,&output));
    REQUIRE(d.dibits_consumed()==0);
    REQUIRE(d.feed(stream.data(),stream.size()-1,collect,&output));
    REQUIRE(output.size()==1);
    d.reset();
    REQUIRE(d.dibits_consumed()==0);
    REQUIRE(d.feed(stream.data(),stream.size(),collect,&output));
    REQUIRE(output.size()==3);
    std::puts("PASS dual-slot chunk invariance, sync-only, reset, invalid input, no identity from noise");
    return true;
}
bool late_entry_test() {
    // Four late-entry bursts carry a 128-bit row/column-codeword over stages B-E.
    const auto lc=reference_lc(991,0x112233);
    const auto payload=reference_embedded(lc);
    std::vector<std::uint8_t> stream;
    append(stream,reference_cach(false),reference_voice(SyncKind::bs_voice));
    for (unsigned stage=0;stage<4;++stage) {
        append(stream,reference_cach(true),reference_voice(SyncKind::ms_voice));
        append(stream,reference_cach(false),reference_embedded_burst(payload,stage,7));
    }
    const auto base=decode_stream(stream,1);
    unsigned ids=0;
    for (const auto& e:base) if(e.late_entry) {
        REQUIRE(e.evidence==Evidence::identity_valid && e.integrity_valid);
        REQUIRE(e.slot==1 && e.destination_id==991 && e.source_id==0x112233);
        ++ids;
    }
    REQUIRE(ids==1);
    for(unsigned chunk : {2U,7U,31U,144U}) {
        const auto events=decode_stream(stream,chunk);
        unsigned found=0;
        for (const auto& e:events) if(e.late_entry) ++found;
        REQUIRE(found==1);
    }
    // Damage one embedded-FEC bit in stage B (not its QR bits), enforce fail closed.
    auto damaged=stream;
    damaged[144U+144U+12U+60U] ^= 1U;
    const auto events=decode_stream(damaged,17);
    for (const auto& e:events) REQUIRE(!e.late_entry);
    // Missing a full slot burst breaks required cadence and must not fabricate LC.
    auto gap=stream;
    gap.erase(gap.begin()+144, gap.begin()+288);
    const auto short_events=decode_stream(gap,13);
    for (const auto& e:short_events) REQUIRE(!e.late_entry);
    std::puts("PASS 4-fragment late-entry LC, CRC-like 5-bit checksum, chunking, corruption and dropout");
    return true;
}
} // namespace
int main(int argc, char** argv) {
    if (argc!=2) { std::fprintf(stderr,"usage: dmr_protocol_conformance golay|cach_bptc|integrity|stream|late_entry\n"); return 2; }
    const std::string selected=argv[1];
    bool result=false;
    if(selected=="golay")result=golay_test();
    else if(selected=="cach_bptc")result=cach_bptc_test();
    else if(selected=="integrity")result=integrity_test();
    else if(selected=="stream")result=stream_test();
    else if(selected=="late_entry")result=late_entry_test();
    else { std::fprintf(stderr,"unknown suite: %s\n",argv[1]); return 2; }
    return result ? 0 : 1;
}
