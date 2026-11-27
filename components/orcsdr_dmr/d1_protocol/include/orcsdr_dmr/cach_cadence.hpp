// D1-S: CACH confidence is inferred from burst cadence, not Hamming alone.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <cstdint>

namespace orcsdr::dmr::d1 {

// These are evidence labels, NOT cryptographic authentication or proof that
// an arbitrary prefix was transmitted as a CACH. The 17-bit Short LC is not
// checked by the burst framer.
enum class CachConfidence : std::uint8_t {
    none,                 // no TACT candidate or no full preceding prefix
    tentative,            // TACT is decodable (all 7-bit values are!)
    cadence_validated,    // four clean, alternating 144-dibit BS sync bursts
    embedded_lc_validated // repeated same-slot CACH plus complete embedded LC
};

struct CachAssessment {
    CachConfidence confidence = CachConfidence::none;
    std::uint8_t candidate_slot = 0;  // diagnostic only; never a confirmed slot
    std::uint8_t corrections = 0;
    std::uint8_t cadence_bursts = 0;
};

// Zero allocation, wrap-safe subtraction, bounded state. Input "has_tact"
// only establishes a parseable candidate, never CACH authenticity.
class CachCadence final {
public:
    static constexpr std::uint64_t kBurstStrideDibits = 144;
    static constexpr std::uint8_t kConfirmationBursts = 4;

    void reset() noexcept {
        have_previous_ = false;
        previous_start_ = 0;
        previous_slot_ = 0;
        streak_ = 0;
    }

    CachAssessment observe(std::uint64_t start, bool bs_sync,
                           bool has_tact, std::uint8_t slot,
                           std::uint8_t corrections) noexcept {
        CachAssessment result{};
        if (!bs_sync || !has_tact || slot < 1 || slot > 2 || corrections > 1) {
            reset();
            return result;
        }
        result.confidence = CachConfidence::tentative;
        result.candidate_slot = slot;
        result.corrections = corrections;

        // Any repaired TACT breaks the lock. Correctable Hamming does NOT
        // detect a counterfeit CACH prefix, including an all-zero prefix.
        if (corrections != 0) {
            reset();
            return result;
        }
        if (have_previous_ && start > previous_start_ &&
            start - previous_start_ == kBurstStrideDibits &&
            slot != previous_slot_) {
            if (streak_ < kConfirmationBursts) ++streak_;
        } else {
            streak_ = 1;
        }
        previous_start_ = start;
        previous_slot_ = slot;
        have_previous_ = true;
        result.cadence_bursts = streak_;
        if (streak_ >= kConfirmationBursts)
            result.confidence = CachConfidence::cadence_validated;
        return result;
    }

private:
    bool have_previous_ = false;
    std::uint64_t previous_start_ = 0;
    std::uint8_t previous_slot_ = 0;
    std::uint8_t streak_ = 0;
};
} // namespace orcsdr::dmr::d1
