#include "orcsdr_dmr/d1_bridge.hpp"
#include <algorithm>

namespace orcsdr::dmr::d1::integration {

void Receiver::clear_protocol() noexcept {
    protocol_.reset();
    sample_history_.fill(0);
}

void Receiver::reset() noexcept {
    frontend_.reset();
    clear_protocol();
    prior_locks_ = prior_losses_ = 0;
    stats_ = {};
    active_callback_ = nullptr;
    active_user_ = nullptr;
    call_events_ = 0;
}

void Receiver::discontinuity() noexcept {
    frontend_.discontinuity();
    clear_protocol();
    prior_locks_ = frontend_.stats().locks;
    prior_losses_ = frontend_.stats().lost_locks;
    ++stats_.stream_epoch;
}

void Receiver::on_protocol(const d1::Event& event, void* context) noexcept {
    static_cast<Receiver*>(context)->deliver(event);
}

void Receiver::deliver(const d1::Event& decoded) noexcept {
    Event result{};
    result.decoded = decoded;
    result.stream_epoch = stats_.stream_epoch;
    const auto total = protocol_.dibits_consumed();
    // Avoid fabricating timestamps for an event whose first transmitted dibit
    // was never emitted. The protocol scanner only emits full 132-dibit bursts.
    if (total > decoded.burst_start_dibit &&
        total - decoded.burst_start_dibit <= sample_history_.size()) {
        result.sample_time_known = true;
        result.burst_start_sample =
            sample_history_[static_cast<std::size_t>(decoded.burst_start_dibit %
                                                    sample_history_.size())];
    }
    ++stats_.protocol_events;
    ++call_events_;
    if (decoded.integrity_valid && decoded.evidence == d1::Evidence::identity_valid)
        ++stats_.validated_identities;
    if (active_callback_) active_callback_(result, active_user_);
}

Result Receiver::process(const ComplexSample* input, std::size_t count,
                         EventSink callback, void* user) noexcept {
    Result result{};
    if (!input && count != 0U) {
        result.ok = false;
        ++stats_.invalid_calls;
        return result;
    }
    active_callback_ = callback;
    active_user_ = user;
    call_events_ = 0;
    while (result.consumed < count) {
        Symbol symbol{};
        const auto ask = std::min(kStepSamples, count - result.consumed);
        // Limit each frontend call to <=8 IQ samples and one output dibit.
        // With its >=9.95-sample symbol spacing this prevents a lost-lock
        // and a new acquisition from being conflated in one returned batch.
        const auto step = frontend_.process(input + result.consumed, ask, &symbol, 1);
        if (step.consumed == 0 || step.consumed > ask || step.produced > 1) {
            result.ok = false;
            ++stats_.invalid_calls;
            break;
        }
        result.consumed += step.consumed;
        stats_.iq_samples += step.consumed;
        const auto& fs = frontend_.stats();
        const bool lost = fs.lost_locks != prior_losses_;
        const bool acquired = fs.locks != prior_locks_;
        if (lost || acquired) {
            // Conservative: an interrupted/unknown window never gets appended
            // to the previous partial burst as if the stream were continuous.
            clear_protocol();
            if (lost) {
                stats_.lost_locks += fs.lost_locks - prior_losses_;
                ++stats_.stream_epoch; // frontend sample indices may restart on IQ loss
            }
            if (acquired) {
                stats_.acquisitions += fs.locks - prior_locks_;
                stats_.acquisition_bursts_missed += fs.locks - prior_locks_;
            }
            prior_locks_ = fs.locks;
            prior_losses_ = fs.lost_locks;
        }
        if (step.produced == 1 && fs.locked) {
            const auto index = protocol_.dibits_consumed();
            sample_history_[static_cast<std::size_t>(index % sample_history_.size())] =
                symbol.sample_index;
            const std::uint8_t dibit = symbol.dibit;
            if (!protocol_.feed(&dibit, 1, on_protocol, this)) {
                result.ok = false;
                ++stats_.invalid_calls;
                break;
            }
            ++stats_.dibits_sent;
        }
    }
    result.events = call_events_;
    active_callback_ = nullptr;
    active_user_ = nullptr;
    return result;
}

} // namespace orcsdr::dmr::d1::integration
