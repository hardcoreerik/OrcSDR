// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "orcsdr_dmr_trunk/trunk.hpp"
#include "orcsdr_dmr/d1_bridge.hpp"
namespace orcsdr::dmr::trunk {
// Worker-owned adapter. Radio epoch and D1 stream epoch are distinct.
// Feed ALL D1 events, not only integrity-valid ones. Clear on event loss.
class D1Adapter {
    Parser parser_;
    bool have_=false,replay_=false;
    uint32_t radio_=0,stream_=0,hz_=0;
public:
    void reset() noexcept {parser_.reset();have_=false;}
    Parse consume(const d1::integration::Event& ev,uint32_t radio_epoch,uint32_t hz,
                  uint32_t now,bool replay,Grant& grant) noexcept {
        if(!have_||radio_!=radio_epoch||stream_!=ev.stream_epoch||hz_!=hz||replay_!=replay){
            reset();have_=true;radio_=radio_epoch;stream_=ev.stream_epoch;hz_=hz;replay_=replay;
        }
        const auto& e=ev.decoded;
        // Embedded LC late-entry event repeats a previous physical burst; do not treat as another burst.
        if(e.late_entry)return Parse::ignored;
        Block b{};b.bytes=e.control_payload;b.epoch=radio_epoch;b.frequency=hz;b.ms=now;
        b.dibit=e.burst_start_dibit;b.slot=e.slot;b.colour=e.colour_code;b.type=e.data_type;
        b.bs=e.sync==SyncKind::bs_data||e.sync==SyncKind::bs_voice;
        b.fec=e.control_payload_valid&&e.golay_valid&&e.bptc_valid;b.exact=e.sync_bit_errors==0;
        b.replay=replay;
        if(!e.slot_from_cach||e.cach_confidence!=d1::CachConfidence::cadence_validated)b.slot=0;
        return parser_.feed(b,grant);
    }
};
}
