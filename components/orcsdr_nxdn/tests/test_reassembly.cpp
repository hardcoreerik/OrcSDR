// Synthetic tests of OPAQUE SACCH fragment reassembly. These prove transport behaviour only; they say nothing about NXDN
// Layer 3 field layouts, which are deliberately not interpreted anywhere.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orcsdr_nxdn/sacch_reassembly.hpp"
#include <cstdio>
#include <vector>
using namespace orcsdr::nxdn;
#define TEST(x) do { if(!(x)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #x); return false; } } while (0)
namespace {
struct Sink { std::vector<OpaqueMessage> out; };
void sink_cb(const OpaqueMessage& m, void* u) noexcept { static_cast<Sink*>(u)->out.push_back(m); }

// Distinct, recognisable bit pattern per fragment so ordering/pass-through errors are visible.
Event frag(std::uint64_t start, std::uint8_t structure, std::uint8_t ran = 7, std::uint8_t seed = 0) {
    Event e{};
    e.frame_start_dibit = start; e.mode = Mode::nxdn96; e.rf_channel = 2; e.functional_channel = 2;
    e.direction = 0; e.structure = structure; e.ran = ran;
    for (std::size_t i = 0; i < kSacchFragmentBits; ++i)
        e.sacch_fragment_bits[i] = static_cast<std::uint8_t>(((i * 5U + structure * 3U + seed) >> 1U) & 1U);
    return e;
}
bool feed(SacchReassembler& r, Sink& s, const Event& e) { return r.feed(e, sink_cb, &s); }

bool test_in_order() {
    SacchReassembler r; Sink s;
    for (unsigned i = 0; i < 4; ++i) TEST(feed(r, s, frag(100 + 192U * i, static_cast<std::uint8_t>(3U - i))));
    TEST(s.out.size() == 1);
    const auto& m = s.out[0];
    TEST(m.bit_count == 72 && m.anchored && m.ran == 7 && m.first_structure == 3);
    TEST(m.first_frame_start_dibit == 100 && m.last_frame_start_dibit == 100 + 3 * 192);
    // pass-through: arrival order, every bit unchanged
    for (unsigned f = 0; f < 4; ++f) {
        const Event e = frag(0, static_cast<std::uint8_t>(3U - f));
        for (std::size_t i = 0; i < kSacchFragmentBits; ++i) TEST(m.bits[f * kSacchFragmentBits + i] == e.sacch_fragment_bits[i]);
    }
    return true;
}
bool test_starts_only_at_anchor() {
    SacchReassembler r; Sink s;
    // stream joins mid-message at structure 1: s1,s0 ignored, then 3,2,1,0 completes
    std::uint64_t p = 0; const std::uint8_t seq[] = {1, 0, 3, 2, 1, 0};
    for (auto st : seq) { TEST(feed(r, s, frag(p, st))); p += 192; }
    TEST(s.out.size() == 1 && s.out[0].first_frame_start_dibit == 2 * 192);
    TEST(r.counters().waiting_for_anchor == 2 && r.counters().fragments_accepted == 4);
    return true;
}
bool test_continuous_stream() {
    SacchReassembler r; Sink s; std::uint64_t p = 0;
    for (unsigned i = 0; i < 40; ++i) { TEST(feed(r, s, frag(p, static_cast<std::uint8_t>((3U - i) & 3U)))); p += 192; }
    TEST(s.out.size() == 10 && r.counters().messages_completed == 10);
    TEST(r.counters().dropped_gap == 0 && r.counters().dropped_out_of_order == 0);
    return true;
}
bool test_missing_fragment_and_gap() {
    SacchReassembler r; Sink s;
    TEST(feed(r, s, frag(0, 3))); TEST(feed(r, s, frag(192, 2)));
    TEST(feed(r, s, frag(576, 0)));               // frame 384 missing: spacing 384, structure 0 != 1
    TEST(r.counters().dropped_gap == 1 && r.fragments_pending() == 0 && s.out.empty());
    // after the loss the next anchor starts a clean message
    for (unsigned i = 0; i < 4; ++i) TEST(feed(r, s, frag(768 + 192U * i, static_cast<std::uint8_t>(3U - i))));
    TEST(s.out.size() == 1 && s.out[0].first_frame_start_dibit == 768);
    return true;
}
bool test_out_of_order_and_repeat() {
    SacchReassembler r; Sink s;
    TEST(feed(r, s, frag(0, 3)));
    TEST(feed(r, s, frag(192, 1)));               // right spacing, wrong structure
    TEST(r.counters().dropped_out_of_order == 1 && r.fragments_pending() == 0);
    TEST(feed(r, s, frag(384, 3))); TEST(feed(r, s, frag(384, 2))); // same position repeated -> malformed
    TEST(r.counters().rejected_malformed == 1 && r.fragments_pending() == 0);
    TEST(feed(r, s, frag(1000, 3))); TEST(feed(r, s, frag(808, 2))); // backwards position -> malformed
    TEST(r.counters().rejected_malformed == 2 && s.out.empty());
    return true;
}
bool test_context_change() {
    SacchReassembler r; Sink s;
    TEST(feed(r, s, frag(0, 3))); TEST(feed(r, s, frag(192, 2, 6)));  // RAN changed mid-message
    TEST(r.counters().dropped_context_change == 1 && r.fragments_pending() == 0);
    Event a = frag(384, 3); TEST(feed(r, s, a));
    Event b = frag(576, 2); b.direction = 1; TEST(feed(r, s, b));
    TEST(r.counters().dropped_context_change == 2 && s.out.empty());
    return true;
}
bool test_malformed() {
    SacchReassembler r; Sink s;
    Event e = frag(0, 3); e.structure = 4; TEST(feed(r, s, e));
    e = frag(192, 3); e.sacch_fragment_bits[5] = 2; TEST(feed(r, s, e));
    e = frag(384, 3); e.mode = static_cast<Mode>(9); TEST(feed(r, s, e));
    TEST(r.counters().rejected_malformed == 3 && r.counters().fragments_accepted == 0 && s.out.empty());
    // a malformed event inside a message discards the partial one
    TEST(feed(r, s, frag(1000, 3))); e = frag(1192, 2); e.sacch_fragment_bits[0] = 7; TEST(feed(r, s, e));
    TEST(r.fragments_pending() == 0);
    return true;
}
bool test_reset_and_retune() {
    SacchReassembler r; Sink s;
    TEST(feed(r, s, frag(0, 3))); TEST(feed(r, s, frag(192, 2)));
    r.reset();
    TEST(r.fragments_pending() == 0 && r.counters().fragments_accepted == 0);
    // decoder positions restart at 0 after a decoder reset: must be accepted again
    for (unsigned i = 0; i < 4; ++i) TEST(feed(r, s, frag(192U * i, static_cast<std::uint8_t>(3U - i))));
    TEST(s.out.size() == 1);
    return true;
}
bool test_config() {
    ReassemblyConfig bad{}; bad.fragments = 0; TEST(!SacchReassembler(bad).valid_config());
    bad = {}; bad.fragments = 5; TEST(!SacchReassembler(bad).valid_config());
    bad = {}; bad.anchor_structure = 4; TEST(!SacchReassembler(bad).valid_config());
    bad = {}; bad.order = static_cast<FragmentOrder>(7); TEST(!SacchReassembler(bad).valid_config());
    SacchReassembler r(bad); Sink s; TEST(!r.feed(frag(0, 3), sink_cb, &s) && r.counters().fragments_accepted == 0);
    ReassemblyConfig asc{}; asc.order = FragmentOrder::ascending; asc.anchor_structure = 0;
    SacchReassembler a(asc); for (unsigned i = 0; i < 4; ++i) TEST(feed(a, s, frag(192U * i, static_cast<std::uint8_t>(i))));
    TEST(s.out.size() == 1 && s.out[0].first_structure == 0);
    ReassemblyConfig un{}; un.require_anchor = false; un.fragments = 2;
    SacchReassembler u(un); Sink t; TEST(feed(u, t, frag(0, 1))); TEST(feed(u, t, frag(192, 0)));
    TEST(t.out.size() == 1 && !t.out[0].anchored && t.out[0].bit_count == 36);
    return true;
}
bool test_bounded() {
    // Fixed footprint regardless of input volume; counters keep running, nothing grows.
    static_assert(sizeof(OpaqueMessage) <= 128, "OpaqueMessage is fixed size");
    SacchReassembler r; Sink s; std::uint64_t p = 0;
    for (unsigned i = 0; i < 100000; ++i) {
        Sink* sp = &s; if (s.out.size() > 8) s.out.clear();
        TEST(r.feed(frag(p, static_cast<std::uint8_t>((3U - i) & 3U)), sink_cb, sp)); p += 192;
    }
    TEST(r.counters().messages_completed == 25000);
    return true;
}
}
int main() {
    struct { const char* n; bool (*f)(); } t[] = {
        {"in_order", test_in_order}, {"anchor", test_starts_only_at_anchor}, {"continuous", test_continuous_stream},
        {"missing_gap", test_missing_fragment_and_gap}, {"out_of_order", test_out_of_order_and_repeat},
        {"context", test_context_change}, {"malformed", test_malformed}, {"reset", test_reset_and_retune},
        {"config", test_config}, {"bounded", test_bounded}};
    for (auto& x : t) { if (!x.f()) { std::fprintf(stderr, "FAILED %s\n", x.n); return 1; } }
    std::puts("PASS NXDN opaque SACCH reassembly (synthetic transport tests; no Layer 3 interpretation)");
    return 0;
}
