#pragma once
#include "sstv_receiver.hpp"
#include "sstv_archive.hpp"
#include "../../../orcdial/src/sstv_control.hpp"
namespace orcsdr::sstv {
// Dashboard-local meanings inside the unchanged authenticated RADIO_STATE.
inline void dial_state(orc::Packet& p,const Metrics& m,const sstv_archive::Stats& a) {
  p.view=uint8_t(m.state);p.action=uint8_t(m.received_mode);
  p.selected=int32_t(m.session_id);p.value=int32_t(m.frames);
  p.item_count=uint32_t(m.row)|(uint32_t(m.rows)<<16);
  p.revision=a.saved;p.capabilities=orc::sstv_control::kSchemaV1|(a.failed&0xffffu);
  p.squelch=int16_t(a.pending<32767?a.pending:32767);
  p.flags=(m.signal?orc::sstv_control::kSignal:0)|(m.receiving?orc::sstv_control::kReceiving:0)|
      (m.storage_ok?orc::sstv_control::kSd:0);
}
}
