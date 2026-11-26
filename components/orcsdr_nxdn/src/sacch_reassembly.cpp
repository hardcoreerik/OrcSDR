// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orcsdr_nxdn/sacch_reassembly.hpp"

namespace orcsdr::nxdn {

SacchReassembler::SacchReassembler(ReassemblyConfig config) noexcept : cfg_(config) {
    config_ok_ = (cfg_.order == FragmentOrder::descending || cfg_.order == FragmentOrder::ascending) &&
                 cfg_.anchor_structure <= 3U && cfg_.fragments >= 1U && cfg_.fragments <= kMaxFragmentsPerMessage;
}

void SacchReassembler::reset() noexcept {
    have_ = 0;
    msg_ = OpaqueMessage{};
    counters_ = ReassemblyCounters{};
}

bool SacchReassembler::feed(const Event& e, OpaqueSink sink, void* user) noexcept {
    if (!config_ok_) return false;
    bool bad = e.structure > 3U || (e.mode != Mode::nxdn48 && e.mode != Mode::nxdn96);
    for (std::size_t i = 0; i < kSacchFragmentBits && !bad; ++i) bad = e.sacch_fragment_bits[i] > 1U;
    // A fragment must lie strictly after the previous accepted one.
    if (!bad && have_ && e.frame_start_dibit <= msg_.last_frame_start_dibit) bad = true;
    if (bad) {
        ++counters_.rejected_malformed;
        drop();
        return true;
    }
    if (have_) {
        if (e.mode != msg_.mode || e.ran != msg_.ran || e.rf_channel != msg_.rf_channel ||
            e.functional_channel != msg_.functional_channel || e.direction != msg_.direction) {
            ++counters_.dropped_context_change;
            drop();
        } else if (e.frame_start_dibit - msg_.last_frame_start_dibit != kFrameDibits) {
            ++counters_.dropped_gap;
            drop();
        } else if (e.structure != expect_structure_) {
            ++counters_.dropped_out_of_order;
            drop();
        }
    }
    if (!have_) {
        if (cfg_.require_anchor && e.structure != cfg_.anchor_structure) {
            ++counters_.waiting_for_anchor;
            return true;
        }
        msg_ = OpaqueMessage{};
        msg_.first_frame_start_dibit = e.frame_start_dibit;
        msg_.mode = e.mode;
        msg_.rf_channel = e.rf_channel;
        msg_.functional_channel = e.functional_channel;
        msg_.direction = e.direction;
        msg_.ran = e.ran;
        msg_.first_structure = e.structure;
        msg_.anchored = cfg_.require_anchor;
    }
    for (std::size_t i = 0; i < kSacchFragmentBits; ++i)
        msg_.bits[static_cast<std::size_t>(have_) * kSacchFragmentBits + i] = e.sacch_fragment_bits[i];
    msg_.last_frame_start_dibit = e.frame_start_dibit;
    ++have_;
    ++counters_.fragments_accepted;
    expect_structure_ = cfg_.order == FragmentOrder::descending
                            ? static_cast<std::uint8_t>((e.structure + 3U) & 3U)
                            : static_cast<std::uint8_t>((e.structure + 1U) & 3U);
    if (have_ == cfg_.fragments) {
        msg_.bit_count = static_cast<std::uint8_t>(have_ * kSacchFragmentBits);
        ++counters_.messages_completed;
        have_ = 0;
        if (sink) sink(msg_, user);
    }
    return true;
}

} // namespace orcsdr::nxdn
