// OrcSDR D1 call manager. Rules ported from the offline V2 proof gate (tools/orcsdr_vocoder/replay_v2.py) and
// extended with the service-options privacy bit; all times are decoder dibits (1 dibit = 208.33 us).
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orcsdr_dmr/d1_call.hpp"

namespace orcsdr::dmr::d1 {
namespace {
struct Ident { bool ok = false; bool group = false; std::uint32_t src = 0, dst = 0; };
Ident identity_of(const std::uint8_t* lc) noexcept {
    Ident r;
    const auto flco = static_cast<std::uint8_t>(lc[0] & 0x3fU);
    const auto fid = lc[1];
    if ((lc[0] & 0x40U) != 0) return r;
    if ((fid == 0 || fid == 0x10U) && (flco == 0 || flco == 3)) {
        r.dst = static_cast<std::uint32_t>((lc[3] << 16U) | (lc[4] << 8U) | lc[5]);
        r.src = static_cast<std::uint32_t>((lc[6] << 16U) | (lc[7] << 8U) | lc[8]);
        r.group = (flco == 0);
        r.ok = r.src != 0 && r.dst != 0;
    } else if (fid == 0x10U && flco == 0x04U && lc[3] == 0 && lc[4] == 0) {
        r.dst = static_cast<std::uint32_t>(lc[5]);
        r.src = static_cast<std::uint32_t>((lc[7] << 8U) | lc[8]);
        r.group = true;
        r.ok = r.src != 0 && r.dst != 0;
    }
    return r;
}
}

void CallManager::reset() noexcept {
    data_ = {}; data_head_ = data_count_ = 0;
    calls_ = {}; privacy_ = {};
}

void CallManager::notice(NoticeKind kind, unsigned slot, std::uint64_t at) noexcept {
    if (ns_) { Notice n; n.kind = kind; n.slot = static_cast<std::uint8_t>(slot); n.at_dibit = at; ns_(n, user_); }
}
void CallManager::end_call(unsigned slot, std::uint64_t at) noexcept {
    if (slot < 1 || slot > 2 || !calls_[slot].live) return;
    calls_[slot] = {};
    ++stats_.calls_ended;
    notice(NoticeKind::call_ended, slot, at);
}

void CallManager::on_data(const Event& e) noexcept {
    if (e.sync != SyncKind::bs_data && e.sync != SyncKind::direct_ts1_data && e.sync != SyncKind::direct_ts2_data) return;
    Data d;
    d.t = e.burst_start_dibit;
    d.slot = e.slot ? e.slot
                    : ((e.cach_tact_decoded && e.cach_tact_corrections == 0) ? e.cach_candidate_slot : std::uint8_t{0});
    d.colour_code = e.colour_code;
    d.type = e.golay_valid ? e.data_type : std::uint8_t{0xff};
    if (e.golay_valid && e.data_type == 1 && e.evidence == Evidence::identity_valid) {
        d.identity = true; d.group = (e.identity == IdentityKind::group);
        d.src = e.source_id; d.dst = e.destination_id;
        d.service = e.service_options; d.protect_flag = e.protected_flag;
    }
    data_[(data_head_ + data_count_) % data_.size()] = d;
    if (data_count_ < data_.size()) ++data_count_; else data_head_ = (data_head_ + 1) % data_.size();
    if (e.golay_valid && e.data_type == 2 && d.slot) { ++stats_.terminators; end_call(d.slot, d.t); }
}

void CallManager::on_time(std::uint64_t now) noexcept {
    for (unsigned s = 1; s <= 2; ++s)
        if (calls_[s].live && now > calls_[s].t + (kMaxLostSuperframes + 2) * kSuperframeDibits) end_call(s, now);
}

void CallManager::on_group(const VoiceGroup& g) noexcept {
    ++stats_.groups_seen;
    const unsigned slot = g.slot;
    const std::uint64_t t = g.start_dibit;
    if (slot != 1 && slot != 2) { ++stats_.unknown_slot; ++stats_.groups_unqualified; notice(NoticeKind::unqualified_voice, 0, t); return; }
    auto& old = calls_[slot];
    auto unqualified = [&](bool is_protected) {
        end_call(slot, t);
        if (is_protected) { privacy_[slot] = Privacy::protected_voice; ++stats_.groups_protected; notice(NoticeKind::protected_voice, slot, t); }
        else { privacy_[slot] = Privacy::unknown; ++stats_.groups_unqualified; notice(NoticeKind::unqualified_voice, slot, t); }
    };
    if (g.pi_seen) { unqualified(true); return; }

    // Data bursts since this slot's previous group: any of them interrupts continuity; the last one may be the
    // header that opens this call. Unknown-slot data counts only if it sits on this group's burst cadence.
    const Data* last = nullptr;
    bool any_previous = false;
    for (unsigned i = 0; i < data_count_; ++i) {
        const auto& d = data_[(data_head_ + i) % data_.size()];
        if (d.t >= t || (old.live && d.t <= old.t)) continue;
        const bool mine = d.slot == slot || (d.slot == 0 && ((t - d.t + 1) % kBurstPairDibits) <= 2);
        if (mine) { any_previous = true; last = &d; }
    }
    auto near = [](std::uint64_t a, std::uint64_t b) { return a + 1 >= b && a <= b + 1; };   // clock slip tolerance: +-1 dibit
    const bool header_adjacent = last && last->slot == slot && last->colour_code == g.colour_code &&
                                 near(t - last->t, kBurstPairDibits);
    unsigned lost = 0;
    bool continuity = false;
    if (old.live && !any_previous && old.colour_code == g.colour_code && t > old.t &&
        t - old.anchor <= kAnchorMaxDibits) {
        const std::uint64_t gap = t - old.t;
        const std::uint64_t whole = (gap + kSuperframeDibits / 2) / kSuperframeDibits;
        if (whole >= 1 && near(gap, whole * kSuperframeDibits)) {
            lost = static_cast<unsigned>(whole) - 1;
            continuity = lost <= kMaxLostSuperframes;
        }
    }
    Ident header_id;
    Data header_copy;
    if (header_adjacent && last->type == 1 && last->identity) {
        header_id.ok = true; header_id.group = last->group; header_id.src = last->src; header_id.dst = last->dst;
        header_copy = *last;
    }
    Ident ident;
    bool lc_protect = false, lc_privacy_bit = false;
    if (g.lc_valid) {
        ident = identity_of(g.lc.data());
        lc_protect = (g.lc[0] & 0x80U) != 0;
        lc_privacy_bit = (g.lc[2] & 0x40U) != 0;
        const auto flco = static_cast<std::uint8_t>(g.lc[0] & 0x3fU);
        const bool is_voice_lc = (flco == 0 || flco == 3 || (g.lc[1] == 0x10U && flco == 4));
        if (lc_protect || (is_voice_lc && !ident.ok)) { unqualified(lc_protect); return; }
        if (ident.ok && lc_privacy_bit) { unqualified(true); return; }
    }
    if (last && last->type == 0 && near(t - last->t, kBurstPairDibits)) { unqualified(true); return; }   // PI header: protected, never decoded
    if (header_id.ok && (header_copy.protect_flag || (header_copy.service & 0x40U))) { unqualified(true); return; }
    if (ident.ok && header_id.ok && (ident.src != header_id.src || ident.dst != header_id.dst || ident.group != header_id.group)) {
        ++stats_.conflicting_identity; unqualified(false); return;
    }
    Ident use;
    std::uint64_t anchor = 0;
    if (ident.ok) { use = ident; anchor = t; }
    else if (header_id.ok) { use = header_id; anchor = last->t; }
    else if (continuity) { use.ok = true; use.group = old.group; use.src = old.src; use.dst = old.dst; anchor = old.anchor; }
    else { unqualified(false); return; }

    const bool same_call = continuity && old.src == use.src && old.dst == use.dst && old.group == use.group;
    if (old.live && !same_call) end_call(slot, t);
    Call& c = calls_[slot];
    const std::uint64_t epoch = same_call ? old.epoch : t;
    const bool fresh = !same_call;
    c.live = true; c.t = t; c.anchor = anchor; c.epoch = epoch; c.colour_code = g.colour_code;
    c.src = use.src; c.dst = use.dst; c.group = use.group;
    privacy_[slot] = Privacy::clear;
    ++stats_.groups_qualified;
    if (fresh) ++stats_.calls_started; else stats_.lost_superframes += lost;
    if (gs_) {
        QualifiedGroup q;
        q.slot = static_cast<std::uint8_t>(slot); q.colour_code = g.colour_code;
        q.source_id = use.src; q.destination_id = use.dst; q.group_call = use.group;
        q.epoch = epoch; q.new_call = fresh; q.lost_superframes = same_call ? lost : 0; q.group = &g;
        gs_(q, user_);
    }
}

} // namespace orcsdr::dmr::d1
